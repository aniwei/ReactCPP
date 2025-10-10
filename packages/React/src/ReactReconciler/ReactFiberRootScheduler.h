#pragma once

#include "ReactReconciler/ReactFiberLane.h"
#include <functional>

namespace facebook {
namespace jsi {
class Runtime;
} // namespace jsi
} // namespace facebook

namespace react {

class ReactRuntime;
struct FiberRoot;
struct Transition;

// Task callback type for scheduler tasks - returns continuation or null
using RenderTaskFn = std::function<std::function<void(bool)>()>;

void ensureRootIsScheduled(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root);
void ensureScheduleIsScheduled(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void flushSyncWorkOnAllRoots(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, Lanes syncTransitionLanes);
void flushSyncWorkOnLegacyRootsOnly(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
Lane requestTransitionLane(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, const Transition* transition);
bool didCurrentEventScheduleTransition(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void markIndicatorHandled(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root);
void registerRootDefaultIndicator(
	ReactRuntime& runtime,
	facebook::jsi::Runtime& jsRuntime,
	FiberRoot& root,
	std::function<std::function<void()>()> onDefaultTransitionIndicator);

// Entry points for different scheduling contexts
bool performSyncWorkOnRoot(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root, Lanes lanes);
RenderTaskFn performWorkOnRootViaSchedulerTask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root, bool didTimeout);

} // namespace react
