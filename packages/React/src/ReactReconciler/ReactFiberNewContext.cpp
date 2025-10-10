#include "ReactReconciler/ReactFiberNewContext.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactWorkTags.h"

#include <memory>
#include <stdexcept>
#include <vector>
#include <cmath>

namespace react {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

constexpr const char* kCurrentValueProp = "_currentValue";
constexpr const char* kCurrentValue2Prop = "_currentValue2";
constexpr const char* kValueProp = "value";

struct ContextDependencyNode {
  std::shared_ptr<Value> context;
  std::shared_ptr<Value> memoizedValue;
  Runtime* runtime{nullptr};
  ContextDependencyNode* next{nullptr};
};

struct ContextDependencyList {
  ContextDependencyNode* head{nullptr};
};

struct ProviderStackEntry {
  std::shared_ptr<Value> context;
  std::shared_ptr<Value> previousValue;
  Runtime* runtime{nullptr};
  ProviderStackEntry* next{nullptr};
};

struct ContextHandle {
  std::shared_ptr<Value> context;
  Runtime* runtime{nullptr};
};

FiberNode* gCurrentlyRenderingFiber = nullptr;
ContextDependencyNode* gLastContextDependency = nullptr;
ProviderStackEntry* gProviderStackTop = nullptr;
#if !defined(NDEBUG)
bool gIsDisallowedContextReadInDEV = false;
#endif

constexpr bool kIsPrimaryRenderer = true;

std::shared_ptr<Value> makeValueHandle(Runtime& runtime, const Value& source) {
  return std::shared_ptr<Value>(new Value(runtime, source), [](Value* value) {
    delete value;
  });
}

const char* currentValuePropertyName() {
  return kIsPrimaryRenderer ? kCurrentValueProp : kCurrentValue2Prop;
}

ContextDependencyList* getContextDependencyList(const FiberNode& fiber) {
  if (!fiber.dependencies) {
    return nullptr;
  }
  return static_cast<ContextDependencyList*>(fiber.dependencies->firstContext);
}

ContextHandle makeContextHandle(Runtime& runtime, const Value& contextValue) {
  ContextHandle handle;
  handle.context = makeValueHandle(runtime, contextValue);
  handle.runtime = &runtime;
  return handle;
}

Value cloneValue(Runtime& runtime, const Value& value) {
  return Value(runtime, value);
}

bool objectIs(Runtime& runtime, const Value& a, const Value& b) {
  if (a.isNumber() && b.isNumber()) {
    const double x = a.getNumber();
    const double y = b.getNumber();
    if (std::isnan(x) && std::isnan(y)) {
      return true;
    }
    if (x == 0.0 && y == 0.0) {
      const bool xIsNegativeZero = std::signbit(x);
      const bool yIsNegativeZero = std::signbit(y);
      return xIsNegativeZero == yIsNegativeZero;
    }
    return x == y;
  }

  if (a.isUndefined() && b.isUndefined()) {
    return true;
  }
  if (a.isNull() && b.isNull()) {
    return true;
  }
  if (a.isBool() && b.isBool()) {
    return a.getBool() == b.getBool();
  }
  if (a.isString() && b.isString()) {
    return Value::strictEquals(runtime, a, b);
  }
  if (a.isSymbol() && b.isSymbol()) {
    return Value::strictEquals(runtime, a, b);
  }
  if (a.isObject() && b.isObject()) {
    return Value::strictEquals(runtime, a, b);
  }

  return Value::strictEquals(runtime, a, b);
}

bool contextsMatch(const ContextDependencyNode& dependency, const ContextHandle& handle) {
  if (dependency.runtime == nullptr || handle.runtime == nullptr) {
    return false;
  }
  if (dependency.runtime != handle.runtime) {
    return false;
  }
  if (!dependency.context || !handle.context) {
    return false;
  }
  return Value::strictEquals(*dependency.runtime, *dependency.context, *handle.context);
}

bool dependencyMatchesAnyContext(
    const ContextDependencyNode& dependency,
    const std::vector<ContextHandle>& contexts) {
  for (const auto& handle : contexts) {
    if (contextsMatch(dependency, handle)) {
      return true;
    }
  }
  return false;
}

Value getStoredValue(Runtime& runtime, const void* slot) {
  if (slot == nullptr) {
    return Value::undefined();
  }
  const auto* stored = static_cast<const Value*>(slot);
  return Value(runtime, *stored);
}

Value getObjectProperty(Runtime& runtime, const Value& objectValue, const char* propertyName) {
  if (!objectValue.isObject()) {
    return Value::undefined();
  }
  Object object = objectValue.getObject(runtime);
  if (!object.hasProperty(runtime, propertyName)) {
    return Value::undefined();
  }
  return object.getProperty(runtime, propertyName);
}

ContextDependencyList* ensureContextList(FiberNode& fiber) {
  if (!fiber.dependencies) {
    fiber.dependencies = std::make_unique<FiberNode::Dependencies>();
  }

  FiberNode::Dependencies* deps = fiber.dependencies.get();
  if (deps->firstContext == nullptr) {
    deps->firstContext = new ContextDependencyList();
    deps->lanes = NoLanes;
  }

  return static_cast<ContextDependencyList*>(deps->firstContext);
}

void appendContextDependency(
    Runtime& runtime,
    FiberNode& consumer,
    const Value& contextValue,
    const Value& memoizedValue) {
  auto* list = ensureContextList(consumer);

  auto* node = new ContextDependencyNode();
  node->context = makeValueHandle(runtime, contextValue);
  node->memoizedValue = makeValueHandle(runtime, memoizedValue);
  node->runtime = &runtime;
  node->next = nullptr;

  if (gLastContextDependency == nullptr) {
    list->head = node;
    gLastContextDependency = node;
    consumer.flags = static_cast<FiberFlags>(consumer.flags | NeedsPropagation);
  } else {
    gLastContextDependency->next = node;
    gLastContextDependency = node;
  }
}

Value readContextCurrentValue(Runtime& runtime, const Value& contextValue) {
  if (!contextValue.isObject()) {
    throw std::invalid_argument("Context value must be an object");
  }

  Object contextObject = contextValue.getObject(runtime);
  const char* prop = kIsPrimaryRenderer ? kCurrentValueProp : kCurrentValue2Prop;
  if (!contextObject.hasProperty(runtime, prop)) {
    return Value::undefined();
  }
  return contextObject.getProperty(runtime, prop);
}

Value readContextForConsumer(Runtime& runtime, FiberNode* consumer, const Value& contextValue) {
  Value currentValue = readContextCurrentValue(runtime, contextValue);

  if (consumer == nullptr) {
    throw std::logic_error(
        "Context can only be read while React is rendering. "
        "This is a bug in the renderer.");
  }

  appendContextDependency(runtime, *consumer, contextValue, currentValue);

  return Value(runtime, currentValue);
}

} // namespace

