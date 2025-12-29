/**
 * React Fiber Unwind Work
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberUnwindWork.js
 */

#include "ReactFiberUnwindWork.h"

namespace react::reconciler {

void popHostContainer(const FiberRef& /*workInProgress*/) {
  // 简化实现
}

void popHostContext(const FiberRef& /*workInProgress*/) {
  // 简化实现
}

void popTreeContext(const FiberRef& workInProgress) {
  // 简化实现
}

void popLegacyContext(const FiberRef& workInProgress) {
  // 简化实现
}

void popTopLevelLegacyContextObject(const FiberRef& workInProgress) {
  // 简化实现
}

bool isLegacyContextProvider(const std::any&) {
  // 简化实现
  return false;
}

bool isLegacyContextProvider(const jsi::Value&) {
  // 简化实现
  return false;
}

void popProvider(const std::any& /*context*/, const FiberRef& /*workInProgress*/) {
  // 简化实现
}

void popProvider(const jsi::Value& /*context*/, const FiberRef& /*workInProgress*/) {
  // 简化实现
}

void popCacheProvider(const FiberRef& /*workInProgress*/, const std::any& /*cache*/) {
  // 简化实现
}

void popHiddenContext(const FiberRef& /*workInProgress*/) {
  // 简化实现
}

void popTransition(const FiberRef& /*workInProgress*/, const FiberRef& /*current*/) {
  // 简化实现
}

void popRootTransition(const FiberRef& /*workInProgress*/, const FiberRootRef& /*root*/, Lanes /*renderLanes*/) {
  // 简化实现
}

void resetHydrationState() {
  // 简化实现
}

void transferActualDuration(const FiberRef& /*workInProgress*/) {
  // 简化实现
}

FiberRef unwindWork(const FiberRef& current, const FiberRef& workInProgress, Lanes renderLanes) {
  popTreeContext(workInProgress);

  switch (workInProgress->tag) {
    case ClassComponent: {
      if (isLegacyContextProvider(workInProgress->type)) {
        popLegacyContext(workInProgress);
      }

      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;

        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }

        return workInProgress;
      }
      return nullptr;
    }

    case HostRoot: {
      auto* rootPtr = std::get_if<FiberRootRef>(&workInProgress->stateNode);
      if (rootPtr) {
        FiberRootRef root = *rootPtr;

        popRootTransition(workInProgress, root, renderLanes);
        popHostContainer(workInProgress);
        popTopLevelLegacyContextObject(workInProgress);

        Flags flags = workInProgress->flags;
        if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
          workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;
          return workInProgress;
        }
      }
      return nullptr;
    }

    case HostComponent:
    case HostHoistable:
    case HostSingleton: {
      popHostContext(workInProgress);
      return nullptr;
    }

    case SuspenseComponent: {
      popSuspenseHandler(workInProgress);

      auto* statePtr = std::any_cast<std::shared_ptr<SuspenseState>>(&workInProgress->memoizedState);
      if (statePtr && *statePtr) {
        auto& suspenseState = *statePtr;
        if (suspenseState->dehydrated.has_value()) {
          auto alternate = workInProgress->alternate.lock();
          if (!alternate) {
            // 新挂载的 dehydrated 组件抛出异常 - 这是 React 的 bug
          }

          resetHydrationState();
        }
      }

      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;

        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }

        return workInProgress;
      }
      return nullptr;
    }

    case SuspenseListComponent: {
      popSuspenseListContext(workInProgress);
      return nullptr;
    }

    case HostPortal: {
      popHostContainer(workInProgress);
      return nullptr;
    }

    case ContextProvider: {
      popProvider(workInProgress->type, workInProgress);
      return nullptr;
    }

    case OffscreenComponent:
    case LegacyHiddenComponent: {
      popSuspenseHandler(workInProgress);
      popHiddenContext(workInProgress);
      popTransition(workInProgress, current);

      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;

        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }

        return workInProgress;
      }
      return nullptr;
    }

    case CacheComponent: {
      return nullptr;
    }

    default:
      return nullptr;
  }
}

void unwindInterruptedWork(const FiberRef& current, const FiberRef& interruptedWork, Lanes renderLanes) {
  popTreeContext(interruptedWork);

  switch (interruptedWork->tag) {
    case ClassComponent: {
      break;
    }

    case HostRoot: {
      auto* rootPtr = std::get_if<FiberRootRef>(&interruptedWork->stateNode);
      if (rootPtr) {
        FiberRootRef root = *rootPtr;
        popRootTransition(interruptedWork, root, renderLanes);
        popHostContainer(interruptedWork);
        popTopLevelLegacyContextObject(interruptedWork);
      }
      break;
    }

    case HostComponent:
    case HostHoistable:
    case HostSingleton: {
      popHostContext(interruptedWork);
      break;
    }

    case HostPortal: {
      popHostContainer(interruptedWork);
      break;
    }

    case SuspenseComponent: {
      popSuspenseHandler(interruptedWork);
      break;
    }

    case SuspenseListComponent: {
      popSuspenseListContext(interruptedWork);
      break;
    }

    case ContextProvider: {
      popProvider(interruptedWork->type, interruptedWork);
      break;
    }

    case OffscreenComponent:
    case LegacyHiddenComponent: {
      popSuspenseHandler(interruptedWork);
      popHiddenContext(interruptedWork);
      popTransition(interruptedWork, current);
      break;
    }

    case CacheComponent: {
      break;
    }

    default:
      break;
  }
}

FiberRef completeUnitOfUnwind(const FiberRef& unitOfWork, const FiberRef& /*thrownValue*/, Lanes renderLanes) {
  FiberRef current = unitOfWork->alternate.lock();

  FiberRef next = unwindWork(current, unitOfWork, renderLanes);

  if (next) {
    next->flags &= ~Incomplete;
    return next;
  }

  FiberRef returnFiber = unitOfWork->return_.lock();
  if (returnFiber) {
    returnFiber->flags |= Incomplete;
    returnFiber->subtreeFlags = NoFlags;
    returnFiber->deletions.clear();
  }

  return nullptr;
}

} // namespace react::reconciler
