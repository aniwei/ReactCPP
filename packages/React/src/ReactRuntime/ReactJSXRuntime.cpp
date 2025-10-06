#include "ReactRuntime/ReactJSXRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace react::jsx {

namespace {

class ReactElementHostObject : public jsi::HostObject {
 public:
  explicit ReactElementHostObject(ReactElementPtr element)
      : element_(std::move(element)) {}

  ReactElementPtr element() const {
    return element_;
  }

  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime&) override {
    return {};
  }

  jsi::Value get(jsi::Runtime&, const jsi::PropNameID&) override {
    return jsi::Value::undefined();
  }

 private:
  ReactElementPtr element_;
};

jsi::Value cloneValue(jsi::Runtime& runtime, const jsi::Value& value) {
  return jsi::Value(runtime, value);
}

std::optional<jsi::Value> cloneOptionalValue(jsi::Runtime& runtime, const std::optional<jsi::Value>& value) {
  if (!value) {
    return std::nullopt;
  }
  return std::optional<jsi::Value>(cloneValue(runtime, *value));
}

ReactElementPtr hostValueToElement(jsi::Runtime& runtime, const jsi::Value& value) {
  if (!value.isObject()) {
    return nullptr;
  }
  auto object = value.getObject(runtime);
  if (!object.isHostObject(runtime)) {
    return nullptr;
  }
  auto host = runtime.getHostObject(object);
  if (!host) {
    return nullptr;
  }
  auto typed = std::dynamic_pointer_cast<ReactElementHostObject>(host);
  if (!typed) {
    return nullptr;
  }
  return typed->element();
}

std::string numberToString(double value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("Cannot convert non-finite number to string");
  }
  std::ostringstream out;
  out.setf(std::ios::fmtflags(0), std::ios::floatfield);
  out << value;
  return out.str();
}

std::string coerceToString(jsi::Runtime& runtime, const jsi::Value& value) {
  if (value.isString()) {
    return value.getString(runtime).utf8(runtime);
  }
  if (value.isNumber()) {
    return numberToString(value.getNumber());
  }
  if (value.isBool()) {
    return value.getBool() ? "true" : "false";
  }
  throw std::invalid_argument("Value cannot be converted to string");
}

bool isReservedDevProp(std::string_view name) {
  return name == "__self" || name == "__source";
}

struct NormalizedProps {
  jsi::Object props;
  std::optional<jsi::Value> key;
  std::optional<jsi::Value> ref;
};

NormalizedProps normalizeProps(
    jsi::Runtime& runtime,
    const jsi::Value& rawProps,
    const std::optional<jsi::Value>& providedKey,
    const std::optional<jsi::Value>& providedRef) {
  NormalizedProps result{jsi::Object(runtime), cloneOptionalValue(runtime, providedKey), cloneOptionalValue(runtime, providedRef)};

  jsi::Object sourceProps = rawProps.isObject() ? rawProps.getObject(runtime) : jsi::Object(runtime);
  jsi::Array names = sourceProps.getPropertyNames(runtime);
  const size_t length = names.size(runtime);

  for (size_t index = 0; index < length; ++index) {
    jsi::Value nameValue = names.getValueAtIndex(runtime, index);
    if (!nameValue.isString()) {
      continue;
    }

    const std::string propName = nameValue.getString(runtime).utf8(runtime);
    jsi::Value propValue = sourceProps.getProperty(runtime, propName.c_str());

    if (propName == "key") {
      if (!result.key) {
        result.key = cloneValue(runtime, propValue);
      }
      continue;
    }

    if (propName == "ref") {
      if (!result.ref) {
        result.ref = cloneValue(runtime, propValue);
      }
      continue;
    }

    if (isReservedDevProp(propName)) {
      continue;
    }

    result.props.setProperty(runtime, propName.c_str(), cloneValue(runtime, propValue));
  }

  return result;
}

ReactElementPtr createElement(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    jsi::Object props,
    std::optional<jsi::Value> key,
    std::optional<jsi::Value> ref,
    std::optional<SourceLocation> source,
    bool hasStaticChildren) {
  auto element = std::make_shared<ReactElement>();
  element->type = cloneValue(runtime, type);
  element->props = jsi::Value(runtime, props);
  element->key = std::move(key);
  element->ref = std::move(ref);
  element->source = std::move(source);
  element->hasStaticChildren = hasStaticChildren;
  return element;
}

struct WasmMemoryBuilder {
  WasmMemoryBuilder() {
    buffer.push_back(0);
  }

  template <typename T>
  uint32_t appendStruct(const T& value) {
    const auto offset = static_cast<uint32_t>(buffer.size());
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
    return offset;
  }

  uint32_t appendValues(const std::vector<WasmReactValue>& values) {
    if (values.empty()) {
      return 0;
    }
    uint32_t firstOffset = 0;
    for (size_t index = 0; index < values.size(); ++index) {
      const auto offset = appendStruct(values[index]);
      if (index == 0) {
        firstOffset = offset;
      }
    }
    return firstOffset;
  }