void propagateContextChangesImpl(
  FiberNode& workInProgress,
  const std::vector<ContextHandle>& contexts,
  Lanes renderLanes,
  bool forcePropagateEntireTree);

void propagateParentContextChangesImpl(
  facebook::jsi::Runtime& runtime,
  FiberNode& current,
  FiberNode& workInProgress,
  Lanes renderLanes,
  bool forcePropagateEntireTree);

void resetContextDependencies() {
  gCurrentlyRenderingFiber = nullptr;
  gLastContextDependency = nullptr;
#if !defined(NDEBUG)
  gIsDisallowedContextReadInDEV = false;
#endif
}

void enterDisallowedContextReadInDEV() {
#if !defined(NDEBUG)
  gIsDisallowedContextReadInDEV = true;
#endif
}

void exitDisallowedContextReadInDEV() {
#if !defined(NDEBUG)
  gIsDisallowedContextReadInDEV = false;
#endif
}

void pushProvider(
    Runtime& runtime,
    FiberNode& providerFiber,
    const Value& contextValue,
    const Value& nextValue) {
  (void)providerFiber;

  if (!contextValue.isObject()) {
    throw std::invalid_argument("Context provider expects an object value.");
  }

  Value previousValue = readContextCurrentValue(runtime, contextValue);

  auto* entry = new ProviderStackEntry();
  entry->context = makeValueHandle(runtime, contextValue);
  entry->previousValue = makeValueHandle(runtime, previousValue);
  entry->runtime = &runtime;
  entry->next = gProviderStackTop;
  gProviderStackTop = entry;

  Object contextObject = contextValue.getObject(runtime);
  contextObject.setProperty(runtime, currentValuePropertyName(), Value(runtime, nextValue));
}

void popProvider(
    Runtime& runtime,
    FiberNode& providerFiber,
    const Value& contextValue) {
  (void)runtime;
  (void)providerFiber;
  (void)contextValue;

  if (gProviderStackTop == nullptr) {
    return;
  }

  ProviderStackEntry* entry = gProviderStackTop;
  gProviderStackTop = entry->next;

  if (entry->runtime != nullptr && entry->context) {
    Runtime& providerRuntime = *entry->runtime;
    Object contextObject = entry->context->getObject(providerRuntime);
    Value restoredValue = entry->previousValue
        ? Value(providerRuntime, *entry->previousValue)
        : Value::undefined();
    contextObject.setProperty(providerRuntime, currentValuePropertyName(), restoredValue);
  }

  delete entry;
}

