#pragma once

#include "ReactReconciler/ReactFiberLane.h"

namespace react {

struct FiberRoot;

struct RootSchedulerState {
  FiberRoot* firstScheduledRoot{nullptr};
  FiberRoot* lastScheduledRoot{nullptr};
  bool didScheduleRootProcessing{false};
  bool isProcessingRootSchedule{false};
  bool mightHavePendingSyncWork{false};
  bool isFlushingWork{false};
  bool didScheduleMicrotask{false};
  bool didScheduleMicrotaskAct{false};
  Lane currentEventTransitionLane{NoLane};
};

} // namespace react
