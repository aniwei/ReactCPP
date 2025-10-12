#include "ReactReconciler/ReactFiberUnwindWork.h"

#include "ReactReconciler/ReactFiberCacheComponentState.h"
#include "ReactReconciler/ReactFiberCompleteWork.h"
#include "ReactReconciler/ReactFiberHiddenContext.h"
#include "ReactReconciler/ReactFiberHostRootState.h"
#include "ReactReconciler/ReactFiberHydrationContext.h"
#include "ReactReconciler/ReactFiberSuspenseComponent.h"
#include "ReactReconciler/ReactFiberSuspenseContext.h"
#include "ReactReconciler/ReactFiberTreeContext.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"
#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "shared/ReactFeatureFlags.h"

namespace react {
void resetSuspendedWorkLoopOnUnwind(ReactRuntime& runtime, FiberNode& fiber);

FiberNode* unwindWork(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode* workInProgress,
    Lanes entangledRenderLanes) {
  (void)entangledRenderLanes;

  if (workInProgress == nullptr) {
    return nullptr;
  }

  resetSuspendedWorkLoopOnUnwind(runtime, *workInProgress);
  popTreeContext(runtime, *workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* const root = static_cast<FiberRoot*>(workInProgress->stateNode);
      auto* const rootState = static_cast<HostRootMemoizedState*>(workInProgress->memoizedState);
      void* cache = rootState != nullptr ? rootState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);

      if (enableTransitionTracing) {
        popRootMarkerInstance(*workInProgress);
      }

      if (root != nullptr) {
        popRootTransition(runtime, *workInProgress, *root, getWorkInProgressRootRenderLanes(runtime));
        popHostContainer(runtime, *workInProgress);
      }

      popTopLevelLegacyContextObject(runtime, *workInProgress);

      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    case WorkTag::HostComponent:
    case WorkTag::HostHoistable:
    case WorkTag::HostSingleton: {
      popHostContext(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostPortal: {
      popHostContainer(runtime, *workInProgress);
      break;
    }
    case WorkTag::SuspenseComponent: {
      popSuspenseHandler(*workInProgress);
      auto* suspenseState = static_cast<SuspenseState*>(workInProgress->memoizedState);
      if (suspenseState != nullptr && suspenseState->dehydrated != nullptr) {
        resetHydrationState(runtime);
      }

      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    case WorkTag::SuspenseListComponent: {
      popSuspenseListContext(*workInProgress);
      break;
    }
    case WorkTag::OffscreenComponent:
    case WorkTag::LegacyHiddenComponent: {
      popSuspenseHandler(*workInProgress);
      popHiddenContext(runtime, *workInProgress);
      popTransition(*workInProgress, current);
      break;
    }
    case WorkTag::CacheComponent: {
      auto* cacheState = static_cast<CacheComponentState*>(workInProgress->memoizedState);
      void* cache = cacheState != nullptr ? cacheState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);
      break;
    }
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing && workInProgress->stateNode != nullptr) {
        popMarkerInstance(*workInProgress);
      }
      break;
    }
    case WorkTag::ClassComponent: {
      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    default:
      break;
  }

  workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Incomplete);
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->childLanes = NoLanes;
  workInProgress->deletions.clear();

  return workInProgress->returnFiber;
}

void unwindInterruptedWork(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode* workInProgress,
    Lanes renderLanes) {
  if (workInProgress == nullptr) {
    return;
  }

  popTreeContext(runtime, *workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* const root = static_cast<FiberRoot*>(workInProgress->stateNode);
      auto* const rootState = static_cast<HostRootMemoizedState*>(workInProgress->memoizedState);
      void* cache = rootState != nullptr ? rootState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);

      if (enableTransitionTracing) {
        popRootMarkerInstance(*workInProgress);
      }

      if (root != nullptr) {
        popRootTransition(runtime, *workInProgress, *root, renderLanes);
        popHostContainer(runtime, *workInProgress);
      }

      popTopLevelLegacyContextObject(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostComponent:
    case WorkTag::HostHoistable:
    case WorkTag::HostSingleton: {
      popHostContext(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostPortal: {
      popHostContainer(runtime, *workInProgress);
      break;
    }
    case WorkTag::SuspenseComponent: {
      popSuspenseHandler(*workInProgress);
      auto* suspenseState = static_cast<SuspenseState*>(workInProgress->memoizedState);
      if (suspenseState != nullptr && suspenseState->dehydrated != nullptr) {
        resetHydrationState(runtime);
      }
      break;
    }
    case WorkTag::ActivityComponent: {
      if (workInProgress->memoizedState != nullptr) {
        popSuspenseHandler(*workInProgress);
        resetHydrationState(runtime);
      }
      break;
    }
    case WorkTag::SuspenseListComponent: {
      popSuspenseListContext(*workInProgress);
      break;
    }
    case WorkTag::OffscreenComponent:
    case WorkTag::LegacyHiddenComponent: {
      popSuspenseHandler(*workInProgress);
      popHiddenContext(runtime, *workInProgress);
      popTransition(*workInProgress, current);
      break;
    }
    case WorkTag::CacheComponent: {
      auto* cacheState = static_cast<CacheComponentState*>(workInProgress->memoizedState);
      void* cache = cacheState != nullptr ? cacheState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);
      break;
    }
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing && workInProgress->stateNode != nullptr) {
        popMarkerInstance(*workInProgress);
      }
      break;
    }
    default:
      break;
  }
}

} // namespace react
