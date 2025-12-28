/**
 * React Fiber Commit Effects
 * 
 * 包含 Commit 阶段中处理各种效果的函数
 * 包括 Hook 效果、Class 生命周期、Ref 处理等
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberCommitEffects.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>
#include <vector>

#include "ReactFiber.h"
#include "ReactFiberFlags.h"
#include "ReactHookEffectTags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"

namespace react::reconciler {

// =============================================================================
// Effect 实例结构
// @source ReactFiberHooks.js
// =============================================================================

struct EffectInstance {
  std::function<void()> destroy = nullptr;
};

// =============================================================================
// Effect 结构
// @source ReactFiberHooks.js
// =============================================================================

struct Effect {
  HookFlags tag = HookNoFlags;
  std::function<std::function<void()>()> create = nullptr;
  EffectInstance inst;
  std::vector<std::any> deps;
  std::shared_ptr<Effect> next = nullptr;
};

using EffectRef = std::shared_ptr<Effect>;

// =============================================================================
// Function Component Update Queue
// @source ReactFiberHooks.js
// =============================================================================

struct FunctionComponentUpdateQueue {
  EffectRef lastEffect = nullptr;
  std::optional<std::any> events;     // useEffectEvent 的事件
  std::optional<std::any> stores;     // useSyncExternalStore 的 stores
  std::optional<std::any> memoCache;  // useMemoCache
};

using FunctionComponentUpdateQueueRef = std::shared_ptr<FunctionComponentUpdateQueue>;

// =============================================================================
// 错误处理
// =============================================================================

using CaptureCommitPhaseErrorFn = std::function<void(
  FiberRef fiber,
  FiberRef nearestMountedAncestor,
  std::exception_ptr error
)>;

// 全局错误捕获函数 (由 WorkLoop 设置)
inline CaptureCommitPhaseErrorFn captureCommitPhaseError = nullptr;

/**
 * 安全调用销毁函数
 */
inline void safelyCallDestroy(
  FiberRef currentlyRenderingFiber,
  FiberRef nearestMountedAncestor,
  std::function<void()> destroy
) {
  if (!destroy) return;
  
  try {
    destroy();
  } catch (...) {
    if (captureCommitPhaseError) {
      captureCommitPhaseError(
        currentlyRenderingFiber,
        nearestMountedAncestor,
        std::current_exception()
      );
    }
  }
}

// =============================================================================
// Hook Effect 提交函数
// @source ReactFiberCommitEffects.js:87-130
// =============================================================================

/**
 * 提交 Hook Effect 列表 - Mount
 * @source:131-191 commitHookEffectListMount
 */
inline void commitHookEffectListMount(
  HookFlags flags,
  FiberRef finishedWork
) {
  if (!finishedWork) return;
  
  // 获取 updateQueue
  auto* updateQueue = std::any_cast<FunctionComponentUpdateQueue>(
    &finishedWork->updateQueue
  );
  
  if (!updateQueue) return;
  
  auto lastEffect = updateQueue->lastEffect;
  if (!lastEffect) return;
  
  auto firstEffect = lastEffect->next;
  auto effect = firstEffect;
  
  do {
    if ((effect->tag & flags) == flags) {
      // Mount
      if (effect->create) {
        try {
          auto destroy = effect->create();
          effect->inst.destroy = destroy;
        } catch (...) {
          if (captureCommitPhaseError) {
            auto returnFiber = finishedWork->return_.lock();
            captureCommitPhaseError(
              finishedWork, 
              returnFiber, 
              std::current_exception()
            );
          }
        }
      }
    }
    effect = effect->next;
  } while (effect && effect != firstEffect);
}

/**
 * 提交 Hook Effect 列表 - Unmount
 * @source:248-301 commitHookEffectListUnmount
 */
inline void commitHookEffectListUnmount(
  HookFlags flags,
  FiberRef finishedWork,
  FiberRef nearestMountedAncestor
) {
  if (!finishedWork) return;
  
  auto* updateQueue = std::any_cast<FunctionComponentUpdateQueue>(
    &finishedWork->updateQueue
  );
  
  if (!updateQueue) return;
  
  auto lastEffect = updateQueue->lastEffect;
  if (!lastEffect) return;
  
  auto firstEffect = lastEffect->next;
  auto effect = firstEffect;
  
  do {
    if ((effect->tag & flags) == flags) {
      // Unmount
      auto destroy = effect->inst.destroy;
      if (destroy) {
        effect->inst.destroy = nullptr;
        safelyCallDestroy(finishedWork, nearestMountedAncestor, destroy);
      }
    }
    effect = effect->next;
  } while (effect && effect != firstEffect);
}

// =============================================================================
// Hook Layout Effect 函数
// @source ReactFiberCommitEffects.js:87-130
// =============================================================================

