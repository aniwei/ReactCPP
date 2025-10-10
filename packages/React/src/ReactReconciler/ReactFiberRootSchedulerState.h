#pragma once

#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

#include <memory>
#include <optional>
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
};

} // namespace react
