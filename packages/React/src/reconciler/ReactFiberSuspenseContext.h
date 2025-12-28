/**
 * React Fiber Suspense Context
 * 
 * Suspense 上下文管理，用于跟踪 Suspense 边界和处理 Suspense 堆栈
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseContext.js
 */

#pragma once

#include <memory>
#include <optional>
#include <functional>

#include "ReactFiber.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"

namespace react::reconciler {

// =============================================================================
// Stack Cursor 类型
// @source ReactFiberStack.js
// =============================================================================

// 用于存储 cursor 历史的栈
inline std::vector<std::any> valueStack;
inline size_t stackIndex = 0;

template<typename T>
struct StackCursor {
  T current;
};

template<typename T>
inline StackCursor<T> createCursor(T defaultValue) {
  return StackCursor<T>{defaultValue};
}

template<typename T>
inline void push(StackCursor<T>& cursor, T value, FiberRef fiber) {
  // 保存当前值到栈
  if (stackIndex >= valueStack.size()) {
    valueStack.push_back(cursor.current);
  } else {
    valueStack[stackIndex] = cursor.current;
  }
  stackIndex++;
  cursor.current = value;
}

template<typename T>
inline void pop(StackCursor<T>& cursor, FiberRef fiber) {
  if (stackIndex > 0) {
    stackIndex--;
    cursor.current = std::any_cast<T>(valueStack[stackIndex]);
  }
}

// 重置栈（用于测试）
inline void resetStack() {
  valueStack.clear();
  stackIndex = 0;
}

// =============================================================================
// Suspense Handler Stack
// @source:21-23 suspenseHandlerStackCursor
// =============================================================================

/**
 * Suspense handler 是应该捕获 suspending 的边界
 * 即堆栈上最近的 `catch` 块
 */
inline StackCursor<FiberRef> suspenseHandlerStackCursor = createCursor<FiberRef>(nullptr);

// =============================================================================
// Shell Boundary
// @source:26-36 shellBoundary
// =============================================================================

/**
 * 表示当前树中不可见的最外层边界
 * 
 * 在这之上的一切都是 "shell"。当为 null 时，表示正在 shell 中渲染
 * 如果非 null，表示正在渲染比 shell 更深的新树
 */
inline FiberRef shellBoundary = nullptr;

/**
 * 获取 shell 边界
 * @source:38-40 getShellBoundary
 */
inline FiberRef getShellBoundary() {
  return shellBoundary;
}

// =============================================================================
// SuspenseContext 类型
// @source:163-166
// =============================================================================

using SuspenseContext = uint8_t;
using SubtreeSuspenseContext = uint8_t;
using ShallowSuspenseContext = uint8_t;

// @source:168
constexpr SuspenseContext DefaultSuspenseContext = 0b00;

// @source:170
constexpr SuspenseContext SubtreeSuspenseContextMask = 0b01;

// @source:174 - 用于 SuspenseList 强制新添加的项进入 fallback 状态
constexpr ShallowSuspenseContext ForceSuspenseFallback = 0b10;

// @source:176-178
inline StackCursor<SuspenseContext> suspenseStackCursor = 
  createCursor(DefaultSuspenseContext);

// =============================================================================
// Suspense Context 工具函数
// @source:180-210
// =============================================================================

/**
 * 检查是否有指定的 SuspenseList 上下文
 * @source:180-184 hasSuspenseListContext
 */
inline bool hasSuspenseListContext(
  SuspenseContext parentContext,
  SuspenseContext flag
) {
  return (parentContext & flag) != 0;
}

/**
 * 设置默认的浅层 SuspenseList 上下文
 * @source:186-190 setDefaultShallowSuspenseListContext
 */
inline SuspenseContext setDefaultShallowSuspenseListContext(
  SuspenseContext parentContext
) {
  return parentContext & SubtreeSuspenseContextMask;
}

/**
 * 设置浅层 SuspenseList 上下文
 * @source:192-197 setShallowSuspenseListContext
 */
inline SuspenseContext setShallowSuspenseListContext(
  SuspenseContext parentContext,
  ShallowSuspenseContext shallowContext
) {
  return (parentContext & SubtreeSuspenseContextMask) | shallowContext;
}

/**
 * 推送 SuspenseList 上下文
 * @source:199-203 pushSuspenseListContext
 */
inline void pushSuspenseListContext(
  FiberRef fiber,
  SuspenseContext newContext
) {
  push(suspenseStackCursor, newContext, fiber);
}

/**
 * 弹出 SuspenseList 上下文
 * @source:205-207 popSuspenseListContext
 */
inline void popSuspenseListContext(FiberRef fiber) {
  pop(suspenseStackCursor, fiber);
}

// =============================================================================
// Suspense Handler 函数
// @source:42-140
// =============================================================================

/**
 * 获取当前 Suspense handler
 * @source:148-150 getSuspenseHandler
 */
inline FiberRef getSuspenseHandler() {
  return suspenseHandlerStackCursor.current;
}

/**
 * 弹出 Suspense handler
 * @source:152-159 popSuspenseHandler
 */
inline void popSuspenseHandler(FiberRef fiber) {
  pop(suspenseHandlerStackCursor, fiber);
  if (shellBoundary == fiber) {
    // 弹出回到 shell
    shellBoundary = nullptr;
  }
  popSuspenseListContext(fiber);
}

/**
 * 重用堆栈上的 Suspense handler
 * @source:143-146 reuseSuspenseHandlerOnStack
 */
inline void reuseSuspenseHandlerOnStack(FiberRef fiber) {
  pushSuspenseListContext(fiber, suspenseStackCursor.current);
  push(suspenseHandlerStackCursor, getSuspenseHandler(), fiber);
}

/**
 * 检查当前树是否隐藏
 * @source ReactFiberHiddenContext.js
 */
inline bool isCurrentTreeHidden() {
  // 简化实现
  return false;
}

/**
 * 推送主树 Suspense handler
 * @source:42-85 pushPrimaryTreeSuspenseHandler
 */
inline void pushPrimaryTreeSuspenseHandler(FiberRef handler) {
  auto current = handler->alternate.lock();
  
  // 浅层 Suspense 上下文字段应该只传播一层
  pushSuspenseListContext(
    handler,
    setDefaultShallowSuspenseListContext(suspenseStackCursor.current)
  );
  
  push(suspenseHandlerStackCursor, handler, handler);
  
  if (shellBoundary == nullptr) {
    if (!current || isCurrentTreeHidden()) {
      // 这个边界在当前 UI 中不可见
      shellBoundary = handler;
    }
  }
}

/**
 * 推送 fallback 树 Suspense handler
 * @source:87-91 pushFallbackTreeSuspenseHandler
 */
inline void pushFallbackTreeSuspenseHandler(FiberRef fiber) {
  // 即将渲染 fallback，如果 fallback 中有内容 suspend
  // 类似于在 `catch` 块中抛出，这个边界不应该捕获
  reuseSuspenseHandlerOnStack(fiber);
}

/**
 * 推送 dehydrated Activity Suspense handler
 * @source:93-105 pushDehydratedActivitySuspenseHandler
 */
inline void pushDehydratedActivitySuspenseHandler(FiberRef fiber) {
  pushSuspenseListContext(fiber, suspenseStackCursor.current);
  push(suspenseHandlerStackCursor, fiber, fiber);
  if (shellBoundary == nullptr) {
    shellBoundary = fiber;
  }
}

/**
 * 推送 Offscreen Suspense handler
 * @source:107-128 pushOffscreenSuspenseHandler
 */
inline void pushOffscreenSuspenseHandler(FiberRef fiber) {
  if (fiber->tag == OffscreenComponent) {
    pushSuspenseListContext(fiber, suspenseStackCursor.current);
    push(suspenseHandlerStackCursor, fiber, fiber);
    if (shellBoundary == nullptr) {
      shellBoundary = fiber;
    }
  } else {
    // 这是一个 LegacyHidden 组件
    reuseSuspenseHandlerOnStack(fiber);
  }
}

} // namespace react::reconciler
