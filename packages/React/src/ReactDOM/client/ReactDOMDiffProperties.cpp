#include "ReactDOM/client/ReactDOMDiffProperties.h"

namespace react {

namespace {

facebook::jsi::Value cloneValue(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& value) {
  return facebook::jsi::Value(runtime, value);
}

} // namespace

facebook::jsi::Object diffHostProperties(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Object& prevProps,
    const facebook::jsi::Object& nextProps) {
  facebook::jsi::Object payload(runtime);
  facebook::jsi::Object attributes(runtime);
  bool hasChanges = false;

  auto valuesEqual = [&runtime](const facebook::jsi::Value& a, const facebook::jsi::Value& b) {
    if (a.isUndefined() && b.isUndefined()) {
      return true;
    }
    if (a.isNull() && b.isNull()) {
      return true;
    }
    if (a.isBool() && b.isBool()) {
      return a.getBool() == b.getBool();
    }
    if (a.isNumber() && b.isNumber()) {
      return a.getNumber() == b.getNumber();
    }
    if (a.isString() && b.isString()) {
      return a.getString(runtime).utf8(runtime) == b.getString(runtime).utf8(runtime);
    }
    return false;
  };

  auto nextNames = nextProps.getPropertyNames(runtime);
  const size_t nextLength = nextNames.size(runtime);
  for (size_t index = 0; index < nextLength; ++index) {
    auto nameValue = nextNames.getValueAtIndex(runtime, index);
    if (!nameValue.isString()) {
      continue;
    }
    const auto name = nameValue.getString(runtime).utf8(runtime);
    auto nextValue = nextProps.getProperty(runtime, name.c_str());
    auto prevValue = prevProps.getProperty(runtime, name.c_str());
    if (!valuesEqual(prevValue, nextValue)) {
      attributes.setProperty(runtime, name.c_str(), cloneValue(runtime, nextValue));
      hasChanges = true;
    }
  }

  if (hasChanges) {
    payload.setProperty(runtime, "attributes", attributes);
  }

  return payload;
}

} // namespace react