  uint32_t appendProps(const std::vector<WasmReactProp>& props) {
    if (props.empty()) {
      return 0;
    }
    uint32_t firstOffset = 0;
    for (size_t index = 0; index < props.size(); ++index) {
      const auto offset = appendStruct(props[index]);
      if (index == 0) {
        firstOffset = offset;
      }
    }
    return firstOffset;
  }

  uint32_t internString(const std::string& value) {
    const auto it = stringOffsets.find(value);
    if (it != stringOffsets.end()) {
      return it->second;
    }
    const auto offset = static_cast<uint32_t>(buffer.size());
    buffer.insert(buffer.end(), value.begin(), value.end());
    buffer.push_back('\0');
    stringOffsets.emplace(value, offset);
    return offset;
  }

  std::vector<uint8_t> takeBuffer() {
    return std::move(buffer);
  }

  std::vector<uint8_t> buffer;
  std::unordered_map<std::string, uint32_t> stringOffsets;
};

void collectChildrenRecursive(jsi::Runtime& runtime, const jsi::Value& value, std::vector<jsi::Value>& out) {
  if (value.isUndefined() || value.isNull()) {
    return;
  }

  if (value.isBool()) {
    return;
  }

  if (value.isNumber() || value.isString()) {
    out.emplace_back(cloneValue(runtime, value));
    return;
  }

  if (!value.isObject()) {
    throw std::invalid_argument("Unsupported child node in JSX runtime");
  }

  if (hostValueToElement(runtime, value)) {
    out.emplace_back(cloneValue(runtime, value));
    return;
  }

  auto object = value.getObject(runtime);
  if (!object.isArray(runtime)) {
    throw std::invalid_argument("Unsupported child node in JSX runtime");
  }

  const size_t length = object.size(runtime);
  for (size_t index = 0; index < length; ++index) {
    collectChildrenRecursive(runtime, object.getValueAtIndex(runtime, index), out);
  }
}

std::vector<jsi::Value> collectChildren(jsi::Runtime& runtime, const jsi::Value& value) {
  std::vector<jsi::Value> result;
  collectChildrenRecursive(runtime, value, result);
  return result;
}

WasmReactValue encodeValue(jsi::Runtime& runtime, const jsi::Value& value, WasmMemoryBuilder& builder);
uint32_t encodeElement(jsi::Runtime& runtime, const ReactElement& element, WasmMemoryBuilder& builder);

WasmReactValue encodePropScalar(jsi::Runtime& runtime, const jsi::Value& value, WasmMemoryBuilder& builder) {
  WasmReactValue encoded{};
  if (value.isString()) {
    encoded.type = WasmValueType::String;
    encoded.data.ptrValue = builder.internString(value.getString(runtime).utf8(runtime));
    return encoded;
  }
  if (value.isNumber()) {
    encoded.type = WasmValueType::Number;
    encoded.data.numberValue = value.getNumber();
    return encoded;
  }
  if (value.isBool()) {
    encoded.type = WasmValueType::Boolean;
    encoded.data.boolValue = value.getBool();
    return encoded;
  }
  throw std::invalid_argument("Unsupported prop value while encoding");
}

std::vector<WasmReactProp> encodeProps(
    jsi::Runtime& runtime,
    const ReactElement& element,
    WasmMemoryBuilder& builder,
    std::vector<jsi::Value>& outChildren) {
  std::vector<WasmReactProp> encoded;

  if (!element.props.isObject()) {
    return encoded;
  }

  auto propsObject = element.props.getObject(runtime);
  auto names = propsObject.getPropertyNames(runtime);
  const size_t length = names.size(runtime);

  for (size_t index = 0; index < length; ++index) {
    jsi::Value nameValue = names.getValueAtIndex(runtime, index);
    if (!nameValue.isString()) {
      continue;
    }
    const std::string propName = nameValue.getString(runtime).utf8(runtime);
    jsi::Value propValue = propsObject.getProperty(runtime, propName.c_str());

    if (propName == "children") {
      collectChildrenRecursive(runtime, propValue, outChildren);
      continue;
    }

    if (propValue.isNull() || propValue.isUndefined()) {
      continue;
    }

    WasmReactProp prop{};
    prop.key_ptr = builder.internString(propName);
    prop.value = encodePropScalar(runtime, propValue, builder);
    encoded.push_back(prop);
  }

  return encoded;
}

WasmReactValue encodeArray(jsi::Runtime& runtime, const jsi::Object& array, WasmMemoryBuilder& builder) {
  const size_t length = array.size(runtime);
  std::vector<WasmReactValue> items;
  items.reserve(length);
  for (size_t index = 0; index < length; ++index) {
    items.push_back(encodeValue(runtime, array.getValueAtIndex(runtime, index), builder));
  }
  WasmReactArray encodedArray{};
  encodedArray.length = static_cast<uint32_t>(items.size());
  encodedArray.items_ptr = builder.appendValues(items);

  WasmReactValue encoded{};
  encoded.type = WasmValueType::Array;
  encoded.data.ptrValue = builder.appendStruct(encodedArray);
  return encoded;
}

