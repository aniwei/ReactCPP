#include "ReactFiberCommitEffects.h"

#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

void safelyCallDestroy(
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError,
  FiberRef currentlyRenderingFiber,
  FiberRef nearestMountedAncestor,
  std::function<void()> destroy
) {
  if (!destroy) return;

  try {
    destroy();
  } catch (...) {
    if (captureCommitPhaseError && *captureCommitPhaseError) {
      (*captureCommitPhaseError)(
        currentlyRenderingFiber,
        nearestMountedAncestor,
        std::current_exception());
    }
  }
}

void commitHookEffectListMount(
  HookFlags flags,
  const FiberRef& finishedWork,
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError
) {
  if (!finishedWork) return;

  auto* updateQueuePtr = std::get_if<std::shared_ptr<FunctionComponentUpdateQueue>>(
    &finishedWork->updateQueue
  );
  if (!updateQueuePtr || !*updateQueuePtr) return;

  auto& updateQueue = **updateQueuePtr;
  auto lastEffect = updateQueue.lastEffect;
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
          if (captureCommitPhaseError && *captureCommitPhaseError) {
            auto returnFiber = finishedWork->return_.lock();
            (*captureCommitPhaseError)(
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

void commitHookEffectListMount(
  react::ReactHostRuntime& hostRuntime,
  HookFlags flags,
  const FiberRef& finishedWork
) {
  commitHookEffectListMount(flags, finishedWork, hostRuntime.getCaptureCommitPhaseErrorPtr());
}

void commitHookEffectListUnmount(
  HookFlags flags,
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  const CaptureCommitPhaseErrorFn* captureCommitPhaseError
) {
  if (!finishedWork) return;

  auto* updateQueuePtr = std::get_if<std::shared_ptr<FunctionComponentUpdateQueue>>(
    &finishedWork->updateQueue
  );
  if (!updateQueuePtr || !*updateQueuePtr) return;

  auto& updateQueue = **updateQueuePtr;
  auto lastEffect = updateQueue.lastEffect;
  if (!lastEffect) return;

  auto firstEffect = lastEffect->next;
  auto effect = firstEffect;

  do {
    if ((effect->tag & flags) == flags) {
      // Unmount
      auto destroy = effect->inst.destroy;
      if (destroy) {
        effect->inst.destroy = nullptr;
        safelyCallDestroy(captureCommitPhaseError, finishedWork, nearestMountedAncestor, destroy);
      }
    }
    effect = effect->next;
  } while (effect && effect != firstEffect);
}

void commitHookEffectListUnmount(
  react::ReactHostRuntime& hostRuntime,
  HookFlags flags,
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor
) {
  commitHookEffectListUnmount(flags, finishedWork, nearestMountedAncestor, hostRuntime.getCaptureCommitPhaseErrorPtr());
}

void commitHookLayoutEffects(
  const FiberRef& finishedWork,
  HookFlags hookFlags
) {
  commitHookEffectListMount(hookFlags, finishedWork, nullptr);
}

void commitHookLayoutUnmountEffects(
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  HookFlags hookFlags
) {
  commitHookEffectListUnmount(hookFlags, finishedWork, nearestMountedAncestor, nullptr);
}

void commitHookPassiveMountEffects(
  const FiberRef& finishedWork,
  HookFlags hookFlags
) {
  commitHookEffectListMount(hookFlags, finishedWork, nullptr);
}

void commitHookPassiveUnmountEffects(
  const FiberRef& finishedWork,
  const FiberRef& nearestMountedAncestor,
  HookFlags hookFlags
) {
  commitHookEffectListUnmount(hookFlags, finishedWork, nearestMountedAncestor, nullptr);
}

void safelyAttachRef(FiberRef fiber, FiberRef /*nearestMountedAncestor*/) {
  if (!fiber) return;

  // 简化实现：设置 ref 的 current
  // 完整实现需要处理 callback ref 和 createRef
  auto& ref = fiber->ref;
  if (ref.isNull() || ref.isUndefined()) {
    return;
  }

  // TODO: 完整的 ref 处理逻辑
}

void safelyDetachRef(FiberRef fiber, FiberRef /*nearestMountedAncestor*/) {
  if (!fiber) return;

  auto& ref = fiber->ref;
  if (ref.isNull() || ref.isUndefined()) {
    return;
  }

  // TODO: 完整的 ref 分离逻辑
}

void safelyCallComponentWillUnmount(
  FiberRef /*current*/, 
  FiberRef /*nearestMountedAncestor*/, 
  std::any /*instance*/
) {
  // 简化实现
  // 完整实现需要调用 instance.componentWillUnmount()
}

void commitProfilerUpdate(
  FiberRef /*finishedWork*/,
  FiberRef /*current*/
) {
  // Profiler 更新逻辑
  // 在非 Profiler 模式下为空实现
}

void commitProfilerPostCommit(
  FiberRef /*finishedWork*/,
  FiberRef /*current*/,
  double /*commitStartTime*/,
  Lanes /*lanes*/
) {
  // Profiler post-commit 逻辑
}

void commitRootCallbacks(FiberRootRef /*root*/) {
  // 处理 root 级别的回调
}

void commitClassCallbacks(FiberRef /*finishedWork*/) {
  // Class 组件回调处理
}

void commitClassHiddenCallbacks(FiberRef /*finishedWork*/) {
  // 隐藏的 Class 组件回调处理
}

void commitClassSnapshot(FiberRef /*finishedWork*/, FiberRef /*current*/) {
  // getSnapshotBeforeUpdate 处理
}

} // namespace react::reconciler
