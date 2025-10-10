#include "ReactRuntime/ReactRuntime.h"
#include "ReactReconciler/ReactHostConfig.h"

#include "ReactDOM/client/ReactDOMComponent.h"
#include "reconciler/FiberNode.h"

#include "jsi/jsi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace react::ReactRuntimeTestHelper {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

Value cloneValue(Runtime& rt, const Value& value) {
  if (value.isUndefined()) {
    return Value::undefined();
  }
  if (value.isNull()) {
    return Value::null();
  }
  if (value.isBool()) {
    return Value(value.getBool());
  }
  if (value.isNumber()) {
    return Value(value.getNumber());
  }
  return Value(rt, value);
}

std::string numberToString(double value) {
  if (std::isnan(value) || !std::isfinite(value)) {
    return std::string{};
  }

  if (std::floor(value) == value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    return std::string(buffer);
  }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%g", value);
  return std::string(buffer);
}

std::string valueToString(Runtime& rt, const Value& value) {
  if (value.isString()) {
    return value.getString(rt).utf8(rt);
  }
  if (value.isNumber()) {
    return numberToString(value.getNumber());
  }
  return std::string{};
}

void collectChildValues(Runtime& rt, const Value& value, std::vector<Value>& out) {
  if (value.isUndefined() || value.isNull() || value.isBool()) {
    return;
  }

  if (value.isString() || value.isNumber()) {
    out.emplace_back(cloneValue(rt, value));
    return;
  }

  if (!value.isObject()) {
    return;
  }

  Object object = value.getObject(rt);
  if (object.isArray(rt)) {
    Array array = object.asArray(rt);
    const size_t length = array.size(rt);
    for (size_t index = 0; index < length; ++index) {
      Value entry = array.getValueAtIndex(rt, index);
      collectChildValues(rt, entry, out);
    }
    return;
  }

  if (object.hasProperty(rt, "type")) {
    out.emplace_back(cloneValue(rt, value));
  }
}

facebook::jsi::Object ensureObject(Runtime& rt, const Value& value) {
  if (value.isObject()) {
    return value.getObject(rt);
  }
  return facebook::jsi::Object(rt);
}

struct ElementExtraction {
  std::string type;
  std::string key;
  Object props;
  Value children;
};

ElementExtraction extractElement(Runtime& rt, const Object& element) {
  ElementExtraction extraction{std::string{}, std::string{}, Object(rt), Value::undefined()};

  Value typeValue = element.getProperty(rt, "type");
  if (!typeValue.isString()) {
    return extraction;
  }
  extraction.type = typeValue.getString(rt).utf8(rt);

  Value keyValue = element.getProperty(rt, "key");
  if (keyValue.isString()) {
    extraction.key = keyValue.getString(rt).utf8(rt);
  }

  Value propsValue = element.getProperty(rt, "props");
  if (propsValue.isObject()) {
    Object propsObject = propsValue.getObject(rt);
    Array names = propsObject.getPropertyNames(rt);
    const size_t length = names.size(rt);
    for (size_t index = 0; index < length; ++index) {
      Value nameValue = names.getValueAtIndex(rt, index);
      if (!nameValue.isString()) {
        continue;
      }
      const std::string propName = nameValue.getString(rt).utf8(rt);
      Value propEntry = propsObject.getProperty(rt, propName.c_str());
      if (propName == "children") {
        extraction.children = cloneValue(rt, propEntry);
        continue;
      }
      extraction.props.setProperty(rt, propName.c_str(), cloneValue(rt, propEntry));
    }
  }

  return extraction;
}

Value makeKeyValue(Runtime& rt, const std::string& key) {
  if (key.empty()) {
    return Value::undefined();
  }
  return Value(rt, String::createFromUtf8(rt, key));
}

void appendChildrenToVector(const std::shared_ptr<FiberNode>& firstChild, std::vector<std::shared_ptr<FiberNode>>& out) {
  auto current = firstChild;
  while (current) {
    out.push_back(current);
    current = current->sibling;
  }
}

