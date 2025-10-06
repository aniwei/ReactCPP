#include "ReactRuntime/ReactHostInterface.h"

#include <algorithm>

namespace react {

namespace {

facebook::jsi::Value cloneValue(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& value) {
  return facebook::jsi::Value(runtime, value);
}

facebook::jsi::Object cloneObject(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Object& object) {
  facebook::jsi::Object clone(runtime);
  auto names = object.getPropertyNames(runtime);
  const size_t length = names.size(runtime);
  for (size_t index = 0; index < length; ++index) {
    auto nameValue = names.getValueAtIndex(runtime, index);
    if (!nameValue.isString()) {
      continue;
    }
    const auto name = nameValue.getString(runtime).utf8(runtime);
    clone.setProperty(runtime, name.c_str(), cloneValue(runtime, object.getProperty(runtime, name.c_str())));
  }
  return clone;
}

std::shared_ptr<ReactDOMComponent> asComponent(const std::shared_ptr<ReactDOMInstance>& instance) {
  return std::dynamic_pointer_cast<ReactDOMComponent>(instance);
}

} // namespace

std::shared_ptr<ReactDOMInstance> HostInterface::createHostInstance(
    facebook::jsi::Runtime& runtime,
    const std::string& type,
    const facebook::jsi::Object& props) {
  auto component = std::make_shared<ReactDOMComponent>(type, runtime, cloneObject(runtime, props));
  return component;
}

std::shared_ptr<ReactDOMInstance> HostInterface::createHostTextInstance(
    facebook::jsi::Runtime& runtime,
    const std::string& text) {
  facebook::jsi::Object emptyProps(runtime);
  auto component = std::make_shared<ReactDOMComponent>("#text", runtime, emptyProps, true, text);
  component->setTextContent(text);
  return component;
}

void HostInterface::detachFromParent(const std::shared_ptr<ReactDOMInstance>& child) {
  if (!child) {
    return;
  }
  auto currentParent = child->getParent();
  if (!currentParent) {
    return;
  }
  auto parentComponent = asComponent(currentParent);
  if (!parentComponent) {
    return;
  }
  auto& siblings = parentComponent->children;
  siblings.erase(
      std::remove_if(
          siblings.begin(),
          siblings.end(),
          [&](const std::shared_ptr<ReactDOMInstance>& candidate) {
            return candidate.get() == child.get();
          }),
      siblings.end());
  child->clearParent();
}

void HostInterface::appendHostChild(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child) {
  auto parentComponent = asComponent(parent);
  auto childComponent = asComponent(child);
  if (!parentComponent || parentComponent->isTextInstance() || !childComponent) {
    return;
  }

  detachFromParent(child);
  parentComponent->children.push_back(child);
  child->setParent(parent);
}

void HostInterface::insertHostChildBefore(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child,
    std::shared_ptr<ReactDOMInstance> beforeChild) {
  auto parentComponent = asComponent(parent);
  auto childComponent = asComponent(child);
  if (!parentComponent || parentComponent->isTextInstance() || !childComponent) {
    return;
  }

  detachFromParent(child);

  if (!beforeChild) {
    parentComponent->children.push_back(child);
    child->setParent(parent);
    return;
  }

  auto& siblings = parentComponent->children;
  auto it = std::find_if(
      siblings.begin(),
      siblings.end(),
      [&](const std::shared_ptr<ReactDOMInstance>& candidate) {
        return candidate.get() == beforeChild.get();
      });

  if (it == siblings.end()) {
    siblings.push_back(child);
  } else {
    siblings.insert(it, child);
  }
  child->setParent(parent);
}

void HostInterface::removeHostChild(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child) {
  auto parentComponent = asComponent(parent);
  auto childComponent = asComponent(child);
  if (!parentComponent || !childComponent) {
    return;
  }
  auto& siblings = parentComponent->children;
  siblings.erase(
      std::remove_if(
          siblings.begin(),
          siblings.end(),
          [&](const std::shared_ptr<ReactDOMInstance>& candidate) {
            return candidate.get() == child.get();
          }),
      siblings.end());
  child->clearParent();
}

void HostInterface::commitHostUpdate(
    facebook::jsi::Runtime& runtime,
    std::shared_ptr<ReactDOMInstance> instance,
    const facebook::jsi::Object& /*oldProps*/,
    const facebook::jsi::Object& newProps,
    const facebook::jsi::Object& /*payload*/) {
  auto component = asComponent(instance);
  if (!component || component->isTextInstance()) {
    return;
  }
  component->setProps(runtime, cloneObject(runtime, newProps));
}

void HostInterface::commitHostTextUpdate(
    std::shared_ptr<ReactDOMInstance> instance,
    const std::string& /*oldText*/,
    const std::string& newText) {
  auto component = asComponent(instance);
  if (!component) {
    return;
  }
  component->setTextContent(newText);
}

} // namespace react
