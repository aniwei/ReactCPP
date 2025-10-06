#include "ReactDOM/client/ReactDOMComponent.h"

namespace react {

namespace {

facebook::jsi::Value cloneValue(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& value) {
  return facebook::jsi::Value(runtime, value);
}

} // namespace

ReactDOMComponent::ReactDOMComponent(
    std::string type,
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Object& props,
    bool isTextInstance,
    std::string textContent)
    : type_(std::move(type)),
      isTextInstance_(isTextInstance),
      textContent_(std::move(textContent)) {
  setProps(runtime, props);
}

bool ReactDOMComponent::isTextInstance() const {
  return isTextInstance_;
}

const std::string& ReactDOMComponent::getType() const noexcept {
  return type_;
}

const std::unordered_map<std::string, facebook::jsi::Value>& ReactDOMComponent::getProps() const noexcept {
  return props_;
}

const std::string& ReactDOMComponent::getTextContent() const noexcept {
  return textContent_;
}

void ReactDOMComponent::setProps(facebook::jsi::Runtime& runtime, const facebook::jsi::Object& props) {
  rebuildPropsMap(runtime, props);
}

void ReactDOMComponent::setTextContent(std::string text) {
  textContent_ = std::move(text);
  isTextInstance_ = true;
}

std::string ReactDOMComponent::debugDescription() const {
  if (isTextInstance_) {
    return "#text{" + textContent_ + "}";
  }
  return "<" + type_ + ">";
}

void ReactDOMComponent::rebuildPropsMap(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Object& props) {
  props_.clear();

  auto names = props.getPropertyNames(runtime);
  const size_t length = names.size(runtime);
  for (size_t index = 0; index < length; ++index) {
    auto nameValue = names.getValueAtIndex(runtime, index);
    if (!nameValue.isString()) {
      continue;
    }
    const auto name = nameValue.getString(runtime).utf8(runtime);
    auto value = props.getProperty(runtime, name.c_str());
    props_.emplace(name, cloneValue(runtime, value));
  }
}

} // namespace react
