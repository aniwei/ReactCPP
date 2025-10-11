#pragma once

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberErrorLogger.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactCapturedValue.h"

#include <functional>
#include <memory>
#include <vector>

namespace facebook::jsi {
class Runtime;
class Value;
}

namespace react {

struct FiberRoot;
class ReactRuntime;

bool isAlreadyFailedLegacyErrorBoundary(void* instance);
void markLegacyErrorBoundaryAsFailed(void* instance);

enum class UpdateTag : std::uint8_t {
  UpdateState = 0,
  ReplaceState = 1,
  ForceUpdate = 2,
  CaptureUpdate = 3,
};

struct Update : ConcurrentUpdate {
  UpdateTag tag{UpdateTag::UpdateState};
  void* payload{nullptr};
  std::function<void()> callback{};
};

struct SharedQueue : ConcurrentUpdateQueue {
  Lanes lanes{NoLanes};
  std::vector<std::function<void()>> hiddenCallbacks{};
};

struct UpdateQueue {
  void* baseState{nullptr};
  Update* firstBaseUpdate{nullptr};
  Update* lastBaseUpdate{nullptr};
  std::shared_ptr<SharedQueue> shared{};
  std::vector<std::function<void()>> callbacks{};
  std::vector<std::unique_ptr<Update>> ownedUpdates{};
};

std::unique_ptr<Update> createUpdate(Lane lane);
void initializeUpdateQueue(FiberNode& fiber);
void cloneUpdateQueue(FiberNode& current, FiberNode& workInProgress);
UpdateQueue& ensureUpdateQueue(FiberNode& fiber);
FiberRoot* enqueueUpdate(
    FiberNode& fiber,
    std::unique_ptr<Update> update,
    Lane lane);
std::unique_ptr<Update> createRootErrorUpdate(
    FiberRoot& root,
    const CapturedValue& errorInfo,
    Lane lane);
std::unique_ptr<Update> createClassErrorUpdate(Lane lane);
void initializeClassErrorUpdate(
    Update& update,
    FiberRoot& root,
    FiberNode& fiber,
    const CapturedValue& errorInfo);
void enqueueCapturedUpdate(FiberNode& fiber, std::unique_ptr<Update> update);
void processUpdateQueue(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& workInProgress,
    const facebook::jsi::Value& props,
    const facebook::jsi::Value& instanceValue,
    Lanes renderLanes);

void suspendIfUpdateReadFromEntangledAsyncAction(ReactRuntime& runtime);
void resetHasForceUpdateBeforeProcessing(ReactRuntime& runtime);
bool checkHasForceUpdateAfterProcessing(ReactRuntime& runtime);
void deferHiddenCallbacks(UpdateQueue& queue);
void commitHiddenCallbacks(UpdateQueue& queue);
void commitCallbacks(UpdateQueue& queue);

} // namespace react
