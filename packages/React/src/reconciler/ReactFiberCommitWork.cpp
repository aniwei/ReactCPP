#include "ReactFiberCommitWork.h"

#include <utility>

namespace react::reconciler {

void ReactFiberCommitWork::setHostConfig(CommitHostConfig config) {
  hostConfig_ = std::move(config);
}

bool ReactFiberCommitWork::shouldFireAfterActiveInstanceBlur() const {
  return shouldFireAfterActiveInstanceBlur_;
}

void ReactFiberCommitWork::resetAfterActiveInstanceBlur() {
  shouldFireAfterActiveInstanceBlur_ = false;
}

} // namespace react::reconciler
