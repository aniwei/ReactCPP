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


// objectIs - Object.is 的 C++ 实现
// @source reactjs/packages/shared/objectIs.js:15-19


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
bool objectIs(const double& x, const double& y);

// 特化: float 类型同样处理
template<>
bool objectIs(const float& x, const float& y);

/**
 * JSI Value 版本的 Object.is
 * 用于运行时比较 JavaScript 值
 */
bool objectIs(jsi::Runtime& runtime, const jsi::Value& x, const jsi::Value& y);


// hasOwnProperty
// @source reactjs/packages/shared/hasOwnProperty.js:11


/**
 * 检查对象是否拥有指定的自有属性
 */
bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const std::string& key);
bool hasOwnProperty(jsi::Runtime& runtime, const jsi::Object& obj, const jsi::PropNameID& name);


// isArray
// @source reactjs/packages/shared/isArray.js:14-16


/**
 * 检查 JSI Value 是否为数组
 */
bool isArray(jsi::Runtime& runtime, const jsi::Value& value);
bool isArray(jsi::Runtime& runtime, const jsi::Object& obj);

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


// assign - Object.assign 实现
// @source reactjs/packages/shared/assign.js:11


/**
 * 将源对象的属性复制到目标对象
 * 模拟 Object.assign 行为
 */
jsi::Object assign(
    jsi::Runtime& runtime,
    jsi::Object target,
    const jsi::Object& source
);

/**
 * 多源对象合并
 */
jsi::Object assign(
    jsi::Runtime& runtime,
    jsi::Object target,
    const std::vector<jsi::Object>& sources
);


// shallowEqual - 浅比较
// @source reactjs/packages/shared/shallowEqual.js:18-51


/**
 * 浅比较两个 JSI 值
 * 
 * 执行对象键的相等性检查，当任何键的值在两个参数之间不严格相等时返回 false
 * 当所有键的值都严格相等时返回 true
 */
bool shallowEqual(jsi::Runtime& runtime, const jsi::Value& objA, const jsi::Value& objB);


// 辅助工具函数


/**
 * 检查值是否为 null 或 undefined
 */
bool isNullOrUndefined(const jsi::Value& value);

/**
 * 检查对象是否为空
 */
bool isEmptyObject(jsi::Runtime& runtime, const jsi::Object& obj);

/**
 * 获取对象的所有键
 */
std::vector<std::string> getObjectKeys(jsi::Runtime& runtime, const jsi::Object& obj);

/**
 * 类型检查工具
 */
bool isFunction(jsi::Runtime& runtime, const jsi::Value& value);

bool isString(const jsi::Value& value);

bool isNumber(const jsi::Value& value);

bool isBool(const jsi::Value& value);

bool isSymbol(const jsi::Value& value);

bool isObject(const jsi::Value& value);

} // namespace react::shared
