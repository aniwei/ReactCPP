#include "ReactReconciler/ReactFiberCompleteWork.h"

#include "ReactReconciler/ReactFiberCacheComponentState.h"
#include "ReactReconciler/ReactFiberHiddenContext.h"
#include "ReactReconciler/ReactFiberHostRootState.h"
#include "ReactReconciler/ReactFiberSuspenseComponent.h"
#include "ReactReconciler/ReactFiberTreeContext.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactFeatureFlags.h"

#include "jsi/jsi.h"

#include <memory>

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

namespace react {
namespace {

constexpr bool kSupportsMutation = true;
constexpr bool kSupportsPersistence = false;

void markCloned(FiberNode& workInProgress) {
  if (kSupportsPersistence && enablePersistedModeClonedFlag) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Cloned);
  }
}

void appendAllChildren(
    ReactRuntime& runtime,
    FiberNode& workInProgress,
    const hostconfig::HostInstance& parent) {
  if (!kSupportsMutation || !parent) {
    return;
  }

  FiberNode* node = workInProgress.child;
  while (node != nullptr) {
    const WorkTag tag = node->tag;
    if (tag == WorkTag::HostComponent || tag == WorkTag::HostText) {
      auto childInstance = getHostInstance(*node);
      if (childInstance) {
        hostconfig::appendInitialChild(runtime, parent, childInstance);
      }
    } else if (tag == WorkTag::HostPortal || tag == WorkTag::HostSingleton) {
      // Portals and singletons manage their own placement in commit.
    } else if (node->child != nullptr) {
      node->child->returnFiber = node;
      node = node->child;
      continue;
    }

    if (node == &workInProgress) {
      return;
    }

    while (node->sibling == nullptr) {
      FiberNode* parentFiber = node->returnFiber;
      if (parentFiber == nullptr || parentFiber == &workInProgress) {
        return;
      }
      node = parentFiber;
    }

    node->sibling->returnFiber = node->returnFiber;
    node = node->sibling;
  }
}

} // namespace

FiberFlags bubbleProperties(FiberNode& completedWork) {
  FiberFlags subtreeFlags = NoFlags;
  Lanes childLanes = NoLanes;

  for (FiberNode* child = completedWork.child; child != nullptr; child = child->sibling) {
    childLanes = mergeLanes(childLanes, child->lanes);
    childLanes = mergeLanes(childLanes, child->childLanes);

    const FiberFlags childFlags = static_cast<FiberFlags>(child->flags & static_cast<FiberFlags>(~StaticMask));
    subtreeFlags = static_cast<FiberFlags>(subtreeFlags | childFlags | child->subtreeFlags);
  }

  completedWork.childLanes = childLanes;
  const FiberFlags staticSubtreeFlags = completedWork.subtreeFlags & StaticMask;
  completedWork.subtreeFlags = static_cast<FiberFlags>(staticSubtreeFlags | subtreeFlags);
  return subtreeFlags;
}

