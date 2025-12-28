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

namespace react::reconciler {

// =============================================================================
// React Context 类型
// @source reactjs/packages/shared/ReactTypes.js
// =============================================================================

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

// =============================================================================
// 类型别名 (使用 ReactFiber.h 中的定义)
// =============================================================================

using ContextDependencyRef = std::shared_ptr<ContextDependency>;
using DependenciesRef = std::shared_ptr<Dependencies>;

// =============================================================================
// Stack Cursor (用于 Context 栈)
// @source reactjs/packages/react-reconciler/src/ReactFiberStack.js
// =============================================================================

template<typename T>
struct StackCursor {
    T current;
    
    StackCursor(T defaultValue) : current(defaultValue) {}
};

// =============================================================================
// Context 栈操作
// =============================================================================

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

// =============================================================================
// ReactFiberNewContext 类
// @source reactjs/packages/react-reconciler/src/ReactFiberNewContext.js
// =============================================================================

class ReactFiberNewContext {
public:
    ReactFiberNewContext() = default;
    
    // =========================================================================
    // 上下文操作
    // =========================================================================
    
    /**
     * 重置 Context 依赖
     * @source:52-60 resetContextDependencies
     */
    void resetContextDependencies() {
        currentlyRenderingFiber_ = nullptr;
        lastContextDependency_ = nullptr;
    }
    
    /**
     * Push Provider 值
     * @source:75-100 pushProvider
     */
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
    
    /**
     * Pop Provider 值
     * @source:120-140 popProvider
     */
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
    
    /**
     * 读取 Context
     * @source:200-280 readContext
     */
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
    
    /**
     * 检查 Context 是否变化
     * @source:390-450 checkIfContextChanged
     */
    bool checkIfContextChanged(DependenciesRef dependencies) {
        if (dependencies == nullptr) {
            return false;
        }
        
        auto dependency = dependencies->firstContext;
        while (dependency != nullptr) {
            // 比较当前值和 memoized 值
            // 由于类型擦除，需要在具体使用时实现比较
            dependency = dependency->next;
        }
        
        return false;
    }
    
    /**
     * 在父路径上调度 Context 工作
     * @source:145-195 scheduleContextWorkOnParentPath
     */
    void scheduleContextWorkOnParentPath(
        FiberRef parent,
        Lanes renderLanes
    ) {
        FiberRef node = parent;
        while (node != nullptr) {
            auto alternate = node->alternate;
            
            if (!isSubsetOfLanes(node->childLanes, renderLanes)) {
                node->childLanes = mergeLanes(node->childLanes, renderLanes);
                if (!alternate.expired()) {
                    auto alt = alternate.lock();
                    alt->childLanes = mergeLanes(alt->childLanes, renderLanes);
                }
            } else if (!alternate.expired()) {
                auto alt = alternate.lock();
                if (!isSubsetOfLanes(alt->childLanes, renderLanes)) {
                    alt->childLanes = mergeLanes(alt->childLanes, renderLanes);
                } else {
                    break;
                }
            } else {
                break;
            }
            
            if (!node->return_.expired()) {
                node = node->return_.lock();
            } else {
                break;
            }
        }
    }
    
    /**
     * 传播 Context 变化
     * @source:300-385 propagateContextChange
     */
    void propagateContextChange(
        FiberRef workInProgress,
        std::any,
        Lanes
    ) {
        // 遍历子树查找消费者
        FiberRef fiber = workInProgress->child;
        if (fiber != nullptr) {
            fiber->return_ = workInProgress;
        }
        
        while (fiber != nullptr) {
            FiberRef nextFiber = nullptr;
            
            // 检查这个 fiber 是否依赖 context
            if (fiber->dependencies != nullptr) {
                auto dep = fiber->dependencies->firstContext;
                
                while (dep != nullptr) {
                    // 比较 context 引用
                    // 需要类型擦除后的比较
                    dep = dep->next;
                }
            }
            
            // 继续遍历
            if (fiber->child != nullptr) {
                nextFiber = fiber->child;
                nextFiber->return_ = fiber;
            }
            
            if (nextFiber == nullptr) {
                nextFiber = fiber;
                while (nextFiber != nullptr) {
                    if (nextFiber == workInProgress) {
                        nextFiber = nullptr;
                        break;
                    }
                    auto sibling = nextFiber->sibling;
                    if (sibling != nullptr) {
                        sibling->return_ = nextFiber->return_;
                        nextFiber = sibling;
                        break;
                    }
                    if (!nextFiber->return_.expired()) {
                        nextFiber = nextFiber->return_.lock();
                    } else {
                        nextFiber = nullptr;
                    }
                }
            }
            
            fiber = nextFiber;
        }
    }
    
    /**
     * 准备读取 Context
     */
    void prepareToReadContext(FiberRef workInProgress, Lanes) {
        currentlyRenderingFiber_ = workInProgress;
        lastContextDependency_ = nullptr;
        
        if (workInProgress->dependencies != nullptr) {
            workInProgress->dependencies->firstContext = nullptr;
        }
    }
    
    /**
     * 获取当前渲染 Fiber
     */
    FiberRef getCurrentlyRenderingFiber() const {
        return currentlyRenderingFiber_;
    }
    
private:
    FiberRef currentlyRenderingFiber_ = nullptr;
    ContextDependencyRef lastContextDependency_ = nullptr;
    std::stack<std::any> valueStack_;
};

// =============================================================================
// 全局实例
// =============================================================================

inline ReactFiberNewContext& getContextModule() {
    static ReactFiberNewContext instance;
    return instance;
}

// =============================================================================
// 便捷函数
// =============================================================================

/**
 * 创建 Context
 * @source reactjs/packages/react/src/ReactContext.js
 */
template<typename T>
std::shared_ptr<ReactContext<T>> createContext(T defaultValue) {
    return ReactContext<T>::create(defaultValue);
}

/**
 * 读取 Context (便捷函数)
 */
template<typename T>
T readContext(std::shared_ptr<ReactContext<T>> context) {
    return getContextModule().readContext(context);
}

} // namespace react::reconciler