std::string getFiberKey(Runtime& rt, const std::shared_ptr<FiberNode>& fiber) {
  if (!fiber) {
    return std::string{};
  }
  if (fiber->key.isString()) {
    return fiber->key.asString(rt).utf8(rt);
  }
  return std::string{};
}

Object buildPropsObject(Runtime& rt, const std::unordered_map<std::string, Value>& propsMap) {
  Object props(rt);
  for (const auto& entry : propsMap) {
    props.setProperty(rt, entry.first.c_str(), cloneValue(rt, entry.second));
  }
  return props;
}

std::shared_ptr<FiberNode> findHostParentFiber(const std::shared_ptr<FiberNode>& fiber) {
  auto parent = fiber ? fiber->returnFiber : nullptr;
  while (parent) {
    if (parent->stateNode) {
      return parent;
    }
    parent = parent->returnFiber;
  }
  return nullptr;
}

std::shared_ptr<ReactDOMInstance> findHostSibling(const std::shared_ptr<FiberNode>& fiber) {
  if (!fiber) {
    return nullptr;
  }
  auto sibling = fiber->sibling;
  while (sibling) {
  if ((sibling->flags & Placement) == 0 && sibling->stateNode) {
      return sibling->stateNode;
    }
    sibling = sibling->sibling;
  }
  return nullptr;
}

void commitDeletion(
    ReactRuntime& runtime,
    Runtime& rt,
    const std::shared_ptr<FiberNode>& parent,
    const std::shared_ptr<FiberNode>& toDelete) {
  (void)rt;
  if (!toDelete) {
    return;
  }

  auto hostParentInstance = parent ? parent->stateNode : std::shared_ptr<ReactDOMInstance>{};
  const bool parentIsContainer = parent && parent->tag == WorkTag::HostRoot;

  struct PendingRemoval {
    std::shared_ptr<FiberNode> fiber;
    std::shared_ptr<ReactDOMInstance> hostParent;
    bool parentIsContainer;
  };

  std::vector<PendingRemoval> stack;
  stack.push_back({toDelete, hostParentInstance, parentIsContainer});

  while (!stack.empty()) {
    auto current = stack.back();
    stack.pop_back();

    auto fiber = current.fiber;
    if (!fiber) {
      continue;
    }

    auto instance = fiber->stateNode;
    auto nextHostParent = current.hostParent;
    bool nextParentIsContainer = current.parentIsContainer;

    if (instance) {
      if (current.hostParent) {
        if (current.parentIsContainer) {
          hostconfig::removeChildFromContainer(runtime, current.hostParent, instance);
        } else {
          hostconfig::removeChild(runtime, current.hostParent, instance);
        }
      }
      nextHostParent = instance;
      nextParentIsContainer = false;
    }

    auto child = fiber->child;
    while (child) {
      auto nextSibling = child->sibling;
      stack.push_back({child, nextHostParent, nextParentIsContainer});
      child = nextSibling;
    }

    fiber->returnFiber.reset();
    fiber->sibling.reset();
    fiber->child.reset();
    fiber->alternate.reset();
    fiber->stateNode.reset();
    fiber->updatePayload = Value::undefined();
    fiber->memoizedProps = Value::undefined();
    fiber->pendingProps = Value::undefined();
  fiber->flags = NoFlags;
  fiber->subtreeFlags = NoFlags;
    fiber->deletions.clear();
  }
}

