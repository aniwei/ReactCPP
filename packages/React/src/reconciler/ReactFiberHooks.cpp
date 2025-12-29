#include "ReactFiberHooks.h"

#include <stdexcept>

#include "../shared/objectIs.h"
#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

ReactFiberHooks& getHooks(react::ReactHostRuntime& hostRuntime) {
  return hostRuntime.getFiberHooks();
}

void setHooks(react::ReactHostRuntime& hostRuntime, std::shared_ptr<ReactFiberHooks> hooks) {
  hostRuntime.setFiberHooks(std::move(hooks));
}

bool areHookInputsEqual(
  const std::vector<std::any>& nextDeps,
  const std::vector<std::any>& prevDeps
) {
  if (nextDeps.size() != prevDeps.size()) {
    return false;
  }

  for (size_t i = 0; i < nextDeps.size(); ++i) {
    if (!react::shared::objectIs(nextDeps[i], prevDeps[i])) {
      return false;
    }
  }

  return true;
}

} // namespace react::reconciler
