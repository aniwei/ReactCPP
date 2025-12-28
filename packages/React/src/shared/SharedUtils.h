/**
 * React Shared 工具函数集合
 * 
 * 包含 objectIs, hasOwnProperty, isArray, assign, shallowEqual 等工具函数
 * 这些是 React 内部使用的基础工具
 * 
 * @source reactjs/packages/shared/objectIs.js
 * @source reactjs/packages/shared/hasOwnProperty.js
 * @source reactjs/packages/shared/isArray.js
 * @source reactjs/packages/shared/assign.js
 * @source reactjs/packages/shared/shallowEqual.js
 */

#pragma once

#include <jsi/jsi.h>
#include <cmath>
#include <type_traits>
#include <vector>
#include <unordered_set>
#include <string>

namespace facebook::jsi {
class Runtime;
class Value;
class Object;
} // namespace facebook::jsi

namespace react::shared {

using namespace facebook;

// =============================================================================
// objectIs - Object.is 的 C++ 实现
// @source reactjs/packages/shared/objectIs.js:15-19
// =============================================================================

/**
 * Object.is polyfill
 * 
 * 处理 NaN 和 +0/-0 的特殊情况:
 * - Object.is(NaN, NaN) => true
 * - Object.is(+0, -0) => false
 */
template<typename T>
inline bool objectIs(const T& x, const T& y) {
    // 基本类型直接比较
    return x == y;
}

// 特化: double 类型需要处理 NaN 和 +0/-0
template<>
inline bool objectIs(const double& x, const double& y) {
    // @source:17 处理 +0 和 -0 的情况
    if (x == y) {
        // 区分 +0 和 -0: 1/+0 = +Infinity, 1/-0 = -Infinity
        return x != 0.0 || (1.0 / x == 1.0 / y);
    }
    // @source:17 处理 NaN 的情况: NaN !== NaN
    return std::isnan(x) && std::isnan(y);
}

// 特化: float 类型同样处理
template<>
inline bool objectIs(const float& x, const float& y) {
    if (x == y) {
        return x != 0.0f || (1.0f / x == 1.0f / y);
    }
    return std::isnan(x) && std::isnan(y);
}

/**
 * JSI Value 版本的 Object.is
 * 用于运行时比较 JavaScript 值
 */
inline bool objectIs(jsi::Runtime& runtime, const jsi::Value& x, const jsi::Value& y) {
    // 处理 undefined
    if (x.isUndefined() && y.isUndefined()) {
        return true;
    }
    
    // 处理 null
    if (x.isNull() && y.isNull()) {
        return true;
    }
    
    // 类型不同则不相等
    if (x.isUndefined() != y.isUndefined() ||
        x.isNull() != y.isNull() ||
        x.isBool() != y.isBool() ||
        x.isNumber() != y.isNumber() ||
        x.isString() != y.isString() ||
        x.isSymbol() != y.isSymbol() ||
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

// =============================================================================
// hasOwnProperty
// @source reactjs/packages/shared/hasOwnProperty.js:11
// =============================================================================

/**
 * 检查对象是否拥有指定的自有属性
 */
inline bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const std::string& key) {
    return obj.hasProperty(runtime, key.c_str());
}

inline bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const jsi::PropNameID& name) {
    return obj.hasProperty(runtime, name);
}

// =============================================================================
// isArray
// @source reactjs/packages/shared/isArray.js:14-16
// =============================================================================

/**
 * 检查 JSI Value 是否为数组
 */
inline bool isArray(jsi::Runtime& runtime, const jsi::Value& value) {
    if (!value.isObject()) {
        return false;
    }
    return value.getObject(runtime).isArray(runtime);
}

inline bool isArray(jsi::Runtime& runtime, const jsi::Object& obj) {
    return obj.isArray(runtime);
}

// C++ 原生类型检查
template<typename T>
inline constexpr bool isArray(const std::vector<T>&) {
    return true;
}

template<typename T, size_t N>
inline constexpr bool isArray(const T (&)[N]) {
    return true;
}

template<typename T>
inline constexpr bool isArray(const T&) {
    return false;
}

// =============================================================================
// assign - Object.assign 实现
// @source reactjs/packages/shared/assign.js:11
// =============================================================================

/**
 * 将源对象的属性复制到目标对象
 * 模拟 Object.assign 行为
 */
inline jsi::Object assign(
    jsi::Runtime& runtime,
    jsi::Object target,
    const jsi::Object& source
) {
    auto propertyNames = source.getPropertyNames(runtime);
    auto length = propertyNames.size(runtime);
    
    for (size_t i = 0; i < length; ++i) {
        auto name = propertyNames.getValueAtIndex(runtime, i).getString(runtime);
        auto value = source.getProperty(runtime, jsi::PropNameID::forString(runtime, name));
        target.setProperty(runtime, jsi::PropNameID::forString(runtime, name), std::move(value));
    }
    
    return target;
}

/**
 * 多源对象合并
 */
inline jsi::Object assign(
    jsi::Runtime& runtime,
    jsi::Object target,
    const std::vector<jsi::Object>& sources
) {
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

// =============================================================================
// shallowEqual - 浅比较
// @source reactjs/packages/shared/shallowEqual.js:18-51
// =============================================================================

/**
 * 浅比较两个 JSI 值
 * 
 * 执行对象键的相等性检查，当任何键的值在两个参数之间不严格相等时返回 false
 * 当所有键的值都严格相等时返回 true
 */
inline bool shallowEqual(jsi::Runtime& runtime, const jsi::Value& objA, const jsi::Value& objB) {
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

// =============================================================================
// 辅助工具函数
// =============================================================================

/**
 * 检查值是否为 null 或 undefined
 */
inline bool isNullOrUndefined(const jsi::Value& value) {
    return value.isNull() || value.isUndefined();
}

/**
 * 检查对象是否为空
 */
inline bool isEmptyObject(jsi::Runtime& runtime, const jsi::Object& obj) {
    auto propertyNames = obj.getPropertyNames(runtime);
    return propertyNames.size(runtime) == 0;
}

/**
 * 获取对象的所有键
 */
inline std::vector<std::string> getObjectKeys(jsi::Runtime& runtime, const jsi::Object& obj) {
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

/**
 * 类型检查工具
 */
inline bool isFunction(jsi::Runtime& runtime, const jsi::Value& value) {
    return value.isObject() && value.getObject(runtime).isFunction(runtime);
}

inline bool isString(const jsi::Value& value) {
    return value.isString();
}

inline bool isNumber(const jsi::Value& value) {
    return value.isNumber();
}

inline bool isBool(const jsi::Value& value) {
    return value.isBool();
}

inline bool isSymbol(const jsi::Value& value) {
    return value.isSymbol();
}

inline bool isObject(const jsi::Value& value) {
    return value.isObject();
}

} // namespace react::shared
