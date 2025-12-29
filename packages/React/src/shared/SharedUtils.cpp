#include "SharedUtils.h"

namespace react::shared {

template<>
bool objectIs(const double& x, const double& y) {
  if (x == y) {
    return x != 0.0 || (1.0 / x == 1.0 / y);
  }
  
  return std::isnan(x) && std::isnan(y);
}

template<>
bool objectIs(const float& x, const float& y) {
  if (x == y) {
    return x != 0.0f || (1.0f / x == 1.0f / y);
  }
  return std::isnan(x) && std::isnan(y);
}

bool objectIs(jsi::Runtime& runtime, const jsi::Value& x, const jsi::Value& y) {
  // 处理 undefined
  if (x.isUndefined() && y.isUndefined()) {
    return true;
  }

  // 处理 null
  if (x.isNull() && y.isNull()) {
    return true;
  }

  // 类型不同则不相等
  if (x.isUndefined() != y.isUndefined() || x.isNull() != y.isNull() || x.isBool() != y.isBool() ||
      x.isNumber() != y.isNumber() || x.isString() != y.isString() || x.isSymbol() != y.isSymbol() ||
      x.isObject() != y.isObject()) {
    return false;
  }

  // 布尔值
  if (x.isBool()) {
    return x.getBool() == y.getBool();
  }

  // 数字 - 使用特化的 objectIs
  if (x.isNumber()) {
    return objectIs(x.getNumber(), y.getNumber());
  }

  // 字符串
  if (x.isString()) {
    return x.getString(runtime).utf8(runtime) == y.getString(runtime).utf8(runtime);
  }

  // 对象引用比较
  if (x.isObject()) {
    // 注意: 在 JSI 中，Object 的 == 是引用比较
    return jsi::Value::strictEquals(runtime, x, y);
  }

  // Symbol 比较
  if (x.isSymbol()) {
    return jsi::Value::strictEquals(runtime, x, y);
  }

  return false;
}

bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const std::string& key) {
  return obj.hasProperty(runtime, key.c_str());
}

bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const jsi::PropNameID& name) {
  return obj.hasProperty(runtime, name);
}

bool isArray(jsi::Runtime& runtime, const jsi::Value& value) {
  if (!value.isObject()) {
    return false;
  }
  return value.getObject(runtime).isArray(runtime);
}

bool isArray(jsi::Runtime& runtime, const jsi::Object& obj) {
  return obj.isArray(runtime);
}

jsi::Object assign(jsi::Runtime& runtime, jsi::Object target, const jsi::Object& source) {
  auto propertyNames = source.getPropertyNames(runtime);
  auto length = propertyNames.size(runtime);

  for (size_t i = 0; i < length; ++i) {
    auto name = propertyNames.getValueAtIndex(runtime, i).getString(runtime);
    auto value = source.getProperty(runtime, jsi::PropNameID::forString(runtime, name));
    target.setProperty(runtime, jsi::PropNameID::forString(runtime, name), std::move(value));
  }

  return target;
}

jsi::Object assign(jsi::Runtime& runtime, jsi::Object target, const std::vector<jsi::Object>& sources) {
  for (const auto& source : sources) {
    auto propertyNames = source.getPropertyNames(runtime);
    auto length = propertyNames.size(runtime);

    for (size_t i = 0; i < length; ++i) {
      auto name = propertyNames.getValueAtIndex(runtime, i).getString(runtime);
      auto value = source.getProperty(runtime, jsi::PropNameID::forString(runtime, name));
      target.setProperty(runtime, jsi::PropNameID::forString(runtime, name), std::move(value));
    }
  }

  return target;
}

bool shallowEqual(jsi::Runtime& runtime, const jsi::Value& objA, const jsi::Value& objB) {
  // @source:19-21 首先使用 Object.is 比较
  if (objectIs(runtime, objA, objB)) {
    return true;
  }

  // @source:23-28 如果不是对象或者为 null，返回 false
  if (!objA.isObject() || objA.isNull() || !objB.isObject() || objB.isNull()) {
    return false;
  }

  const auto& oA = objA.getObject(runtime);
  const auto& oB = objB.getObject(runtime);

  // @source:30-31 获取两个对象的键
  auto keysA = oA.getPropertyNames(runtime);
  auto keysB = oB.getPropertyNames(runtime);

  auto lengthA = keysA.size(runtime);
  auto lengthB = keysB.size(runtime);

  // @source:33-35 键数量不同，不相等
  if (lengthA != lengthB) {
    return false;
  }

  // @source:38-47 检查 A 的每个键在 B 中是否存在且值相等
  for (size_t i = 0; i < lengthA; ++i) {
    auto keyName = keysA.getValueAtIndex(runtime, i).getString(runtime);
    auto propNameId = jsi::PropNameID::forString(runtime, keyName);

    // 检查 B 是否有这个属性
    if (!oB.hasProperty(runtime, propNameId)) {
      return false;
    }

    // 比较属性值
    auto valueA = oA.getProperty(runtime, propNameId);
    auto valueB = oB.getProperty(runtime, propNameId);

    if (!objectIs(runtime, valueA, valueB)) {
      return false;
    }
  }

  return true;
}

bool isNullOrUndefined(const jsi::Value& value) {
  return value.isNull() || value.isUndefined();
}

bool isEmptyObject(jsi::Runtime& runtime, const jsi::Object& obj) {
  auto propertyNames = obj.getPropertyNames(runtime);
  return propertyNames.size(runtime) == 0;
}

std::vector<std::string> getObjectKeys(jsi::Runtime& runtime, const jsi::Object& obj) {
  std::vector<std::string> keys;
  auto propertyNames = obj.getPropertyNames(runtime);
  auto length = propertyNames.size(runtime);

  keys.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    auto name = propertyNames.getValueAtIndex(runtime, i).getString(runtime);
    keys.push_back(name.utf8(runtime));
  }

  return keys;
}

bool isFunction(jsi::Runtime& runtime, const jsi::Value& value) {
  return value.isObject() && value.getObject(runtime).isFunction(runtime);
}

bool isString(const jsi::Value& value) {
  return value.isString();
}

bool isNumber(const jsi::Value& value) {
  return value.isNumber();
}

bool isBool(const jsi::Value& value) {
  return value.isBool();
}

bool isSymbol(const jsi::Value& value) {
  return value.isSymbol();
}

bool isObject(const jsi::Value& value) {
  return value.isObject();
}

} // namespace react::shared
