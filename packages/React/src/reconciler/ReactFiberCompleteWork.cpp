#include "ReactFiberCompleteWork.h"

#include <utility>

namespace react::reconciler {

ReactFiberCompleteWork::ReactFiberCompleteWork(
  CompleteHostConfig hostConfig
) : hostConfig_(std::move(hostConfig)) {}

CompleteWorkResult ReactFiberCompleteWork::completeWork(
  FiberRef /*current*/,
  FiberRef /*workInProgress*/,
  Lanes /*renderLanes*/
) {
  // 最小实现：不生成额外工作，直接返回 nullptr。
  return nullptr;
}

void ReactFiberCompleteWork::setHostConfig(CompleteHostConfig config) {
  hostConfig_ = std::move(config);
}

void ReactFiberCompleteWork::setHostContext(HostContext context) {
  hostContext_ = std::move(context);
}

const HostContext& ReactFiberCompleteWork::getHostContext() const {
  return hostContext_;
}

} // namespace react::reconciler
