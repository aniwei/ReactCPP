/**
 * React Fiber Throw
 * 
 * 处理渲染过程中抛出的错误和 Suspense
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberThrow.js
 */

#pragma once

#include <memory>
#include <functional>
#include <any>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "ReactCapturedValue.h"
#include "ReactFiberSuspenseContext.h"
#include "ReactFiberSuspenseComponent.h"
#include "ReactFiberThenable.h"
#include "ReactFiberClassUpdateQueue.h"

namespace react::reconciler {

// Update 类型（用于错误边界 / Suspense 流程中的更新）。
// 之前这里复用了 ClassUpdateQueue 的模板版本；现在 ClassUpdateQueue 对齐为非模板结构，
// 因此在 throw 模块内单独定义一个轻量的 FiberUpdate 模板。
template<typename State>
struct FiberUpdate {
  Lane lane{NoLane};
  UpdateTag tag{UpdateTag::UpdateState};
  facebook::jsi::Value payload = facebook::jsi::Value::undefined();
  std::function<void()> callback;
  std::shared_ptr<FiberUpdate<State>> next;
};

template<typename State>
using FiberUpdateRef = std::shared_ptr<FiberUpdate<State>>;

// 创建更新
template<typename State>
inline FiberUpdateRef<State> createFiberUpdate(Lane lane) {
  auto update = std::make_shared<FiberUpdate<State>>();
  update->lane = lane;
  return update;
}

// 创建根错误更新
FiberUpdateRef<std::any> createRootErrorUpdate(
  FiberRootRef root,
  ErrorCapturedValueRef errorInfo,
  Lane lane);

// 创建类组件错误更新
FiberUpdateRef<std::any> createClassErrorUpdate(Lane lane);

// Suspense 边界标记

/**
 * 标记 Suspense 边界应该捕获
 * 当组件 suspend 时，标记最近的 Suspense 边界来显示 fallback
 */
FiberRef markSuspenseBoundaryShouldCapture(
  FiberRef suspenseBoundary,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  FiberRootRef root,
  Lanes rootRenderLanes);

// 重置 suspended 组件
void resetSuspendedComponent(FiberRef sourceFiber, Lanes rootRenderLanes);

// 抛出异常处理
enum class ThrownExceptionType {
  Error,           // 普通错误
  Suspense,        // Suspense (thenable)
  SuspenseyCommit, // Suspense commit
  Postpone         // 延迟渲染
};
struct ThrownException {
  ThrownExceptionType type = ThrownExceptionType::Error;
  std::any value;                          // 错误值或 thenable
  ErrorCapturedValueRef capturedValue;     // 捕获的错误信息
  FiberWeakRef source;                     // 抛出异常的 Fiber
};

using ThrownExceptionRef = std::shared_ptr<ThrownException>;

/**
 * 检查值是否是 Thenable (Promise-like)
 */
bool isThenable(const std::any& value);

/**
 * 处理抛出的异常
 * 
 * 这是错误边界和 Suspense 的核心逻辑
 * 
 * @source:400-600 throwException
 */
void throwException(
  FiberRootRef root,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  std::any value,
  Lanes rootRenderLanes
);


// 标记遗留错误边界失败
void markLegacyErrorBoundaryAsFailed(FiberRef fiber);
bool isAlreadyFailedLegacyErrorBoundary(FiberRef fiber);
void clearLegacyErrorBoundaries();

} // namespace react::reconciler