void prepareToReadContext(FiberNode& workInProgress, Lanes /*renderLanes*/) {
  gCurrentlyRenderingFiber = &workInProgress;
  gLastContextDependency = nullptr;

  if (workInProgress.dependencies) {
    deleteContextDependencies(workInProgress.dependencies->firstContext);
    workInProgress.dependencies->firstContext = nullptr;
    workInProgress.dependencies->lanes = NoLanes;
  }
}

Value readContextDuringReconciliation(
    Runtime& runtime,
    FiberNode& consumer,
    const Value& contextValue,
    Lanes renderLanes) {
  if (gCurrentlyRenderingFiber == nullptr) {
    prepareToReadContext(consumer, renderLanes);
  }
  return readContextForConsumer(runtime, &consumer, contextValue);
}

Value readContext(
    Runtime& runtime,
    FiberNode& consumer,
    const Value& contextValue) {
  if (gCurrentlyRenderingFiber == nullptr) {
    prepareToReadContext(consumer, consumer.lanes);
  }
  return readContextForConsumer(runtime, &consumer, contextValue);
}

void scheduleContextWorkOnParentPath(FiberNode* parent, Lanes renderLanes, FiberNode& propagationRoot) {
  FiberNode* node = parent;
  while (node != nullptr) {
    FiberNode* alternate = node->alternate;
    if (!isSubsetOfLanes(node->childLanes, renderLanes)) {
      node->childLanes = mergeLanes(node->childLanes, renderLanes);
      if (alternate != nullptr) {
        alternate->childLanes = mergeLanes(alternate->childLanes, renderLanes);
      }
    } else if (
        alternate != nullptr &&
        !isSubsetOfLanes(alternate->childLanes, renderLanes)) {
      alternate->childLanes = mergeLanes(alternate->childLanes, renderLanes);
    }

    if (node == &propagationRoot) {
      break;
    }
    node = node->returnFiber;
  }

#if !defined(NDEBUG)
  if (node != &propagationRoot) {
    throw std::logic_error(
        "Expected to find the propagation root when scheduling context work.");
  }
#endif
}

void propagateContextChange(
    Runtime& runtime,
    FiberNode& workInProgress,
    const Value& contextValue,
    Lanes renderLanes) {
  std::vector<ContextHandle> contexts;
  contexts.reserve(1);
  contexts.push_back(makeContextHandle(runtime, contextValue));
  propagateContextChangesImpl(workInProgress, contexts, renderLanes, true);
}

void lazilyPropagateParentContextChanges(
    Runtime& runtime,
    FiberNode& current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  propagateParentContextChangesImpl(runtime, current, workInProgress, renderLanes, false);
}

void propagateParentContextChangesToDeferredTree(
    Runtime& runtime,
    FiberNode& current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  propagateParentContextChangesImpl(runtime, current, workInProgress, renderLanes, true);
}

bool checkIfContextChanged(const FiberNode::Dependencies& currentDependencies) {
  const auto* list = static_cast<const ContextDependencyList*>(currentDependencies.firstContext);
  if (list == nullptr) {
    return false;
  }

  ContextDependencyNode* dependency = list->head;
  while (dependency != nullptr) {
    if (dependency->runtime != nullptr && dependency->context && dependency->memoizedValue) {
      Runtime& runtime = *dependency->runtime;
      Value currentValue = readContextCurrentValue(runtime, *dependency->context);
      if (!objectIs(runtime, currentValue, *dependency->memoizedValue)) {
        return true;
      }
    }
    dependency = dependency->next;
  }

  return false;
}

