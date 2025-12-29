/**
 * React Fiber New Context
 * 
 * Context 实现 - 提供跨组件树的数据传递机制
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberNewContext.js
 */

#pragma once

#include <memory>
#include <functional>
#include <any>
#include <vector>
#include <stack>

#include "ReactFiber.h"
#include "ReactFiberLane.h"
#include "../shared/objectIs.h"

namespace react {
class ReactHostRuntime;
} // namespace react

namespace react::reconciler {


// React Context 类型
template<typename T>
struct ReactContext {
  std::string typeOf = "react.context";
  
  // 当前值
  T _currentValue;
  T _currentValue2;  // 用于第二个渲染器
  
  // 默认值
  T _defaultValue;
  
  // Provider 组件
  std::any Provider;
  
  // Consumer 组件
  std::any Consumer;
  
  // 调试信息
  std::string _debugDisplayName;
  
  // 创建 Context
  static std::shared_ptr<ReactContext<T>> create(T defaultValue) {
    auto context = std::make_shared<ReactContext<T>>();
    context->_defaultValue = defaultValue;
    context->_currentValue = defaultValue;
    context->_currentValue2 = defaultValue;
    return context;
  }
};


// 类型别名 (使用 ReactFiber.h 中的定义)
using ContextDependencyRef = std::shared_ptr<ContextDependency>;
using DependenciesRef = std::shared_ptr<Dependencies>;

// Stack Cursor (用于 Context 栈)
template<typename T>
struct StackCursor {
    T current;
    
    StackCursor(T defaultValue) : current(defaultValue) {}
};


// Context 栈操作


template<typename T>
class ContextStack {
public:
  void push(StackCursor<T>& cursor, T value, FiberRef) {
    stack_.push(cursor.current);
    cursor.current = value;
  }
  
  void pop(StackCursor<T>& cursor, FiberRef) {
    if (!stack_.empty()) {
      cursor.current = stack_.top();
      stack_.pop();
    }
  }
    
private:
  std::stack<T> stack_;
};


// ReactFiberNewContext 类
class ReactFiberNewContext {
public:
  ReactFiberNewContext() = default;
  
  // 重置 Context 依赖
  void resetContextDependencies();
  
  // Push Provider 值
  template<typename T>
  void pushProvider(
    FiberRef,
    std::shared_ptr<ReactContext<T>> context,
    T nextValue
  ) {
    // 保存当前值到栈
    valueStack_.push(std::any(context->_currentValue));
    
    // 设置新值
    context->_currentValue = nextValue;
  }
  
  // Pop Provider 值
  template<typename T>
  void popProvider(
    std::shared_ptr<ReactContext<T>> context,
    FiberRef
  ) {
    if (!valueStack_.empty()) {
      context->_currentValue = std::any_cast<T>(valueStack_.top());
      valueStack_.pop();
    }
  }
  
  // 读取 Context
  template<typename T>
  T readContext(std::shared_ptr<ReactContext<T>> context) {
    T value = context->_currentValue;
      
    if (lastContextDependency_ == nullptr) {
      // 第一个 context 依赖
      auto contextItem = std::make_shared<ContextDependency>();
      contextItem->context = context;
      contextItem->memoizedValue = value;
      contextItem->next = nullptr;
      
      lastContextDependency_ = contextItem;
      
      // 设置到 fiber 的依赖
      if (currentlyRenderingFiber_ != nullptr) {
        if (currentlyRenderingFiber_->dependencies == nullptr) {
            currentlyRenderingFiber_->dependencies = std::make_shared<Dependencies>();
        }
        currentlyRenderingFiber_->dependencies->firstContext = contextItem;
      }
    } else {
      // 追加 context 依赖
      auto contextItem = std::make_shared<ContextDependency>();
      contextItem->context = context;
      contextItem->memoizedValue = value;
      contextItem->next = nullptr;
      
      lastContextDependency_->next = contextItem;
      lastContextDependency_ = contextItem;
    }
    
    return value;
  }
  
  // 检查 Context 是否变化/
  bool checkIfContextChanged(DependenciesRef dependencies);
  
  // 在父路径上调度 Context 工作
  void scheduleContextWorkOnParentPath(
    const FiberRef& parent,
    Lanes renderLanes);
  
  // 传播 Context 变化
  void propagateContextChange(
    const FiberRef& workInProgress,
    std::any,
    Lanes);
  
  // 准备读取 Context
  void prepareToReadContext(const FiberRef& workInProgress, Lanes);
  
  // 获取当前渲染 Fiber
  FiberRef getCurrentlyRenderingFiber() const;
  
private:
  FiberRef currentlyRenderingFiber_ = nullptr;
  ContextDependencyRef lastContextDependency_ = nullptr;
  std::stack<std::any> valueStack_;
};


// 实例（per ReactHostRuntime）
ReactFiberNewContext& getContextModule(react::ReactHostRuntime& hostRuntime);

// 便捷函数
// 创建 Context
template<typename T>
std::shared_ptr<ReactContext<T>> createContext(T defaultValue) {
  return ReactContext<T>::create(defaultValue);
}

// 读取 Context (便捷函数)
template<typename T>
T readContext(react::ReactHostRuntime& hostRuntime, std::shared_ptr<ReactContext<T>> context) {
  return getContextModule(hostRuntime).readContext(context);
}

} // namespace react::reconciler
