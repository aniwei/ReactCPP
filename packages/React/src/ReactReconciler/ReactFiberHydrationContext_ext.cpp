#include "ReactReconciler/ReactFiberHydrationContext_ext.h"
#include "ReactReconciler/ReactFiberHydrationContext.h"
#include "ReactReconciler/ReactHostConfig.h"
#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"
#include "ReactRuntime/ReactRuntime.h"
#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactDOM/client/ReactDOMInstance.h"

#include <iostream>
#include <string>

namespace react {
namespace {
inline WorkLoopState& getState(ReactRuntime& runtime) {
  return runtime.workLoopState();
}
} // namespace

ReactDOMInstance* tryToClaimNextHydratableInstance(ReactRuntime& runtime, FiberNode& fiber, const std::string& type) {
  auto& state = getState(runtime);
  if (!state.isHydrating || state.nextHydratableInstance == nullptr) {
    return nullptr;
  }
  auto* instance = static_cast<ReactDOMInstance*>(state.nextHydratableInstance);
  auto* component = dynamic_cast<ReactDOMComponent*>(instance);
  if (component && component->getType() == type) {
    state.hydrationParentFiber = &fiber;
    state.rootOrSingletonHydrationContext = false;
    auto sharedInstance = instance->shared_from_this();
    state.nextHydratableInstance = hostconfig::getFirstHydratableChild(runtime, sharedInstance);
    return instance;
  }
  // If not matched, report error and skip.
  queueHydrationError(runtime, fiber, "Hydration: instance type mismatch or not found");
  state.nextHydratableInstance = hostconfig::getNextHydratableSibling(runtime, instance);
  return nullptr;
}

ReactDOMInstance* tryToClaimNextHydratableTextInstance(ReactRuntime& runtime, FiberNode& fiber) {
  auto& state = getState(runtime);
  if (!state.isHydrating || state.nextHydratableInstance == nullptr) {
    return nullptr;
  }

  auto* instance = static_cast<ReactDOMInstance*>(state.nextHydratableInstance);
  auto* component = dynamic_cast<ReactDOMComponent*>(instance);
  if (component && component->isTextInstance()) {
    state.hydrationParentFiber = &fiber;
    state.rootOrSingletonHydrationContext = false;
    state.nextHydratableInstance = hostconfig::getNextHydratableSibling(runtime, instance);
    return instance;
  }

  queueHydrationError(runtime, fiber, "Hydration: text instance not found");
  state.nextHydratableInstance = hostconfig::getNextHydratableSibling(runtime, instance);
  return nullptr;
}

ReactDOMInstance* claimHydratableSingleton(ReactRuntime& runtime, FiberNode& fiber, const std::string& type) {
  auto& state = getState(runtime);
  if (!state.isHydrating || state.nextHydratableInstance == nullptr || !hostconfig::supportsSingletons(runtime)) {
    return nullptr;
  }

  auto* instance = static_cast<ReactDOMInstance*>(state.nextHydratableInstance);
  while (instance != nullptr) {
    auto* component = dynamic_cast<ReactDOMComponent*>(instance);
    if (component && component->getType() == type) {
      state.hydrationParentFiber = &fiber;
      state.rootOrSingletonHydrationContext = true;
      auto sharedInstance = instance->shared_from_this();
      state.nextHydratableInstance = hostconfig::getFirstHydratableChildWithinSingleton(
          runtime,
          type,
          sharedInstance,
          state.nextHydratableInstance);
      return instance;
    }

    queueHydrationError(runtime, fiber, "Hydration: singleton instance mismatch");
    instance = static_cast<ReactDOMInstance*>(hostconfig::getNextHydratableSibling(runtime, instance));
    state.nextHydratableInstance = instance;
  }

  return nullptr;
}

void queueHydrationError(ReactRuntime& runtime, FiberNode& fiber, const char* message) {
  auto& state = getState(runtime);
  state.hydrationErrors.push_back({&fiber, std::string(message)});
  // Still mirror to stderr for debugging; host interfaces receive queued errors via ReactRuntime::drainHydrationErrors.
  std::cerr << "[HydrationError] Fiber key: " << fiber.key << " - " << message << std::endl;
}

} // namespace react