void propagateContextChangesImpl(
    FiberNode& workInProgress,
    const std::vector<ContextHandle>& contexts,
    Lanes renderLanes,
    bool forcePropagateEntireTree) {
  if (contexts.empty()) {
    return;
  }

  FiberNode* fiber = workInProgress.child;
  if (fiber != nullptr) {
    fiber->returnFiber = &workInProgress;
  }

  while (fiber != nullptr) {
    FiberNode* nextFiber = nullptr;

    ContextDependencyList* dependencyList = getContextDependencyList(*fiber);
    if (dependencyList != nullptr) {
      nextFiber = fiber->child;
      ContextDependencyNode* dependency = dependencyList->head;
      bool matched = false;
      while (dependency != nullptr) {
        if (dependencyMatchesAnyContext(*dependency, contexts)) {
          matched = true;
          break;
        }
        dependency = dependency->next;
      }

      if (matched) {
        fiber->lanes = mergeLanes(fiber->lanes, renderLanes);
        FiberNode* alternate = fiber->alternate;
        if (alternate != nullptr) {
          alternate->lanes = mergeLanes(alternate->lanes, renderLanes);
        }
        scheduleContextWorkOnParentPath(fiber->returnFiber, renderLanes, workInProgress);

        if (!forcePropagateEntireTree) {
          nextFiber = nullptr;
        }
      }
    } else if (fiber->tag == WorkTag::DehydratedFragment) {
      FiberNode* parentSuspense = fiber->returnFiber;
      if (parentSuspense == nullptr) {
        throw std::logic_error(
            "Encountered a dehydrated fragment without a parent Suspense fiber.");
      }

      parentSuspense->lanes = mergeLanes(parentSuspense->lanes, renderLanes);
      if (parentSuspense->alternate != nullptr) {
        parentSuspense->alternate->lanes = mergeLanes(parentSuspense->alternate->lanes, renderLanes);
      }
      scheduleContextWorkOnParentPath(parentSuspense, renderLanes, workInProgress);
      nextFiber = nullptr;
    } else {
      nextFiber = fiber->child;
    }

    if (nextFiber != nullptr) {
      nextFiber->returnFiber = fiber;
    } else {
      nextFiber = fiber;
      while (nextFiber != nullptr) {
        if (nextFiber == &workInProgress) {
          nextFiber = nullptr;
          break;
        }
        FiberNode* sibling = nextFiber->sibling;
        if (sibling != nullptr) {
          sibling->returnFiber = nextFiber->returnFiber;
          nextFiber = sibling;
          break;
        }
        nextFiber = nextFiber->returnFiber;
      }
    }

    fiber = nextFiber;
  }
}

void propagateParentContextChangesImpl(
    Runtime& runtime,
    FiberNode& current,
    FiberNode& workInProgress,
    Lanes renderLanes,
    bool forcePropagateEntireTree) {
  (void)current;

  std::vector<ContextHandle> contexts;
  FiberNode* parent = &workInProgress;
  bool isInsidePropagationBailout = false;

  while (parent != nullptr) {
    if (!isInsidePropagationBailout) {
      if ((parent->flags & NeedsPropagation) != NoFlags) {
        isInsidePropagationBailout = true;
      } else if ((parent->flags & DidPropagateContext) != NoFlags) {
        break;
      }
    }

    if (parent->tag == WorkTag::ContextProvider) {
      FiberNode* currentParent = parent->alternate;
      if (currentParent == nullptr) {
        throw std::logic_error("Expected a current fiber when propagating context changes.");
      }

      Value oldProps = getStoredValue(runtime, currentParent->memoizedProps);
      if (!oldProps.isUndefined() && !oldProps.isNull()) {
        Value newProps = getStoredValue(runtime, parent->pendingProps);
        Value newValue = getObjectProperty(runtime, newProps, kValueProp);
        Value oldValue = getObjectProperty(runtime, oldProps, kValueProp);

        if (!objectIs(runtime, newValue, oldValue)) {
          Value contextValue = getStoredValue(runtime, parent->type);
          contexts.push_back(makeContextHandle(runtime, contextValue));
        }
      }
    }

    parent = parent->returnFiber;
  }

  if (!contexts.empty()) {
    propagateContextChangesImpl(workInProgress, contexts, renderLanes, forcePropagateEntireTree);
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | DidPropagateContext);
}

void* cloneContextDependencies(void* head) {
  if (head == nullptr) {
    return nullptr;
  }

  auto* source = static_cast<ContextDependencyList*>(head);
  auto* clone = new ContextDependencyList();

  ContextDependencyNode* sourceNode = source->head;
  ContextDependencyNode* previousCloneNode = nullptr;

  while (sourceNode != nullptr) {
    auto* newNode = new ContextDependencyNode();
    newNode->context = sourceNode->context;
    newNode->memoizedValue = sourceNode->memoizedValue;
    newNode->runtime = sourceNode->runtime;
    newNode->next = nullptr;

    if (previousCloneNode == nullptr) {
      clone->head = newNode;
    } else {
      previousCloneNode->next = newNode;
    }

    previousCloneNode = newNode;
    sourceNode = sourceNode->next;
  }

  return clone;
}

void deleteContextDependencies(void* head) {
  if (head == nullptr) {
    return;
  }

  auto* list = static_cast<ContextDependencyList*>(head);
  ContextDependencyNode* node = list->head;
  while (node != nullptr) {
    ContextDependencyNode* next = node->next;
    delete node;
    node = next;
  }

  delete list;
}

} // namespace react
