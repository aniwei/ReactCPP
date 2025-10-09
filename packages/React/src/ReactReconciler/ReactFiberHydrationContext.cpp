#include "ReactReconciler/ReactFiberHydrationContext.h"

#include <memory>
#include <string>

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"
#include "ReactReconciler/ReactHostConfig.h"
#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactRuntime/ReactRuntime.h"

namespace react {
namespace {

inline WorkLoopState& getState(ReactRuntime& runtime) {
  return runtime.workLoopState();
}

hostconfig::HostInstance getHostInstanceFromFiber(FiberNode& fiber) {
  auto* slot = static_cast<hostconfig::HostInstance*>(fiber.stateNode);
  if (slot == nullptr) {
    return nullptr;
  }
  return *slot;
}

std::string getHostSingletonType(FiberNode& fiber) {
  auto instance = getHostInstanceFromFiber(fiber);
  if (!instance) {
    return std::string{};
  }
  auto component = std::dynamic_pointer_cast<ReactDOMComponent>(instance);
  if (!component) {
    return std::string{};
  }
  return component->getType();
}

} // namespace

void resetHydrationState(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  state.isHydrating = false;
  state.hydrationParentFiber = nullptr;
  state.nextHydratableInstance = nullptr;
  state.rootOrSingletonHydrationContext = false;
}

bool enterHydrationState(ReactRuntime& runtime, FiberNode& fiber, void* firstHydratableInstance) {
  if (!hostconfig::supportsHydration(runtime)) {
    return false;
  }

  auto& state = getState(runtime);
  state.isHydrating = true;
  state.hydrationParentFiber = &fiber;
  state.nextHydratableInstance = firstHydratableInstance;
  state.rootOrSingletonHydrationContext = true;
  return true;
}

bool popHydrationState(ReactRuntime& runtime, FiberNode& workInProgress) {
  if (!hostconfig::supportsHydration(runtime)) {
    return false;
  }

  auto& state = getState(runtime);
  if (!state.isHydrating) {
    return false;
  }

  if (state.hydrationParentFiber != &workInProgress) {
    return false;
  }

  state.hydrationParentFiber = workInProgress.returnFiber;

  if (state.hydrationParentFiber == nullptr) {
    resetHydrationState(runtime);
    return true;
  }

  switch (state.hydrationParentFiber->tag) {
    case WorkTag::HostRoot:
    case WorkTag::HostSingleton:
      state.rootOrSingletonHydrationContext = true;
      break;
    default:
      state.rootOrSingletonHydrationContext = false;
      break;
  }

  if (state.hydrationParentFiber != nullptr) {
    if (workInProgress.tag == WorkTag::HostSingleton && hostconfig::supportsSingletons(runtime)) {
      auto singletonType = getHostSingletonType(workInProgress);
      state.nextHydratableInstance = hostconfig::getNextHydratableSiblingAfterSingleton(
          runtime,
          singletonType,
          state.nextHydratableInstance);
    } else {
      state.nextHydratableInstance = hostconfig::getNextHydratableSibling(runtime, workInProgress.stateNode);
    }
  } else {
    state.nextHydratableInstance = nullptr;
  }

  return true;
}

bool getIsHydrating(ReactRuntime& runtime) {
  return getState(runtime).isHydrating;
}

} // namespace react
