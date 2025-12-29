/**
 * React Fiber Suspense Context
 * 
 * Suspense 上下文管理，用于跟踪 Suspense 边界和处理 Suspense 堆栈
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseContext.js
 */

#pragma once

#include <any>
#include <memory>
#include <optional>
#include <functional>
#include <vector>

#include "ReactFiber.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"

namespace react::reconciler {


// Stack Cursor 类型
// @source ReactFiberStack.js


// 用于存储 cursor 历史的栈
extern std::vector<std::any> valueStack;
extern size_t stackIndex;

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
  (void)fiber;
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
  (void)fiber;
  if (stackIndex > 0) {
    stackIndex--;
    cursor.current = std::any_cast<T>(valueStack[stackIndex]);
  }
}

// 重置栈（用于测试）
void resetStack();


// Suspense Handler Stack
// @source:21-23 suspenseHandlerStackCursor


/**
 * Suspense handler 是应该捕获 suspending 的边界
 * 即堆栈上最近的 `catch` 块
 */
extern StackCursor<FiberRef> suspenseHandlerStackCursor;


// Shell Boundary
// @source:26-36 shellBoundary


/**
 * 表示当前树中不可见的最外层边界
 * 
 * 在这之上的一切都是 "shell"。当为 null 时，表示正在 shell 中渲染
 * 如果非 null，表示正在渲染比 shell 更深的新树
 */
extern FiberRef shellBoundary;

/**
 * 获取 shell 边界
 * @source:38-40 getShellBoundary
 */
FiberRef getShellBoundary();


// SuspenseContext 类型
// @source:163-166


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
extern StackCursor<SuspenseContext> suspenseStackCursor;


// Suspense Context 工具函数
// @source:180-210


/**
 * 检查是否有指定的 SuspenseList 上下文
 * @source:180-184 hasSuspenseListContext
 */
bool hasSuspenseListContext(
  SuspenseContext parentContext,
  SuspenseContext flag
);

/**
 * 设置默认的浅层 SuspenseList 上下文
 * @source:186-190 setDefaultShallowSuspenseListContext
 */
SuspenseContext setDefaultShallowSuspenseListContext(
  SuspenseContext parentContext
);

/**
 * 设置浅层 SuspenseList 上下文
 * @source:192-197 setShallowSuspenseListContext
 */
SuspenseContext setShallowSuspenseListContext(
  SuspenseContext parentContext,
  ShallowSuspenseContext shallowContext
);

/**
 * 推送 SuspenseList 上下文
 * @source:199-203 pushSuspenseListContext
 */
void pushSuspenseListContext(
  FiberRef fiber,
  SuspenseContext newContext
);

/**
 * 弹出 SuspenseList 上下文
 * @source:205-207 popSuspenseListContext
 */
void popSuspenseListContext(FiberRef fiber);


// Suspense Handler 函数
// @source:42-140


/**
 * 获取当前 Suspense handler
 * @source:148-150 getSuspenseHandler
 */
FiberRef getSuspenseHandler();

/**
 * 弹出 Suspense handler
 * @source:152-159 popSuspenseHandler
 */
void popSuspenseHandler(FiberRef fiber);

/**
 * 重用堆栈上的 Suspense handler
 * @source:143-146 reuseSuspenseHandlerOnStack
 */
void reuseSuspenseHandlerOnStack(FiberRef fiber);

/**
 * 检查当前树是否隐藏
 * @source ReactFiberHiddenContext.js
 */
bool isCurrentTreeHidden();

/**
 * 推送主树 Suspense handler
 * @source:42-85 pushPrimaryTreeSuspenseHandler
 */
void pushPrimaryTreeSuspenseHandler(FiberRef handler);

/**
 * 推送 fallback 树 Suspense handler
 * @source:87-91 pushFallbackTreeSuspenseHandler
 */
void pushFallbackTreeSuspenseHandler(FiberRef fiber);

/**
 * 推送 dehydrated Activity Suspense handler
 * @source:93-105 pushDehydratedActivitySuspenseHandler
 */
void pushDehydratedActivitySuspenseHandler(FiberRef fiber);

/**
 * 推送 Offscreen Suspense handler
 * @source:107-128 pushOffscreenSuspenseHandler
 */
void pushOffscreenSuspenseHandler(FiberRef fiber);

} // namespace react::reconciler
