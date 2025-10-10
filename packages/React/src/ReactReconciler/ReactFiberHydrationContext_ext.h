#pragma once

#include "ReactReconciler/ReactFiber.h"
#include "ReactRuntime/ReactRuntime.h"

#include <string>

namespace react {

class ReactDOMInstance;

// Hydration instance matching and error reporting (extension)
ReactDOMInstance* tryToClaimNextHydratableInstance(ReactRuntime& runtime, FiberNode& fiber, const std::string& type);
ReactDOMInstance* tryToClaimNextHydratableTextInstance(ReactRuntime& runtime, FiberNode& fiber);
ReactDOMInstance* claimHydratableSingleton(ReactRuntime& runtime, FiberNode& fiber, const std::string& type);
void* tryToClaimNextHydratableSuspenseInstance(ReactRuntime& runtime, FiberNode& fiber);
void queueHydrationError(ReactRuntime& runtime, FiberNode& fiber, const char* message);

} // namespace react