void commitPlacement(
    ReactRuntime& runtime,
    Runtime& rt,
    const std::shared_ptr<FiberNode>& fiber) {
  if (!fiber) {
    return;
  }

  auto parentFiber = findHostParentFiber(fiber);
  if (!parentFiber || !parentFiber->stateNode) {
    return;
  }

  auto hostParent = parentFiber->stateNode;
  auto instance = fiber->stateNode;
  bool createdInstance = false;
  std::string instanceType;
  facebook::jsi::Object instanceProps(rt);

  if (!instance) {
    if (fiber->tag == WorkTag::HostComponent) {
      if (!fiber->type.isString()) {
        return;
      }
      instanceType = fiber->type.asString(rt).utf8(rt);
      instanceProps = ensureObject(rt, fiber->memoizedProps);
      instance = hostconfig::createInstance(runtime, rt, instanceType, instanceProps);
      createdInstance = true;
    } else if (fiber->tag == WorkTag::HostText) {
      std::string text = valueToString(rt, fiber->memoizedProps);
      instance = hostconfig::createTextInstance(runtime, rt, text);
      createdInstance = true;
    }
    fiber->stateNode = instance;
  }

  if (!instance) {
    return;
  }

  auto beforeChild = findHostSibling(fiber);
  const bool parentIsContainer = parentFiber->tag == WorkTag::HostRoot;

  if (beforeChild) {
    if (parentIsContainer) {
      hostconfig::insertInContainerBefore(runtime, hostParent, instance, beforeChild);
    } else {
      hostconfig::insertBefore(runtime, hostParent, instance, beforeChild);
    }
  } else {
    if (parentIsContainer) {
      hostconfig::appendChildToContainer(runtime, hostParent, instance);
    } else {
      hostconfig::appendChild(runtime, hostParent, instance);
    }
  }

  fiber->flags = fiber->flags & ~Placement;

  if (createdInstance && fiber->tag == WorkTag::HostComponent && instance) {
    if (hostconfig::finalizeInitialChildren(runtime, rt, instance, instanceType, instanceProps)) {
  fiber->flags = fiber->flags | Update;
    }
  }
}

} // namespace

std::shared_ptr<FiberNode> cloneFiberForReuse(
    ReactRuntime& runtime,
    Runtime& rt,
    const std::shared_ptr<FiberNode>& current,
    const Value& nextProps,
    const Value& nextState) {
  if (!current) {
    return nullptr;
  }

  auto clone = std::make_shared<FiberNode>(current->tag, cloneValue(rt, nextProps), cloneValue(rt, current->key));
  clone->type = cloneValue(rt, current->type);
  clone->elementType = cloneValue(rt, current->elementType);
  clone->stateNode = current->stateNode;
  clone->returnFiber = current->returnFiber;
  clone->child = current->child;
  clone->memoizedProps = cloneValue(rt, nextProps);
  clone->memoizedState = cloneValue(rt, nextState);
  clone->flags = NoFlags;
  clone->subtreeFlags = NoFlags;
  clone->deletions.clear();
  clone->alternate = current;
  current->alternate = clone;
  clone->sibling = nullptr;
  clone->updatePayload = Value::undefined();

  Value payload;
  bool hasPayload = false;
  if (current->tag == WorkTag::HostComponent) {
    hasPayload = computeHostComponentUpdatePayload(runtime, rt, current->memoizedProps, clone->memoizedProps, payload);
  } else if (current->tag == WorkTag::HostText) {
    hasPayload = computeHostTextUpdatePayload(runtime, rt, current->memoizedProps, clone->memoizedProps, payload);
  }

  if (hasPayload) {
  clone->flags = clone->flags | Update;
  clone->updatePayload = Value(rt, payload);
  }

  return clone;
}

