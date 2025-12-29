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

namespace react {
class ReactHostRuntime;
} // namespace react

namespace react::reconciler {


// Effect 实例结构
// @source ReactFiberHooks.js


struct EffectInstance {
  std::function<void()> destroy = nullptr;
};


// Effect 结构
// @source ReactFiberHooks.js


struct Effect {
  HookFlags tag = HookNoFlags;
  std::function<std::function<void()>()> create = nullptr;
  EffectInstance inst;
  std::vector<std::any> deps;
  std::shared_ptr<Effect> next = nullptr;
};

using EffectRef = std::shared_ptr<Effect>;


// Function Component Update Queue
// @source ReactFiberHooks.js


struct FunctionComponentUpdateQueue {
  EffectRef lastEffect = nullptr;
  std::optional<std::any> events;     // useEffectEvent 的事件
  std::optional<std::any> stores;     // useSyncExternalStore 的 stores
  std::optional<std::any> memoCache;  // useMemoCache
};

using FunctionComponentUpdateQueueRef = std::shared_ptr<FunctionComponentUpdateQueue>;


// 错误处理


using CaptureCommitPhaseErrorFn = std::function<void(
  FiberRef fiber,
  FiberRef nearestMountedAncestor,
  std::exception_ptr error
)>;

/**
 * 安全调用销毁函数
 */
void safelyCallDestroy(
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError,
  FiberRef currentlyRenderingFiber,
  FiberRef nearestMountedAncestor,
  std::function<void()> destroy);

// Hook Effect 提交函数
// 提交 Hook Effect 列表 - Mount
void commitHookEffectListMount(
  HookFlags flags,
  const FiberRef& finishedWork,
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError = nullptr);

void commitHookEffectListMount(
  react::ReactHostRuntime& hostRuntime,
  HookFlags flags,
  const FiberRef& finishedWork);

// 提交 Hook Effect 列表 - Unmount
void commitHookEffectListUnmount(
  HookFlags flags,
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError = nullptr);

void commitHookEffectListUnmount(
  react::ReactHostRuntime& hostRuntime,
  HookFlags flags,
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor);


// Hook Layout Effect 函数
// 提交 Hook Layout Effects
void commitHookLayoutEffects(
  const FiberRef& finishedWork,
  HookFlags hookFlags
);

// 提交 Hook Layout Unmount Effects
void commitHookLayoutUnmountEffects(
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  HookFlags hookFlags);


// Hook Passive Effect 函数
// 提交 Hook Passive Mount Effects
void commitHookPassiveMountEffects(
  const FiberRef& finishedWork,
  HookFlags hookFlags);

// 提交 Hook Passive Unmount Effects
void commitHookPassiveUnmountEffects(
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  HookFlags hookFlags);


// Ref 处理函数
// 安全地附加 Ref
void safelyAttachRef(const FiberRef& fiber, const FiberRef& nearestMountedAncestor);

/**
 * 安全地分离 Ref
 * @source:750+ safelyDetachRef
 */
void safelyDetachRef(FiberRef fiber, FiberRef nearestMountedAncestor);


// Class 生命周期函数
// @source ReactFiberCommitEffects.js:332-470


/**
 * 安全调用 componentWillUnmount
 * @source:332-360 safelyCallComponentWillUnmount
 */
void safelyCallComponentWillUnmount(
  FiberRef current,
  FiberRef nearestMountedAncestor,
  std::any instance
);


// Profiler 函数
// @source ReactFiberCommitEffects.js:900+


/**
 * 提交 Profiler 更新
 * @source:900+ commitProfilerUpdate
 */
void commitProfilerUpdate(
  FiberRef finishedWork,
  FiberRef current
);

/**
 * 提交 Profiler Post Commit
 * @source:950+ commitProfilerPostCommit
 */
void commitProfilerPostCommit(
  FiberRef finishedWork,
  FiberRef current,
  double commitStartTime,
  Lanes lanes
);


// Root Callbacks
// @source ReactFiberCommitEffects.js:1000+


/**
 * 提交 Root Callbacks
 * @source:1000+ commitRootCallbacks
 */
void commitRootCallbacks(FiberRootRef root);


// Class Callbacks
// @source ReactFiberCommitEffects.js:470-550


/**
 * 提交 Class Callbacks
 */
void commitClassCallbacks(FiberRef finishedWork);

/**
 * 提交 Class Hidden Callbacks
 */
void commitClassHiddenCallbacks(FiberRef finishedWork);

/**
 * 提交 Class Snapshot
 */
void commitClassSnapshot(FiberRef finishedWork, FiberRef current);

} // namespace react::reconciler
