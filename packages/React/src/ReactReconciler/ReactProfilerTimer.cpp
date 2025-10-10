#include "ReactReconciler/ReactProfilerTimer.h"

#include "shared/ReactFeatureFlags.h"

namespace react {
namespace {

bool currentUpdateIsNested = false;
bool nestedUpdateScheduled = false;

} // namespace

bool isCurrentUpdateNested() {
  if (!enableProfilerNestedUpdatePhase) {
    return false;
  }
  return currentUpdateIsNested;
}

void markNestedUpdateScheduled() {
  if (enableProfilerNestedUpdatePhase) {
    nestedUpdateScheduled = true;
  }
}

void resetNestedUpdateFlag() {
  if (enableProfilerNestedUpdatePhase) {
    currentUpdateIsNested = false;
    nestedUpdateScheduled = false;
  }
}

void syncNestedUpdateFlag() {
  if (enableProfilerNestedUpdatePhase) {
    currentUpdateIsNested = nestedUpdateScheduled;
    nestedUpdateScheduled = false;
  }
}

} // namespace react