void commitMutationEffects(
    ReactRuntime& runtime,
    Runtime& rt,
    const std::shared_ptr<FiberNode>& root) {
  if (!root) {
    return;
  }

  std::vector<std::shared_ptr<FiberNode>> stack;
  stack.push_back(root);

  while (!stack.empty()) {
    auto fiber = stack.back();
    stack.pop_back();
    if (!fiber) {
      continue;
    }

    if (!fiber->deletions.empty()) {
      auto parent = findHostParentFiber(fiber);
      for (const auto& deletion : fiber->deletions) {
        commitDeletion(runtime, rt, parent, deletion);
      }
      fiber->deletions.clear();
  fiber->flags = fiber->flags & ~ChildDeletion;
    }

  if ((fiber->flags & Placement) != 0) {
      commitPlacement(runtime, rt, fiber);
    }

  if ((fiber->flags & Update) != 0) {
      if (fiber->tag == WorkTag::HostText) {
        auto instance = fiber->stateNode;
        if (instance) {
          std::string oldText = valueToString(rt, fiber->alternate ? fiber->alternate->memoizedProps : fiber->memoizedProps);
          std::string newText = valueToString(rt, fiber->memoizedProps);
          hostconfig::commitTextUpdate(runtime, instance, oldText, newText);
        }
      } else {
        auto instance = fiber->stateNode;
        if (instance) {
          Value prevProps = Value::undefined();
          if (fiber->alternate) {
            prevProps = Value(rt, fiber->alternate->memoizedProps);
          }
          Value nextProps = Value(rt, fiber->memoizedProps);
          hostconfig::commitUpdate(runtime, rt, instance, prevProps, nextProps, fiber->updatePayload);
        }
      }

  fiber->updatePayload = Value::undefined();
  fiber->flags = fiber->flags & ~Update;
    }

    if (fiber->sibling) {
      stack.push_back(fiber->sibling);
    }
    if (fiber->child) {
      stack.push_back(fiber->child);
    }
  }

  hostconfig::resetAfterCommit(runtime);
}

