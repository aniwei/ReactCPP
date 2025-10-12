#pragma once

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactHostConfig.h"

#include <vector>

namespace facebook {
namespace jsi {
class Runtime;
} // namespace jsi
} // namespace facebook

namespace react {

class ReactRuntime;
struct FiberRoot;

FiberFlags bubbleProperties(FiberNode& completedWork);
FiberNode* completeWork(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes entangledRenderLanes);

// Helpers consumed by the complete-work phase that are defined elsewhere.
void markUpdate(FiberNode& workInProgress);
void popHostContext(ReactRuntime& runtime, FiberNode& workInProgress);
void popHostContainer(ReactRuntime& runtime, FiberNode& workInProgress);
void popTopLevelLegacyContextObject(ReactRuntime& runtime, FiberNode& workInProgress);
void popCacheProvider(FiberNode& workInProgress, void* cache);
void popRootTransition(ReactRuntime& runtime, FiberNode& workInProgress, FiberRoot& root, Lanes renderLanes);
void popSuspenseHandler(FiberNode& workInProgress);
void popHiddenContext(ReactRuntime& runtime, FiberNode& workInProgress);
void popTransition(FiberNode& workInProgress, FiberNode* current);

bool popHydrationState(ReactRuntime& runtime, FiberNode& workInProgress);
void emitPendingHydrationWarningsInternal(ReactRuntime& runtime);
void upgradeHydrationErrorsToRecoverable(ReactRuntime& runtime);
void updateHostContainer(FiberNode* current, FiberNode& workInProgress);

std::vector<const Transition*>& getWorkInProgressTransitions(ReactRuntime& runtime);
void popRootMarkerInstance(FiberNode& workInProgress);
void popMarkerInstance(FiberNode& workInProgress);

facebook::jsi::Value cloneJsiValue(facebook::jsi::Runtime& jsRuntime, const void* storage);
facebook::jsi::Object ensureObject(facebook::jsi::Runtime& jsRuntime, const facebook::jsi::Value& value);
std::string getFiberType(facebook::jsi::Runtime& jsRuntime, const FiberNode& fiber);
hostconfig::HostInstance getHostInstance(const FiberNode& fiber);
void setHostInstance(FiberNode& fiber, hostconfig::HostInstance instance);
void storeHostUpdatePayload(facebook::jsi::Runtime& jsRuntime, FiberNode& fiber, const facebook::jsi::Value& payload);
void clearHostUpdatePayload(FiberNode& fiber);
std::string valueToString(facebook::jsi::Runtime& jsRuntime, const facebook::jsi::Value& value);

} // namespace react
