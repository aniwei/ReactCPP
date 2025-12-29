/**
 * Object.is polyfill
 * 
 * 实现 JavaScript Object.is 语义的值比较
 * 用于 Hooks 依赖比较
 * 
 * @source reactjs/packages/shared/objectIs.js
 */

#pragma once

#include <any>
#include <cmath>
#include <string>
#include <typeinfo>
#include <functional>
#include <unordered_map>

namespace react::shared {

/**
 * Object.is 实现
 * 
 * 与 === 的区别:
 * - Object.is(NaN, NaN) => true
 * - Object.is(0, -0) => false
 * 
 * @source objectIs.js:10-20
 */
template<typename T>
inline bool objectIsImpl(const T& x, const T& y) {
    // 处理特殊情况
    if constexpr (std::is_floating_point_v<T>) {
        // NaN 与 NaN 相等
        if (std::isnan(x) && std::isnan(y)) {
            return true;
        }
        // +0 与 -0 不相等
        if (x == 0 && y == 0) {
            return (1.0 / x) == (1.0 / y);
        }
    }
    return x == y;
}

/**
 * Object.is 用于 std::any
 * 
 * 比较两个 std::any 值
 * 支持基本类型和引用类型
 */
bool objectIs(const std::any& x, const std::any& y);

/**
 * 比较两个相同类型的值
 */
template<typename T>
inline bool is(const T& x, const T& y) {
    return objectIsImpl(x, y);
}

/**
 * 浅比较两个对象
 * 用于 memo 和 shouldComponentUpdate
 */
bool shallowEqual(
    const std::unordered_map<std::string, std::any>& objA,
    const std::unordered_map<std::string, std::any>& objB
);

} // namespace react::shared