void reconcileChildren(
    ReactRuntime& runtime,
    Runtime& rt,
    const std::shared_ptr<FiberNode>& parent,
    const std::shared_ptr<FiberNode>& currentFirstChild,
    const Value& newChildren) {
  (void)runtime;
  if (!parent) {
    return;
  }

  parent->deletions.clear();
  parent->flags = parent->flags & ~ChildDeletion;

  std::vector<Value> desiredChildren;
  collectChildValues(rt, newChildren, desiredChildren);

  std::unordered_map<std::string, std::shared_ptr<FiberNode>> keyedExisting;
  std::vector<std::shared_ptr<FiberNode>> unkeyedComponents;
  std::vector<std::shared_ptr<FiberNode>> unkeyedText;

  auto existing = currentFirstChild;
  while (existing) {
    auto next = existing->sibling;
    std::string key = getFiberKey(rt, existing);
    if (!key.empty()) {
      keyedExisting.emplace(key, existing);
    } else if (existing->tag == WorkTag::HostText) {
      unkeyedText.push_back(existing);
    } else {
      unkeyedComponents.push_back(existing);
    }
    existing = next;
  }

  std::shared_ptr<FiberNode> firstNewChild;
  std::shared_ptr<FiberNode> previousNewChild;

  auto appendChild = [&](const std::shared_ptr<FiberNode>& fiber, uint32_t index) {
    fiber->index = index;
    fiber->returnFiber = parent;
    if (previousNewChild) {
      previousNewChild->sibling = fiber;
    } else {
      firstNewChild = fiber;
    }
    previousNewChild = fiber;
  };

  for (size_t index = 0; index < desiredChildren.size(); ++index) {
    const Value& childValue = desiredChildren[index];
    std::shared_ptr<FiberNode> nextFiber;

    if (childValue.isString() || childValue.isNumber()) {
      std::string text = valueToString(rt, childValue);
      Value textValue(rt, String::createFromUtf8(rt, text));

      std::shared_ptr<FiberNode> match;
      if (!unkeyedText.empty()) {
        match = unkeyedText.front();
        unkeyedText.erase(unkeyedText.begin());
      }

      if (match) {
        auto cloned = cloneFiberForReuse(runtime, rt, match, textValue, Value::undefined());
        cloned->tag = WorkTag::HostText;
        cloned->key = Value::undefined();
        cloned->pendingProps = cloneValue(rt, textValue);
        cloned->memoizedProps = cloneValue(rt, textValue);
        cloned->type = Value::undefined();
        cloned->elementType = Value::undefined();
        cloned->child = nullptr;
        cloned->sibling = nullptr;
        if (match->index != static_cast<uint32_t>(index)) {
          cloned->flags = cloned->flags | Placement;
        }
        nextFiber = cloned;
      } else {
        auto fiber = std::make_shared<FiberNode>(WorkTag::HostText, cloneValue(rt, textValue), Value::undefined());
        fiber->memoizedProps = cloneValue(rt, textValue);
  fiber->flags = Placement;
        nextFiber = fiber;
      }
    } else if (childValue.isObject()) {
      Object element = childValue.getObject(rt);
      if (!element.hasProperty(rt, "type")) {
        continue;
      }

      ElementExtraction extraction = extractElement(rt, element);
      if (extraction.type.empty()) {
        continue;
      }

      std::shared_ptr<FiberNode> match;
      if (!extraction.key.empty()) {
        auto it = keyedExisting.find(extraction.key);
        if (it != keyedExisting.end()) {
          match = it->second;
          keyedExisting.erase(it);
        }
      }

      if (!match) {
        auto it = std::find_if(
            unkeyedComponents.begin(),
            unkeyedComponents.end(),
            [&](const std::shared_ptr<FiberNode>& candidate) {
              if (!candidate || candidate->tag != WorkTag::HostComponent) {
                return false;
              }
              if (!candidate->type.isString()) {
                return false;
              }
              return candidate->type.asString(rt).utf8(rt) == extraction.type;
            });
        if (it != unkeyedComponents.end()) {
          match = *it;
          unkeyedComponents.erase(it);
        }
      }

      Value propsValue(rt, extraction.props);
      Value keyValue = makeKeyValue(rt, extraction.key);

      if (match) {
        auto cloned = cloneFiberForReuse(runtime, rt, match, propsValue, Value::undefined());
        cloned->key = cloneValue(rt, keyValue);
        cloned->pendingProps = cloneValue(rt, propsValue);
        cloned->memoizedProps = cloneValue(rt, propsValue);
        cloned->type = Value(rt, String::createFromUtf8(rt, extraction.type));
        cloned->elementType = cloneValue(rt, cloned->type);
        cloned->child = match->child;
        cloned->sibling = nullptr;
        if (match->index != static_cast<uint32_t>(index)) {
          cloned->flags = cloned->flags | Placement;
        }
        nextFiber = cloned;
      } else {
        auto fiber = std::make_shared<FiberNode>(
            WorkTag::HostComponent,
            cloneValue(rt, propsValue),
            cloneValue(rt, keyValue));
        fiber->memoizedProps = cloneValue(rt, propsValue);
        fiber->type = Value(rt, String::createFromUtf8(rt, extraction.type));
        fiber->elementType = cloneValue(rt, fiber->type);
  fiber->flags = Placement;
        nextFiber = fiber;
      }
    } else {
      continue;
    }

    if (!nextFiber) {
      continue;
    }

  nextFiber->subtreeFlags = NoFlags;
    nextFiber->deletions.clear();
    nextFiber->sibling = nullptr;
    appendChild(nextFiber, static_cast<uint32_t>(index));
  }

  if (previousNewChild) {
    previousNewChild->sibling = nullptr;
  }
  parent->child = firstNewChild;

  std::vector<std::shared_ptr<FiberNode>> leftovers;
  leftovers.reserve(
      keyedExisting.size() + unkeyedComponents.size() + unkeyedText.size());
  for (auto& entry : keyedExisting) {
    leftovers.push_back(entry.second);
  }
  leftovers.insert(leftovers.end(), unkeyedComponents.begin(), unkeyedComponents.end());
  leftovers.insert(leftovers.end(), unkeyedText.begin(), unkeyedText.end());

  if (!leftovers.empty()) {
  parent->flags = parent->flags | ChildDeletion;
    for (auto& fiber : leftovers) {
      if (fiber) {
        parent->deletions.push_back(fiber);
      }
    }
  }
}

} // namespace react::ReactRuntimeTestHelper