WasmReactValue encodeValue(jsi::Runtime& runtime, const jsi::Value& value, WasmMemoryBuilder& builder) {
  WasmReactValue encoded{};
  if (value.isNull()) {
    encoded.type = WasmValueType::Null;
    encoded.data.ptrValue = 0;
    return encoded;
  }
  if (value.isUndefined()) {
    encoded.type = WasmValueType::Undefined;
    encoded.data.ptrValue = 0;
    return encoded;
  }
  if (value.isBool()) {
    encoded.type = WasmValueType::Boolean;
    encoded.data.boolValue = value.getBool();
    return encoded;
  }
  if (value.isNumber()) {
    encoded.type = WasmValueType::Number;
    encoded.data.numberValue = value.getNumber();
    return encoded;
  }
  if (value.isString()) {
    encoded.type = WasmValueType::String;
    encoded.data.ptrValue = builder.internString(value.getString(runtime).utf8(runtime));
    return encoded;
  }
  if (!value.isObject()) {
    throw std::invalid_argument("Unsupported value while encoding");
  }

  if (auto element = hostValueToElement(runtime, value)) {
    encoded.type = WasmValueType::Element;
    encoded.data.ptrValue = encodeElement(runtime, *element, builder);
    return encoded;
  }

  auto object = value.getObject(runtime);
  if (object.isArray(runtime)) {
    return encodeArray(runtime, object, builder);
  }

  throw std::invalid_argument("Unsupported value while encoding");
}

std::optional<std::string> extractKeyString(jsi::Runtime& runtime, const std::optional<jsi::Value>& key) {
  if (!key) {
    return std::nullopt;
  }
  if (key->isUndefined() || key->isNull()) {
    return std::nullopt;
  }
  return coerceToString(runtime, *key);
}

uint32_t encodeElement(jsi::Runtime& runtime, const ReactElement& element, WasmMemoryBuilder& builder) {
  if (!element.type.isString()) {
    throw std::invalid_argument("JSX runtime can only serialize host elements identified by string type");
  }

  const auto typeOffset = builder.internString(element.type.getString(runtime).utf8(runtime));

  uint32_t keyOffset = 0;
  if (auto keyString = extractKeyString(runtime, element.key)) {
    keyOffset = builder.internString(*keyString);
  }

  std::vector<jsi::Value> childValues;
  const auto props = encodeProps(runtime, element, builder, childValues);

  std::vector<WasmReactValue> encodedChildren;
  encodedChildren.reserve(childValues.size());
  for (const auto& child : childValues) {
    encodedChildren.push_back(encodeValue(runtime, child, builder));
  }

  const auto propsOffset = builder.appendProps(props);
  const auto childrenOffset = builder.appendValues(encodedChildren);

  WasmReactElement encoded{};
  encoded.type_name_ptr = typeOffset;
  encoded.key_ptr = keyOffset;
  encoded.ref_ptr = 0;
  encoded.props_count = static_cast<uint32_t>(props.size());
  encoded.props_ptr = propsOffset;
  encoded.children_count = static_cast<uint32_t>(encodedChildren.size());
  encoded.children_ptr = childrenOffset;

  return builder.appendStruct(encoded);
}

} // namespace

ReactElementPtr jsx(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& props,
    std::optional<jsi::Value> key,
    std::optional<jsi::Value> ref) {
  auto normalized = normalizeProps(runtime, props, key, ref);
  return createElement(runtime, type, std::move(normalized.props), std::move(normalized.key), std::move(normalized.ref), std::nullopt, false);
}

ReactElementPtr jsxs(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& props,
    std::optional<jsi::Value> key,
    std::optional<jsi::Value> ref) {
  auto normalized = normalizeProps(runtime, props, key, ref);
  return createElement(runtime, type, std::move(normalized.props), std::move(normalized.key), std::move(normalized.ref), std::nullopt, true);
}

ReactElementPtr jsxDEV(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& config,
    std::optional<jsi::Value> maybeKey,
    SourceLocation source,
    std::optional<jsi::Value> ref) {
  auto normalized = normalizeProps(runtime, config, maybeKey, ref);

  std::optional<SourceLocation> location;
  if (source.isValid()) {
    location = source;
  }

  return createElement(runtime, type, std::move(normalized.props), std::move(normalized.key), std::move(normalized.ref), std::move(location), false);
}

WasmSerializedLayout serializeToWasm(jsi::Runtime& runtime, const ReactElement& element) {
  WasmMemoryBuilder builder;
  const auto rootOffset = encodeElement(runtime, element, builder);
  WasmSerializedLayout layout;
  layout.buffer = builder.takeBuffer();
  layout.rootOffset = rootOffset;
  return layout;
}

jsi::Value createJsxHostValue(jsi::Runtime& runtime, const ReactElementPtr& element) {
  auto host = std::make_shared<ReactElementHostObject>(element);
  return jsi::Value(runtime, jsi::Object::createFromHostObject(runtime, host));
}

ReactElementPtr getReactElementFromValue(jsi::Runtime& runtime, const jsi::Value& value) {
  return hostValueToElement(runtime, value);
}

bool isReactElementValue(jsi::Runtime& runtime, const jsi::Value& value) {
  return hostValueToElement(runtime, value) != nullptr;
}

} // namespace react::jsx