/**
 * 提交 Hook Layout Effects
 * @source:87-100 commitHookLayoutEffects
 */
inline void commitHookLayoutEffects(
  FiberRef finishedWork,
  HookFlags hookFlags
) {
  commitHookEffectListMount(hookFlags, finishedWork);
}

/**
 * 提交 Hook Layout Unmount Effects
 * @source:102-130 commitHookLayoutUnmountEffects
 */
inline void commitHookLayoutUnmountEffects(
  FiberRef finishedWork,
  FiberRef nearestMountedAncestor,
  HookFlags hookFlags
) {
  commitHookEffectListUnmount(hookFlags, finishedWork, nearestMountedAncestor);
}

// =============================================================================
// Hook Passive Effect 函数
// @source ReactFiberCommitEffects.js:302-330
// =============================================================================

/**
 * 提交 Hook Passive Mount Effects
 * @source:302-312 commitHookPassiveMountEffects
 */
inline void commitHookPassiveMountEffects(
  FiberRef finishedWork,
  HookFlags hookFlags
) {
  commitHookEffectListMount(hookFlags, finishedWork);
}

/**
 * 提交 Hook Passive Unmount Effects
 * @source:314-330 commitHookPassiveUnmountEffects
 */
inline void commitHookPassiveUnmountEffects(
  FiberRef finishedWork,
  FiberRef nearestMountedAncestor,
  HookFlags hookFlags
) {
  commitHookEffectListUnmount(hookFlags, finishedWork, nearestMountedAncestor);
}

// =============================================================================
// Ref 处理函数
// @source ReactFiberCommitEffects.js:700-800
// =============================================================================

/**
 * 安全地附加 Ref
 * @source:700+ safelyAttachRef
 */
inline void safelyAttachRef(FiberRef fiber, FiberRef nearestMountedAncestor) {
  if (!fiber) return;
  
  // 简化实现：设置 ref 的 current
  // 完整实现需要处理 callback ref 和 createRef
  auto& ref = fiber->ref;
  if (ref.isNull() || ref.isUndefined()) {
    return;
  }
  
  // TODO: 完整的 ref 处理逻辑
}

/**
 * 安全地分离 Ref
 * @source:750+ safelyDetachRef
 */
inline void safelyDetachRef(FiberRef fiber, FiberRef nearestMountedAncestor) {
  if (!fiber) return;
  
  auto& ref = fiber->ref;
  if (ref.isNull() || ref.isUndefined()) {
    return;
  }
  
  // TODO: 完整的 ref 分离逻辑
}

// =============================================================================
// Class 生命周期函数
// @source ReactFiberCommitEffects.js:332-470
// =============================================================================

/**
 * 安全调用 componentWillUnmount
 * @source:332-360 safelyCallComponentWillUnmount
 */
inline void safelyCallComponentWillUnmount(
  FiberRef current,
  FiberRef nearestMountedAncestor,
  std::any instance
) {
  // 简化实现
  // 完整实现需要调用 instance.componentWillUnmount()
}

// =============================================================================
// Profiler 函数
// @source ReactFiberCommitEffects.js:900+
// =============================================================================

/**
 * 提交 Profiler 更新
 * @source:900+ commitProfilerUpdate
 */
inline void commitProfilerUpdate(
  FiberRef finishedWork,
  FiberRef current
) {
  // Profiler 更新逻辑
  // 在非 Profiler 模式下为空实现
}

/**
 * 提交 Profiler Post Commit
 * @source:950+ commitProfilerPostCommit
 */
inline void commitProfilerPostCommit(
  FiberRef finishedWork,
  FiberRef current,
  double commitStartTime,
  Lanes lanes
) {
  // Profiler post-commit 逻辑
}

// =============================================================================
// Root Callbacks
// @source ReactFiberCommitEffects.js:1000+
// =============================================================================

/**
 * 提交 Root Callbacks
 * @source:1000+ commitRootCallbacks
 */
inline void commitRootCallbacks(FiberRootRef root) {
  // 处理 root 级别的回调
}

// =============================================================================
// Class Callbacks
// @source ReactFiberCommitEffects.js:470-550
// =============================================================================

/**
 * 提交 Class Callbacks
 */
inline void commitClassCallbacks(FiberRef finishedWork) {
  // Class 组件回调处理
}

/**
 * 提交 Class Hidden Callbacks
 */
inline void commitClassHiddenCallbacks(FiberRef finishedWork) {
  // 隐藏的 Class 组件回调处理
}

/**
 * 提交 Class Snapshot
 */
inline void commitClassSnapshot(FiberRef finishedWork, FiberRef current) {
  // getSnapshotBeforeUpdate 处理
}

} // namespace react::reconciler
