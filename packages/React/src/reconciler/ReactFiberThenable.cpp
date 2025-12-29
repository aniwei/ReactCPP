/**
 * React Fiber Thenable
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberThenable.js
 */

#include "ReactFiberThenable.h"

namespace react::reconciler {

std::shared_ptr<Thenable<void>> noopSuspenseyCommitThenable() {
  auto thenable = std::make_shared<Thenable<void>>();
  thenable->status = ThenableStatus::Pending;
  thenable->then = [](auto, auto) {
    // noop
  };
  return thenable;
}

ThenableStateRef createThenableState() {
  return std::make_shared<ThenableState>();
}

std::shared_ptr<Thenable<std::any>> getSuspendedThenable() {
  // 简化实现 - 实际需要从 work loop 状态获取
  return nullptr;
}

} // namespace react::reconciler
