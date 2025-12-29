#include "ReactFiberRoot.h"

namespace react::reconciler {

FiberRoot::FiberRoot() {
  // 初始化 LaneMap
  expirationTimes.fill(-1.0);
  entanglements.fill(NoLanes);
}

std::shared_ptr<FiberRoot> FiberRoot::getNext() const {
  return next.lock();
}

void FiberRoot::setNext(std::shared_ptr<FiberRoot> nextRoot) {
  next = nextRoot;
}

void FiberRoot::markRootUpdated(Lane updateLane, double eventTime) {
  pendingLanes |= updateLane;

  // 设置过期时间
  int index = laneToIndex(updateLane);
  if (index >= 0 && index < TotalLanes) {
    expirationTimes[index] = eventTime;
  }
}

void FiberRoot::markRootSuspended(Lanes suspendedLanes_) {
  suspendedLanes |= suspendedLanes_;
  pingedLanes &= ~suspendedLanes_;
}

void FiberRoot::markRootPinged(Lanes pingedLanes_) {
  pingedLanes |= pendingLanes & pingedLanes_;
}

void FiberRoot::markRootFinished(Lanes finishedLanes) {
  Lanes remainingLanes = pendingLanes & ~finishedLanes;

  pendingLanes = remainingLanes;
  suspendedLanes = NoLanes;
  pingedLanes = NoLanes;
  expiredLanes &= remainingLanes;
  warmLanes &= remainingLanes;

  // 清理完成的 lanes 的过期时间
  Lanes lanes = finishedLanes;
  while (lanes > 0) {
    int index = laneToIndex(getHighestPriorityLane(lanes));
    if (index >= 0 && index < TotalLanes) {
      expirationTimes[index] = -1.0;
      entanglements[index] = NoLanes;
    }
    lanes &= ~(1 << index);
  }
}

bool FiberRoot::hasPendingWork() const {
  return pendingLanes != NoLanes;
}

Lanes FiberRoot::getNextLanesToWork() const {
  return getNextLanes(pendingLanes, suspendedLanes);
}

FiberRootRef createFiberRoot(
  const ::react::ContainerInfo& containerInfo,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError
) {
  auto root = std::make_shared<FiberRoot>();

  root->tag = tag;
  root->containerInfo = containerInfo;
  root->identifierPrefix = identifierPrefix;
  root->onUncaughtError = onUncaughtError;
  root->onCaughtError = onCaughtError;
  root->onRecoverableError = onRecoverableError;

  // 创建根 Fiber
  FiberRef uninitializedFiber = createHostRootFiber(tag, isStrictMode, false);
  root->current = uninitializedFiber;
  uninitializedFiber->stateNode = root;

  return root;
}

FiberRootRef createFiberRoot(
  ::react::ReactDOMContainer* container,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError
) {
  return createFiberRoot(
    ::react::ContainerInfo(container),
    tag,
    isStrictMode,
    identifierPrefix,
    onUncaughtError,
    onCaughtError,
    onRecoverableError);
}

FiberRootRef createFiberRoot(
  const std::string& debugName,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError
) {
  return createFiberRoot(
    ::react::ContainerInfo(debugName),
    tag,
    isStrictMode,
    identifierPrefix,
    onUncaughtError,
    onCaughtError,
    onRecoverableError);
}

} // namespace react::reconciler