FiberNode* completeWork(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes entangledRenderLanes) {
  if (workInProgress == nullptr) {
    return nullptr;
  }

  popTreeContext(runtime, *workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* fiberRoot = static_cast<FiberRoot*>(workInProgress->stateNode);
      if (fiberRoot == nullptr) {
        bubbleProperties(*workInProgress);
        break;
      }

      if (enableTransitionTracing) {
        if (!getWorkInProgressTransitions(runtime).empty()) {
          workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Passive);
        }
        popRootMarkerInstance(*workInProgress);
      }

      popCacheProvider(*workInProgress, nullptr);

      popRootTransition(runtime, *workInProgress, *fiberRoot, entangledRenderLanes);
      popHostContainer(runtime, *workInProgress);
      popTopLevelLegacyContextObject(runtime, *workInProgress);

      if (fiberRoot->pendingContext != nullptr) {
        fiberRoot->context = fiberRoot->pendingContext;
        fiberRoot->pendingContext = nullptr;
      }

      const bool isInitialRender = current == nullptr || current->child == nullptr;
      if (isInitialRender) {
        const bool wasHydrated = popHydrationState(runtime, *workInProgress);
        if (wasHydrated) {
          emitPendingHydrationWarningsInternal(runtime);
          fiberRoot->hostRootState.isDehydrated = false;
          markUpdate(*workInProgress);
        } else if (current != nullptr) {
          auto* prevHostRootState = static_cast<HostRootMemoizedState*>(current->memoizedState);
          const bool prevWasDehydrated = prevHostRootState != nullptr ? prevHostRootState->isDehydrated : fiberRoot->hostRootState.isDehydrated;
          const bool wasForcedClientRender = (workInProgress->flags & ForceClientRender) != 0;
          if (!prevWasDehydrated || wasForcedClientRender) {
            workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Snapshot);
            upgradeHydrationErrorsToRecoverable(runtime);
          }
        }
      }

      updateHostContainer(current, *workInProgress);
      bubbleProperties(*workInProgress);

      if (enableTransitionTracing) {
        if ((workInProgress->subtreeFlags & Visibility) != NoFlags) {
          workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Passive);
        }
      }
      break;
    }
    case WorkTag::HostHoistable: {
      const std::string type = getFiberType(jsRuntime, *workInProgress);
      Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->pendingProps);
      if (nextPropsValue.isUndefined() || nextPropsValue.isNull()) {
        nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->memoizedProps);
      }
      Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

      if (current != nullptr && current->stateNode != nullptr) {
        Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
        Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);
        if (!payload.isUndefined()) {
          storeHostUpdatePayload(jsRuntime, *workInProgress, payload);
          markUpdate(*workInProgress);
        } else {
          clearHostUpdatePayload(*workInProgress);
        }

        if (workInProgress->stateNode == nullptr) {
          auto instance = getHostInstance(*current);
          if (instance) {
            setHostInstance(*workInProgress, instance);
          }
        }
      } else {
        if (workInProgress->stateNode == nullptr && !type.empty()) {
          auto instance = hostconfig::createHoistableInstance(runtime, jsRuntime, type, nextPropsObject);
          setHostInstance(*workInProgress, instance);
        }

        clearHostUpdatePayload(*workInProgress);
        if (current == nullptr) {
          markCloned(*workInProgress);
        }
      }

      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::HostSingleton: {
      popHostContext(runtime, *workInProgress);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::HostComponent: {
      popHostContext(runtime, *workInProgress);

      const std::string type = getFiberType(jsRuntime, *workInProgress);
      facebook::jsi::Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->pendingProps);
      if (nextPropsValue.isUndefined() || nextPropsValue.isNull()) {
        nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->memoizedProps);
      }
      facebook::jsi::Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

      if (current != nullptr && current->stateNode != nullptr) {
        facebook::jsi::Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
        facebook::jsi::Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);
        if (!payload.isUndefined()) {
          storeHostUpdatePayload(jsRuntime, *workInProgress, payload);
          markUpdate(*workInProgress);
        } else {
          clearHostUpdatePayload(*workInProgress);
        }

        if (workInProgress->stateNode == nullptr) {
          auto instance = getHostInstance(*current);
          if (instance) {
            setHostInstance(*workInProgress, instance);
          }
        }

        bubbleProperties(*workInProgress);
        break;
      }

      if (type.empty()) {
        bubbleProperties(*workInProgress);
        break;
      }

      auto instance = hostconfig::createInstance(runtime, jsRuntime, type, nextPropsObject);
      setHostInstance(*workInProgress, instance);

      markCloned(*workInProgress);
      appendAllChildren(runtime, *workInProgress, instance);

      if (hostconfig::finalizeInitialChildren(runtime, jsRuntime, instance, type, nextPropsObject)) {
        markUpdate(*workInProgress);
      }

      clearHostUpdatePayload(*workInProgress);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::HostText: {
      facebook::jsi::Value nextTextValue = cloneJsiValue(jsRuntime, workInProgress->memoizedProps);
      if (nextTextValue.isUndefined()) {
        nextTextValue = cloneJsiValue(jsRuntime, workInProgress->pendingProps);
      }
      const std::string nextText = valueToString(jsRuntime, nextTextValue);

      if (current != nullptr && current->stateNode != nullptr) {
        facebook::jsi::Value prevTextValue = cloneJsiValue(jsRuntime, current->memoizedProps);
        const std::string prevText = valueToString(jsRuntime, prevTextValue);
        if (nextText != prevText) {
          markUpdate(*workInProgress);
        }

        if (workInProgress->stateNode == nullptr) {
          auto instance = getHostInstance(*current);
          if (instance) {
            setHostInstance(*workInProgress, instance);
          }
        }

      } else {
        markCloned(*workInProgress);
        auto textInstance = hostconfig::createTextInstance(runtime, jsRuntime, nextText);
        setHostInstance(*workInProgress, textInstance);
      }

      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::Fragment:
    case WorkTag::Mode:
    case WorkTag::ContextProvider:
    case WorkTag::ContextConsumer:
    case WorkTag::Profiler:
    case WorkTag::SuspenseComponent:
      bubbleProperties(*workInProgress);
      break;
    case WorkTag::OffscreenComponent:
    case WorkTag::LegacyHiddenComponent: {
      popSuspenseHandler(*workInProgress);
      popHiddenContext(runtime, *workInProgress);
      popTransition(*workInProgress, current);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::HostPortal: {
      popHostContainer(runtime, *workInProgress);
      updateHostContainer(current, *workInProgress);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::CacheComponent: {
      auto* cacheState = static_cast<CacheComponentState*>(workInProgress->memoizedState);
      void* cache = cacheState != nullptr ? cacheState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::MemoComponent:
    case WorkTag::ForwardRef:
    case WorkTag::SimpleMemoComponent:
    case WorkTag::FunctionComponent:
    case WorkTag::ClassComponent:
    default:
      bubbleProperties(*workInProgress);
      break;
  }

  if (entangledRenderLanes != NoLanes) {
    const Lanes entangledChildren = intersectLanes(entangledRenderLanes, workInProgress->childLanes);
    if (entangledChildren != NoLanes) {
      workInProgress->childLanes = mergeLanes(workInProgress->childLanes, entangledChildren);
      workInProgress->lanes = mergeLanes(workInProgress->lanes, entangledChildren);
    }
  }

  return nullptr;
}

} // namespace react
