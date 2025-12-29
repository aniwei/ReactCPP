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
#include <any>
#include <memory>
#include <string>
#include <optional>

#include "shared/ReactSymbols.h"
#include "../runtime/ReactContainerInfo.h"

namespace facebook::jsi {
class Runtime;
class Value;
class Object;
} // namespace facebook::jsi

namespace react {

using namespace facebook;


// ReactElement 结构体
struct ReactElement 
  : public jsi::HostObject,
    public std::enable_shared_from_this<ReactElement> {
  shared::ReactSymbol typeSymbol = shared::REACT_ELEMENT_TYPE;
  jsi::Value type;
  jsi::Value key = jsi::Value::undefined();
  jsi::Value ref = jsi::Value::undefined();
  jsi::Value props;
  jsi::Value _owner = jsi::Value::undefined();
    
#ifdef DEV
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

  jsi::Value get(jsi::Runtime& runtime, const jsi::PropNameID& name) override;
  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime& runtime) override;
  
  // 检查是否为有效的 React Element
  bool isValidElement() const {
    return typeSymbol == shared::REACT_ELEMENT_TYPE;
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


// ReactElement 创建函数
// 创建 ReactElement
// 等同于 React.createElement
ReactElement createElement(
  facebook::jsi::Runtime& runtime,
  const jsi::Value& type,
  const jsi::Value& props,
  const jsi::Value& key,
  const jsi::Value& ref = jsi::Value::undefined()
);


// 类型检查函数
// 检查是否为有效的 React Element
bool isValidElement(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& value);

// 获取元素的类型字符串
std::string getElementTypeString(const ReactElement& element);

// ReactPortal
struct ReactPortal {
  shared::ReactSymbol typeSymbol = shared::REACT_PORTAL_TYPE;
  ContainerInfo containerInfo;
  jsi::Value key = jsi::Value::undefined();
  jsi::Value children;
  jsi::Value implementation;
};

// 创建 Portal
ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  const ContainerInfo& containerInfo,
  const jsi::Value& key = jsi::Value::undefined());

ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  ReactDOMContainer* container,
  const jsi::Value& key = jsi::Value::undefined());

ReactPortal createPortal(
  facebook::jsi::Runtime& rt,
  const jsi::Value& children,
  const std::string& debugName,
  const jsi::Value& key = jsi::Value::undefined());


} // namespace react
