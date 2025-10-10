#pragma once

#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

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
  std::optional<bool> supportsMicrotasksCache{};
  Lane currentEventTransitionLane{NoLane};
  std::uint64_t nextActCallbackId{1};
  std::unordered_map<std::uint64_t, std::shared_ptr<facebook::jsi::Value>> actCallbacks{};
  bool hasTrackedSchedulerEvent{false};
  std::string lastTrackedSchedulerEventType{};
  double lastTrackedSchedulerEventTimestamp{-1.0};
};

} // namespace react
