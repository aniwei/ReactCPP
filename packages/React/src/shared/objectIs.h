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
inline bool objectIs(const std::any& x, const std::any& y) {
    // 类型不同直接返回 false
    if (x.type() != y.type()) {
        return false;
    }
    
    // 都为空
    if (!x.has_value() && !y.has_value()) {
        return true;
    }
    
    // 其中一个为空
    if (!x.has_value() || !y.has_value()) {
        return false;
    }
    
    // 比较基本类型
    try {
        // int
        if (x.type() == typeid(int)) {
            return std::any_cast<int>(x) == std::any_cast<int>(y);
        }
        
        // double
        if (x.type() == typeid(double)) {
            double dx = std::any_cast<double>(x);
            double dy = std::any_cast<double>(y);
            return objectIsImpl(dx, dy);
        }
        
        // float
        if (x.type() == typeid(float)) {
            float fx = std::any_cast<float>(x);
            float fy = std::any_cast<float>(y);
            return objectIsImpl(fx, fy);
        }
        
        // bool
        if (x.type() == typeid(bool)) {
            return std::any_cast<bool>(x) == std::any_cast<bool>(y);
        }
        
        // string
        if (x.type() == typeid(std::string)) {
            return std::any_cast<std::string>(x) == std::any_cast<std::string>(y);
        }
        
        // const char*
        if (x.type() == typeid(const char*)) {
            return std::string(std::any_cast<const char*>(x)) == 
                   std::string(std::any_cast<const char*>(y));
        }
        
        // long
        if (x.type() == typeid(long)) {
            return std::any_cast<long>(x) == std::any_cast<long>(y);
        }
        
        // long long
        if (x.type() == typeid(long long)) {
            return std::any_cast<long long>(x) == std::any_cast<long long>(y);
        }
        
        // size_t
        if (x.type() == typeid(size_t)) {
            return std::any_cast<size_t>(x) == std::any_cast<size_t>(y);
        }
        
        // unsigned int
        if (x.type() == typeid(unsigned int)) {
            return std::any_cast<unsigned int>(x) == std::any_cast<unsigned int>(y);
        }
        
        // char
        if (x.type() == typeid(char)) {
            return std::any_cast<char>(x) == std::any_cast<char>(y);
        }
        
        // shared_ptr<void> - 比较地址
        if (x.type() == typeid(std::shared_ptr<void>)) {
            return std::any_cast<std::shared_ptr<void>>(x).get() == 
                   std::any_cast<std::shared_ptr<void>>(y).get();
        }
        
        // void* - 比较地址
        if (x.type() == typeid(void*)) {
            return std::any_cast<void*>(x) == std::any_cast<void*>(y);
        }
        
        // std::nullptr_t
        if (x.type() == typeid(std::nullptr_t)) {
            return true;  // null === null
        }
        
        // std::any (嵌套)
        if (x.type() == typeid(std::any)) {
            return objectIs(std::any_cast<std::any>(x), std::any_cast<std::any>(y));
        }
        
    } catch (const std::bad_any_cast&) {
        return false;
    }
    
    // 对于不支持的类型，比较 type_info
    // 这意味着两个对象类型相同但值可能不同
    // 在这种情况下，我们假设它们不相等（保守策略）
    return false;
}

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
inline bool shallowEqual(
    const std::unordered_map<std::string, std::any>& objA,
    const std::unordered_map<std::string, std::any>& objB
) {
    if (objA.size() != objB.size()) {
        return false;
    }
    
    for (const auto& [key, valueA] : objA) {
        auto it = objB.find(key);
        if (it == objB.end()) {
            return false;
        }
        if (!objectIs(valueA, it->second)) {
            return false;
        }
    }
    
    return true;
}

} // namespace react::shared
