#include "ReactFiberBeginWork.h"

#include <utility>

namespace react::reconciler {

ReactFiberBeginWork::ReactFiberBeginWork(
  ChildReconciler reconciler,
  HostConfigInterface hostConfig
) : reconciler_(std::move(reconciler)),
    hostConfig_(std::move(hostConfig)) {
    resetContext();
}

BeginWorkResult ReactFiberBeginWork::beginWork(
  FiberRef /*current*/,
  FiberRef workInProgress,
  Lanes /*renderLanes*/
) {
  // 最小实现：不做实际 reconcile，只沿着现有 child 指针向下遍历。
  if (!workInProgress) {
    return nullptr;
  }

  return workInProgress->child;
}

void ReactFiberBeginWork::markWorkInProgressReceivedUpdate() {
  context_.didReceiveUpdate = true;
}

bool ReactFiberBeginWork::didReceiveUpdate() const {
  return context_.didReceiveUpdate;
}

void ReactFiberBeginWork::resetContext() {
  context_.didReceiveUpdate = false;
}

void ReactFiberBeginWork::setReconciler(ChildReconciler reconciler) {
  reconciler_ = std::move(reconciler);
}

void ReactFiberBeginWork::setHostConfig(HostConfigInterface config) {
  hostConfig_ = std::move(config);
}

} // namespace react::reconciler
