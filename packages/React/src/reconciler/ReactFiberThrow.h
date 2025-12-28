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

// =============================================================================
// Update 类型 (使用 ClassUpdateQueue 中的定义)
// @source ReactFiberClassUpdateQueue.js
// =============================================================================

// 使用 ClassUpdate 的别名
template<typename State>
using FiberUpdate = ClassUpdate<State>;

template<typename State>
using FiberUpdateRef = std::shared_ptr<FiberUpdate<State>>;

/**
 * 创建更新
 * @source ReactFiberClassUpdateQueue.js createUpdate
 */
template<typename State>
inline FiberUpdateRef<State> createFiberUpdate(Lane lane) {
  auto update = std::make_shared<FiberUpdate<State>>();
  update->lane = lane;
  return update;
}

// =============================================================================
// 错误更新创建函数
// @source:88-105 createRootErrorUpdate
// =============================================================================

/**
 * 创建根错误更新
 * 当根节点发生未捕获的错误时使用
 */
inline FiberUpdateRef<std::any> createRootErrorUpdate(
  FiberRootRef root,
  ErrorCapturedValueRef errorInfo,
  Lane lane
) {
  auto update = createFiberUpdate<std::any>(lane);
  update->tag = UpdateTag::CaptureUpdate;
  
  // 通过渲染 null 来卸载根节点
  update->payload = std::any{}; // element: null
  
  update->callback = [root, errorInfo]() {
    // 记录未捕获的错误
    if (errorInfo && errorInfo->message) {
      // 简化实现：在实际情况下会调用 logUncaughtError
    }
  };
  
  return update;
}

/**
 * 创建类组件错误更新
 * @source:107-109 createClassErrorUpdate
 */
inline FiberUpdateRef<std::any> createClassErrorUpdate(Lane lane) {
  auto update = createFiberUpdate<std::any>(lane);
  update->tag = UpdateTag::CaptureUpdate;
  return update;
}

// =============================================================================
// Suspense 边界标记
// @source:236-310 markSuspenseBoundaryShouldCapture
// =============================================================================

/**
 * 标记 Suspense 边界应该捕获
 * 当组件 suspend 时，标记最近的 Suspense 边界来显示 fallback
 */
inline FiberRef markSuspenseBoundaryShouldCapture(
  FiberRef suspenseBoundary,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  FiberRootRef root,
  Lanes rootRenderLanes
) {
  // 检查是否是遗留模式
  bool isLegacyMode = (suspenseBoundary->mode & ConcurrentMode) == NoMode;
  
  if (isLegacyMode) {
    // 遗留模式 Suspense
    if (suspenseBoundary == returnFiber) {
      // 特殊情况：suspended 发生在 Suspense 边界的内部 Offscreen wrapper
      suspenseBoundary->flags |= ShouldCapture;
    } else {
      suspenseBoundary->flags |= DidCapture;
      sourceFiber->flags |= ForceUpdateForLegacySuspense;
      
      // 移除生命周期效果标签
      sourceFiber->flags &= ~(LifecycleEffectMask | Incomplete);
      
      if (sourceFiber->tag == ClassComponent) {
        auto currentSourceFiber = sourceFiber->alternate.lock();
        if (!currentSourceFiber) {
          // 新挂载，改变 tag
          sourceFiber->tag = IncompleteClassComponent;
        }
      }
    }
  } else {
    // 并发模式
    suspenseBoundary->flags |= ShouldCapture;
  }
  
  return suspenseBoundary;
}

// =============================================================================
// 重置 suspended 组件
// @source:199-234 resetSuspendedComponent
// =============================================================================

/**
 * 重置 suspended 组件的状态
 */
