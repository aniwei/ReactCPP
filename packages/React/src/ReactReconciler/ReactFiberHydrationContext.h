#pragma once

namespace react {

class ReactRuntime;
struct FiberNode;

void resetHydrationState(ReactRuntime& runtime);
bool enterHydrationState(ReactRuntime& runtime, FiberNode& fiber, void* firstHydratableInstance);
bool popHydrationState(ReactRuntime& runtime, FiberNode& workInProgress);
bool getIsHydrating(ReactRuntime& runtime);

} // namespace react
