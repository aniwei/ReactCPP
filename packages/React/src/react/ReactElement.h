/**
 * React Element 结构
 * 
 * ReactElement 是 React 组件树的基本构建块
 * 由 createElement/JSX 创建
 * 
 * @source reactjs/packages/shared/ReactElementType.js
 * @source reactjs/packages/react/src/jsx/ReactJSXElement.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <any>
#include <optional>

#include "shared/ReactSymbols.h"

namespace facebook::jsi {
class Runtime;
class Value;
class Object;
} // namespace facebook::jsi

namespace react {

using namespace facebook;

// =============================================================================
// ReactElement 结构体
// @source reactjs/packages/shared/ReactElementType.js:12-26
// =============================================================================

/**
 * ReactElement - React 元素
 * 
 * 这是一个不可变的描述，告诉 React 应该渲染什么
 */
struct ReactElement {
  // @source:13 $$typeof 标识这是一个 React Element
  shared::ReactSymbol $$typeof = shared::REACT_ELEMENT_TYPE;
  
  // @source:14 type 可以是字符串(原生元素)、函数(组件)或 Symbol
  jsi::Value type;
  
  // @source:15 key 用于 reconciliation
  jsi::Value key = jsi::Value::undefined();
  
  // @source:16 ref 引用
  jsi::Value ref = jsi::Value::undefined();
  
  // @source:17 props 属性对象
  jsi::Value props;
  
  // @source:19 _owner (DEV or string refs)
  jsi::Value _owner = jsi::Value::undefined();
  
  // =========================================================================
  // DEV 字段
  // =========================================================================
    
#ifdef DEV
  // @source:22 验证状态: 0=未验证, 1=已验证, 2=强制失败
  struct Store {
      int validated = 0;
  };
  Store _store;
  
  // @source:23
  std::any _debugInfo;
  
  // @source:24
  std::any _debugStack;
  
  // @source:25
  std::any _debugTask;
#endif
    
  // =========================================================================
  // 构造函数
  // =========================================================================
  
  ReactElement() = default;
  
  ReactElement(
    facebook::jsi::Runtime& runtime,
    const jsi::Value& type_,
    const jsi::Value& key_,
    const jsi::Value& ref_,
    const jsi::Value& props_
  ) : type(runtime, type_),
      key(runtime, key_),
      ref(runtime, ref_),
      props(runtime, props_)
  {}
  
  // =========================================================================
  // 辅助方法
  // =========================================================================
  
  /**
   * 检查是否为有效的 React Element
   */
  bool isValidElement() const {
      return $$typeof == shared::REACT_ELEMENT_TYPE;
  }
  
  /**
   * 检查是否有 key
   */
  bool hasKey() const {
      return !key.isUndefined();
  }
  
  /**
   * 检查是否有 ref
   */
  bool hasRef() const {
      return !ref.isUndefined();
  }
};

// =============================================================================
// ReactElement 创建函数
// @source reactjs/packages/react/src/jsx/ReactJSXElement.js
// =============================================================================

/**
 * 创建 ReactElement
 * 等同于 React.createElement
 */
inline ReactElement createElement(
  facebook::jsi::Runtime& runtime,
  const jsi::Value& type,
  const jsi::Value& props,
  const jsi::Value& key,
  const jsi::Value& ref = jsi::Value::undefined()
) {
  return ReactElement(runtime, type, key, ref, props);
}

// =============================================================================
// 类型检查函数
// =============================================================================

/**
 * 检查是否为有效的 React Element
 */
inline bool isValidElement(const std::any& object) {
  if (!object.has_value()) {
    return false;
  }
  
  try {
    const auto& element = std::any_cast<const ReactElement&>(object);
    return element.isValidElement();
  } catch (const std::bad_any_cast&) {
    return false;
  }
}

/**
 * 获取元素的类型字符串
 */
inline std::string getElementTypeString(const ReactElement& element) {
  // 如果 type 是字符串，直接返回
  try {
      return std::any_cast<std::string>(element.type);
  } catch (const std::bad_any_cast&) {
      // 不是字符串，可能是组件
  }
  
  // 如果 type 是 const char*
  try {
      return std::string(std::any_cast<const char*>(element.type));
  } catch (const std::bad_any_cast&) {
      // 也不是 const char*
  }
  
  // 其他类型
  return "[Component]";
}

// =============================================================================
// ReactPortal
// @source reactjs/packages/shared/ReactTypes.js
// =============================================================================

struct ReactPortal {
    shared::ReactSymbol $$typeof = shared::REACT_PORTAL_TYPE;
    std::optional<std::string> key = std::nullopt;
    std::any children;
    std::any containerInfo;
    std::any implementation;
};

/**
 * 创建 Portal
 */
inline ReactPortal createPortal(
    const std::any& children,
    const std::any& containerInfo,
    const std::optional<std::string>& key = std::nullopt
) {
    ReactPortal portal;
    portal.children = children;
    portal.containerInfo = containerInfo;
    portal.key = key;
    return portal;
}

// =============================================================================
// ReactFragment
// =============================================================================

/**
 * Fragment 只是一个 Symbol，没有额外结构
 * 使用 REACT_FRAGMENT_TYPE 作为 type
 */

} // namespace react