inline void resetSuspendedComponent(FiberRef sourceFiber, Lanes rootRenderLanes) {
  auto currentSourceFiber = sourceFiber->alternate.lock();
  
  // 遗留模式 Suspense 特殊处理
  WorkTag tag = sourceFiber->tag;
  bool isLegacyMode = (sourceFiber->mode & ConcurrentMode) == NoMode;
  
  if (isLegacyMode && 
      (tag == FunctionComponent || tag == ForwardRef || tag == SimpleMemoComponent)) {
    if (currentSourceFiber) {
      sourceFiber->updateQueue = currentSourceFiber->updateQueue;
      sourceFiber->memoizedState = currentSourceFiber->memoizedState;
      sourceFiber->lanes = currentSourceFiber->lanes;
    } else {
      sourceFiber->updateQueue = std::any{};
      sourceFiber->memoizedState = std::any{};
    }
  }
}

// =============================================================================
// 抛出异常处理
// @source:400-600 throwException
// =============================================================================

/**
 * 异常类型
 */
enum class ThrownExceptionType {
  Error,           // 普通错误
  Suspense,        // Suspense (thenable)
  SuspenseyCommit, // Suspense commit
  Postpone         // 延迟渲染
};

/**
 * 抛出的异常信息
 */
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
inline bool isThenable(const std::any& value) {
  if (!value.has_value()) {
    return false;
  }
  
  // 尝试检查是否是我们的 Thenable 类型
  try {
    auto* thenable = std::any_cast<std::shared_ptr<Thenable<std::any>>>(&value);
    return thenable != nullptr;
  } catch (...) {
    return false;
  }
}

/**
 * 处理抛出的异常
 * 
 * 这是错误边界和 Suspense 的核心逻辑
 * 
 * @source:400-600 throwException
 */
inline void throwException(
  FiberRootRef root,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  std::any value,
  Lanes rootRenderLanes
) {
  // 标记 fiber 为 incomplete
  sourceFiber->flags |= Incomplete;
  
  // 检查是否是 Suspense 异常
  if (isThenable(value)) {
    // 这是 Suspense
    resetSuspendedComponent(sourceFiber, rootRenderLanes);
    
    // 查找最近的 Suspense handler
    FiberRef suspenseHandler = getSuspenseHandler();
    
    if (suspenseHandler) {
      markSuspenseBoundaryShouldCapture(
        suspenseHandler,
        returnFiber,
        sourceFiber,
        root,
        rootRenderLanes
      );
    }
    
    return;
  }
  
  // 这是一个错误 - 查找错误边界
  auto capturedValue = createCapturedValueFromError(
    std::make_exception_ptr(std::runtime_error("Render error")),
    std::nullopt
  );
  
  // 向上遍历查找错误边界
  FiberRef workInProgress = returnFiber;
  
  while (workInProgress) {
    switch (workInProgress->tag) {
      case HostRoot: {
        // 到达根节点 - 创建根错误更新
        auto errorUpdate = createRootErrorUpdate(root, capturedValue, SyncLane);
        // 在实际实现中，这会入队更新
        workInProgress->flags |= ShouldCapture;
        return;
      }
      
      case ClassComponent: {
        // 检查是否是错误边界
        // 在实际实现中，检查 getDerivedStateFromError 或 componentDidCatch
        auto errorUpdate = createClassErrorUpdate(SyncLane);
        workInProgress->flags |= ShouldCapture;
        return;
      }
      
      default:
        break;
    }
    
    workInProgress = workInProgress->return_.lock();
  }
}

// =============================================================================
// 错误恢复
// =============================================================================

/**
 * 标记遗留错误边界失败
 */
inline std::set<FiberRef> legacyErrorBoundariesThatAlreadyFailed;

inline void markLegacyErrorBoundaryAsFailed(FiberRef fiber) {
  legacyErrorBoundariesThatAlreadyFailed.insert(fiber);
}

inline bool isAlreadyFailedLegacyErrorBoundary(FiberRef fiber) {
  return legacyErrorBoundariesThatAlreadyFailed.count(fiber) > 0;
}

inline void clearLegacyErrorBoundaries() {
  legacyErrorBoundariesThatAlreadyFailed.clear();
}

} // namespace react::reconciler
