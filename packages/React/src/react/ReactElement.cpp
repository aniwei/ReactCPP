#include "ReactElement.h"

#include <functional>
#include <vector>

namespace react {

jsi::Value ReactElement::get(jsi::Runtime& runtime, const jsi::PropNameID& name) {
  const std::string prop = name.utf8(runtime);
  if (prop == "$$typeof") {
    return jsi::Value(static_cast<double>(typeSymbol));
  }
  if (prop == "type") {
    return jsi::Value(runtime, type);
  }
  if (prop == "key") {
    return jsi::Value(runtime, key);
  }
  if (prop == "ref") {
    return jsi::Value(runtime, ref);
  }
  if (prop == "props") {
    return jsi::Value(runtime, props);
  }
  if (prop == "_owner") {
    return jsi::Value(runtime, _owner);
  }

  return jsi::Value::undefined();
}

std::vector<jsi::PropNameID> ReactElement::getPropertyNames(jsi::Runtime& runtime) {
  std::vector<jsi::PropNameID> props;
  props.reserve(6);
  props.push_back(jsi::PropNameID::forUtf8(runtime, "$$typeof"));
  props.push_back(jsi::PropNameID::forUtf8(runtime, "type"));
  props.push_back(jsi::PropNameID::forUtf8(runtime, "key"));
  props.push_back(jsi::PropNameID::forUtf8(runtime, "ref"));
  props.push_back(jsi::PropNameID::forUtf8(runtime, "props"));
  props.push_back(jsi::PropNameID::forUtf8(runtime, "_owner"));
  return props;
}

ReactElement createElement(
  facebook::jsi::Runtime& runtime,
  const jsi::Value& type,
  const jsi::Value& props,
  const jsi::Value& key,
  const jsi::Value& ref
) {
  return ReactElement(
    runtime, 
    type, 
    key, 
    ref, 
    props);
}

bool isValidElement(jsi::Runtime& runtime, const jsi::Value& value) {
  if (!value.isObject()) {
    return false;
  }

  // 1) HostObject 路径：如果是 ReactElement HostObject，直接认为有效。
  try {
    auto hostObj = value.asObject(runtime).getHostObject(runtime);
    if (std::dynamic_pointer_cast<ReactElement>(hostObj) != nullptr) {
      return true;
    }
  } catch (...) {
    // ignore
  }

  // 2) 兼容路径：检查 $$typeof 标记（当前实现用 uint32 模拟 Symbol）。
  try {
    auto obj = value.asObject(runtime);
    if (!obj.hasProperty(runtime, "$$typeof")) {
      return false;
    }
    auto marker = obj.getProperty(runtime, "$$typeof");
    if (!marker.isNumber()) {
      return false;
    }
    const auto markerValue = static_cast<shared::ReactSymbol>(marker.getNumber());
    return markerValue == shared::REACT_ELEMENT_TYPE;
  } catch (...) {
    return false;
  }
}

std::string getElementTypeString(const ReactElement& element) {
  // 注意：jsi::Value 取字符串需要 Runtime，这里仅做粗分类
  if (element.type.isString()) {
    return "[HostComponent]";
  }
  return "[Component]";
}

ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  const ContainerInfo& containerInfo,
  const jsi::Value& key
) {
  ReactPortal portal;
  portal.containerInfo = containerInfo;
  portal.children = jsi::Value(rt, children);
  portal.key = jsi::Value(rt, key);
  portal.implementation = jsi::Value::undefined();
  return portal;
}

ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  ReactDOMContainer* container,
  const jsi::Value& key
) {
  return createPortal(rt, children, ContainerInfo(container), key);
}

ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  const std::string& debugName,
  const jsi::Value& key
) {
  return createPortal(rt, children, ContainerInfo(debugName), key);
}

} // namespace react
