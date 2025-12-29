#include "ReactFiberThrow.h"

#include <exception>
#include <optional>
#include <set>
#include <stdexcept>

namespace react::reconciler {

FiberUpdateRef<std::any> createRootErrorUpdate(
  FiberRootRef root,
  ErrorCapturedValueRef errorInfo,
  Lane lane
) {
  auto update = createFiberUpdate<std::any>(lane);
  update->tag = UpdateTag::CaptureUpdate;

  update->payload = nullptr;

  update->callback = [root, errorInfo]() {
    (void)root;
    if (errorInfo && errorInfo->message) {
      // Simplified stub.
    }
  };

  return update;
}

FiberUpdateRef<std::any> createClassErrorUpdate(Lane lane) {
  auto update = createFiberUpdate<std::any>(lane);
  update->tag = UpdateTag::CaptureUpdate;
  return update;
}

FiberRef markSuspenseBoundaryShouldCapture(
  FiberRef suspenseBoundary,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  FiberRootRef /*root*/,
  Lanes /*rootRenderLanes*/
) {
  bool isLegacyMode = (suspenseBoundary->mode & ConcurrentMode) == NoMode;

  if (isLegacyMode) {
    if (suspenseBoundary == returnFiber) {
      suspenseBoundary->flags |= ShouldCapture;
    } else {
      suspenseBoundary->flags |= DidCapture;
      sourceFiber->flags |= ForceUpdateForLegacySuspense;
      sourceFiber->flags &= ~(LifecycleEffectMask | Incomplete);

      if (sourceFiber->tag == ClassComponent) {
        auto currentSourceFiber = sourceFiber->alternate.lock();
        if (!currentSourceFiber) {
          sourceFiber->tag = IncompleteClassComponent;
        }
      }
    }
  } else {
    suspenseBoundary->flags |= ShouldCapture;
  }

  return suspenseBoundary;
}

void resetSuspendedComponent(
  FiberRef sourceFiber,
  Lanes /*rootRenderLanes*/) {
  auto currentSourceFiber = sourceFiber->alternate.lock();

  WorkTag tag = sourceFiber->tag;
  bool isLegacyMode = (sourceFiber->mode & ConcurrentMode) == NoMode;

  if (isLegacyMode &&
    (tag == FunctionComponent || tag == ForwardRef || tag == SimpleMemoComponent)) {

    if (currentSourceFiber) {
      sourceFiber->updateQueue = currentSourceFiber->updateQueue;
      sourceFiber->memoizedState = currentSourceFiber->memoizedState;
      sourceFiber->lanes = currentSourceFiber->lanes;
    } else {
      sourceFiber->updateQueue = std::monostate{};
      sourceFiber->memoizedState = std::any{};
    }
  }
}

bool isThenable(const std::any& value) {
  if (!value.has_value()) {
    return false;
  }

  try {
    auto* thenable = std::any_cast<std::shared_ptr<Thenable<std::any>>>(&value);
    return thenable != nullptr;
  } catch (...) {
    return false;
  }
}

void throwException(
  FiberRootRef root,
  FiberRef returnFiber,
  FiberRef sourceFiber,
  std::any value,
  Lanes rootRenderLanes
) {
  sourceFiber->flags |= Incomplete;

  if (isThenable(value)) {
    resetSuspendedComponent(sourceFiber, rootRenderLanes);

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

  auto capturedValue = createCapturedValueFromError(
    std::make_exception_ptr(std::runtime_error("Render error")),
    std::nullopt
  );

  FiberRef workInProgress = returnFiber;

  while (workInProgress) {
    switch (workInProgress->tag) {
      case HostRoot: {
        (void)createRootErrorUpdate(root, capturedValue, SyncLane);
        workInProgress->flags |= ShouldCapture;
        return;
      }

      case ClassComponent: {
        (void)createClassErrorUpdate(SyncLane);
        workInProgress->flags |= ShouldCapture;
        return;
      }

      default:
        break;
    }

    workInProgress = workInProgress->return_.lock();
  }
}

namespace {
using FiberSet = std::set<FiberRef, std::owner_less<FiberRef>>;
FiberSet& legacyErrorBoundariesThatAlreadyFailed() {
  static FiberSet set;
  return set;
}
} // namespace

void markLegacyErrorBoundaryAsFailed(FiberRef fiber) {
  legacyErrorBoundariesThatAlreadyFailed().insert(fiber);
}

bool isAlreadyFailedLegacyErrorBoundary(FiberRef fiber) {
  return legacyErrorBoundariesThatAlreadyFailed().count(fiber) > 0;
}

void clearLegacyErrorBoundaries() {
  legacyErrorBoundariesThatAlreadyFailed().clear();
}

} // namespace react::reconciler
