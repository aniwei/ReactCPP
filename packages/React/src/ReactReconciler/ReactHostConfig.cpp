#include "ReactReconciler/ReactHostConfig.h"

#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactDOM/client/ReactDOMDiffProperties.h"

#include <cmath>
#include <sstream>
#include <vector>

namespace react::hostconfig {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

std::shared_ptr<ReactDOMComponent> asComponent(const HostInstance& instance) {
  return std::dynamic_pointer_cast<ReactDOMComponent>(instance);
}

std::string numberToString(double value) {
  if (std::isnan(value) || !std::isfinite(value)) {
    return std::string{};
  }

  if (std::floor(value) == value) {
    std::ostringstream oss;
    oss << static_cast<long long>(value);
    return oss.str();
  }

  std::ostringstream oss;
  oss << value;
  return oss.str();
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

Object ensureObject(Runtime& rt, const Value& value) {
  if (value.isObject()) {
    return value.getObject(rt);
  }
  return Object(rt);
}

} // namespace

HostInstance createInstance(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const std::string& type,
    const Object& props) {
  return runtime.createInstance(jsRuntime, type, props);
}

HostTextInstance createTextInstance(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const std::string& text) {
  return runtime.createTextInstance(jsRuntime, text);
}

void appendInitialChild(
    ReactRuntime& runtime,
    const HostInstance& parent,
    const HostInstance& child) {
  if (!parent || !child) {
    return;
  }
  runtime.appendChild(parent, child);
}

void appendChild(
    ReactRuntime& runtime,
    const HostInstance& parent,
    const HostInstance& child) {
  appendInitialChild(runtime, parent, child);
}

void appendChildToContainer(
    ReactRuntime& runtime,
    const HostContainer& container,
    const HostInstance& child) {
  if (!container || !child) {
    return;
  }
  runtime.appendChild(container, child);
}

void insertBefore(
    ReactRuntime& runtime,
    const HostInstance& parent,
    const HostInstance& child,
    const HostInstance& beforeChild) {
  if (!parent || !child) {
    return;
  }
  runtime.insertBefore(parent, child, beforeChild);
}

void insertInContainerBefore(
    ReactRuntime& runtime,
    const HostContainer& container,
    const HostInstance& child,
    const HostInstance& beforeChild) {
  if (!container || !child) {
    return;
  }
  runtime.insertBefore(container, child, beforeChild);
}

void removeChild(
    ReactRuntime& runtime,
    const HostInstance& parent,
    const HostInstance& child) {
  if (!parent || !child) {
    return;
  }
  runtime.removeChild(parent, child);
}

void removeChildFromContainer(
    ReactRuntime& runtime,
    const HostContainer& container,
    const HostInstance& child) {
  if (!container || !child) {
    return;
  }
  runtime.removeChild(container, child);
}

bool finalizeInitialChildren(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const HostInstance& instance,
    const std::string& type,
    const Object& props) {
  auto component = asComponent(instance);
  if (!component) {
    return false;
  }

  component->setProps(jsRuntime, props);

  if (shouldSetTextContent(jsRuntime, type, props)) {
    auto childrenValue = props.getProperty(jsRuntime, "children");
    component->setTextContent(valueToString(jsRuntime, childrenValue));
  }

  return false;
}

bool shouldSetTextContent(
    Runtime& jsRuntime,
    const std::string& /*type*/,
    const Object& props) {
  if (props.hasProperty(jsRuntime, "dangerouslySetInnerHTML")) {
    return true;
  }

  if (!props.hasProperty(jsRuntime, "children")) {
    return false;
  }

  auto value = props.getProperty(jsRuntime, "children");
  return value.isString() || value.isNumber();
}

UpdatePayload prepareUpdate(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const Value& prevProps,
    const Value& nextProps,
    bool isTextNode) {
  (void)runtime;
  if (isTextNode) {
    std::string previousText = valueToString(jsRuntime, prevProps);
    std::string nextText = valueToString(jsRuntime, nextProps);

    if (previousText == nextText) {
      return Value::undefined();
    }

    Object payload(jsRuntime);
    payload.setProperty(jsRuntime, "text", String::createFromUtf8(jsRuntime, nextText));
    return Value(jsRuntime, payload);
  }

  Object prevObj = ensureObject(jsRuntime, prevProps);
  Object nextObj = ensureObject(jsRuntime, nextProps);

  Object payload = diffHostProperties(jsRuntime, prevObj, nextObj);
  bool hasChanges = payload.hasProperty(jsRuntime, "attributes");

  auto prevNames = prevObj.getPropertyNames(jsRuntime);
  const size_t prevLength = prevNames.size(jsRuntime);
  std::vector<std::string> removed;
  removed.reserve(prevLength);

  for (size_t index = 0; index < prevLength; ++index) {
    auto nameValue = prevNames.getValueAtIndex(jsRuntime, index);
    if (!nameValue.isString()) {
      continue;
    }
    const auto name = nameValue.getString(jsRuntime).utf8(jsRuntime);
    if (!nextObj.hasProperty(jsRuntime, name.c_str())) {
      removed.push_back(name);
    }
  }

  if (!removed.empty()) {
    facebook::jsi::Array removedArray(jsRuntime, removed.size());
    for (size_t i = 0; i < removed.size(); ++i) {
      removedArray.setValueAtIndex(jsRuntime, i, String::createFromUtf8(jsRuntime, removed[i]));
    }
    payload.setProperty(jsRuntime, "removedAttributes", removedArray);
    hasChanges = true;
  }

  if (!hasChanges) {
    return Value::undefined();
  }

  return Value(jsRuntime, payload);
}

void commitUpdate(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const HostInstance& instance,
    const Value& prevProps,
    const Value& nextProps,
    const UpdatePayload& payload) {
  if (!instance || payload.isUndefined() || !payload.isObject()) {
    return;
  }

  Object prev = ensureObject(jsRuntime, prevProps);
  Object next = ensureObject(jsRuntime, nextProps);
  Object payloadObject = payload.getObject(jsRuntime);
  runtime.commitUpdate(instance, prev, next, payloadObject);
}

void commitTextUpdate(
    ReactRuntime& runtime,
    const HostTextInstance& textInstance,
    const std::string& oldText,
    const std::string& newText) {
  if (!textInstance) {
    return;
  }
  runtime.commitTextUpdate(textInstance, oldText, newText);
}

void resetAfterCommit(ReactRuntime& /*runtime*/) {
  // No-op for the test host environment.
}

} // namespace react::hostconfig
