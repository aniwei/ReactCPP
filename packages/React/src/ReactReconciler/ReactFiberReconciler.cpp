#include "ReactReconciler/ReactFiberReconciler.h"

#include "ReactReconciler/ReactFiberAsyncAction.h"
#include "ReactReconciler/ReactFiberClassUpdateQueue.h"
#include "ReactReconciler/ReactFiberRoot.h"
#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "ReactRuntime/ReactRuntimeContext.h"
#include "shared/ReactFeatureFlags.h"

#include "jsi/jsi.h"

#include <stdexcept>
#include <string>
#include <memory>

namespace react {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

void assignContextSlot(void*& slot, Runtime& runtime, const Value& value) {
  auto* stored = new Value(runtime, value);
  if (slot != nullptr) {
    delete static_cast<Value*>(slot);
  }
  slot = stored;
}

Value createEmptyContext(Runtime& runtime) {
  Object empty(runtime);
  return Value(runtime, empty);
}

[[noreturn]] void throwNotImplemented(const char* name) {
  throw std::logic_error(std::string("ReactFiberReconciler::") + name + " not implemented");
}

Value getContextForSubtree(Runtime& runtime, Object* parentComponent) {
  (void)parentComponent;
  // Legacy context propagation is not yet ported. Provide an empty object stub.
  return createEmptyContext(runtime);
}

void updateContainerImpl(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& rootFiber,
    FiberRoot& container,
    Lane lane,
    const ReactNodeList& element,
    Object* parentComponent,
    facebook::jsi::Function* callback) {
#ifdef DEBUG 
 // TODO: onScheduleRoot
#endif

#ifdef PROFILER
// TODO: markRenderScheduled
#endif

  Value context = getContextForSubtree(jsRuntime, parentComponent);
  if (container.context == nullptr) {
    assignContextSlot(container.context, jsRuntime, context);
  } else {
    assignContextSlot(container.pendingContext, jsRuntime, context);
  }

  auto update = createUpdate(lane);
  auto payload = std::make_unique<HostRootUpdatePayload>();
  payload->element = std::make_unique<Value>(jsRuntime, element);
  update->payloadType = UpdatePayloadType::HostRoot;
  update->payload = payload.release();

  if (callback != nullptr) {
    // TODO: bridge commit callbacks into JS when host support is available.
  }

  FiberRoot* scheduledRoot = enqueueUpdate(rootFiber, std::move(update), lane);
  if (scheduledRoot != nullptr) {
    // TDOO: startUpdateTimerByLane
    scheduleUpdateOnFiber(runtime, jsRuntime, *scheduledRoot, rootFiber, lane);
  }
}

} // namespace

FiberRoot* createContainer(
    Container containerInfo,
    RootTag tag,
    SuspenseHydrationCallbacks* hydrationCallbacks,
    bool isStrictMode,
    std::nullptr_t concurrentUpdatesByDefaultOverride,
    const std::string& identifierPrefix,
    void (*onUncaughtError)(void*, const void*),
    void (*onCaughtError)(void*, const void*),
    void (*onRecoverableError)(void*, const void*),
    void (*onDefaultTransitionIndicator)(),
    TransitionTracingCallbacks* transitionCallbacks) {
  (void)concurrentUpdatesByDefaultOverride;

  ReactRuntime& runtime = requireReactRuntime();
  Runtime& jsRuntime = requireJsiRuntime();

  constexpr bool hydrate = false;
  const ReactNodeList initialChildren{};
  FiberRoot* root = createFiberRoot(
      runtime,
      containerInfo,
      tag,
      hydrate,
      initialChildren,
      hydrationCallbacks,
      isStrictMode,
      identifierPrefix,
      nullptr,
      onUncaughtError,
      onCaughtError,
      onRecoverableError,
      onDefaultTransitionIndicator,
      transitionCallbacks);

  if (root == nullptr) {
    return nullptr;
  }

  root->jsRuntime = &jsRuntime;
  registerDefaultIndicator(runtime, root, root->onDefaultTransitionIndicator);
  return root;
}

FiberRoot* createHydrationContainer(
    const ReactNodeList& initialChildren,
    facebook::jsi::Function* callback,
    Container containerInfo,
    RootTag tag,
    SuspenseHydrationCallbacks* hydrationCallbacks,
    bool isStrictMode,
    std::nullptr_t concurrentUpdatesByDefaultOverride,
    const std::string& identifierPrefix,
    void (*onUncaughtError)(void*, const void*),
    void (*onCaughtError)(void*, const void*),
    void (*onRecoverableError)(void*, const void*),
    void (*onDefaultTransitionIndicator)(),
    TransitionTracingCallbacks* transitionCallbacks,
    ReactFormState* formState) {
  (void)concurrentUpdatesByDefaultOverride;

  ReactRuntime& runtime = requireReactRuntime();
  Runtime& jsRuntime = requireJsiRuntime();

  constexpr bool hydrate = true;
  FiberRoot* root = createFiberRoot(
      runtime,
      containerInfo,
      tag,
      hydrate,
      initialChildren,
      hydrationCallbacks,
      isStrictMode,
      identifierPrefix,
      formState,
      onUncaughtError,
      onCaughtError,
      onRecoverableError,
      onDefaultTransitionIndicator,
      transitionCallbacks);

  if (root == nullptr) {
    return nullptr;
  }

  root->jsRuntime = &jsRuntime;
  registerDefaultIndicator(runtime, root, root->onDefaultTransitionIndicator);

  Value rootContext = createEmptyContext(jsRuntime);
  assignContextSlot(root->context, jsRuntime, rootContext);
  assignContextSlot(root->pendingContext, jsRuntime, rootContext);

  FiberNode* current = root->current;
  if (current == nullptr) {
    return root;
  }

  Lane lane = requestUpdateLane(runtime, *current);
  if (enableHydrationLaneScheduling) {
    lane = getBumpedLaneForHydrationByLane(lane);
  }

  auto update = createUpdate(lane);
  if (callback != nullptr) {
    (void)callback;
  }

  enqueueUpdate(*current, std::move(update), lane);
  scheduleInitialHydrationOnRoot(runtime, jsRuntime, *root, lane);

  return root;
}

Lane updateContainer(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const ReactNodeList& element,
    FiberRoot* container,
    facebook::jsi::Object* parentComponent,
    facebook::jsi::Function* callback) {
  if (container == nullptr || container->current == nullptr) {
    return NoLane;
  }

  FiberNode* rootFiber = container->current;

  Lane lane = requestUpdateLane(runtime, *rootFiber);
  updateContainerImpl(runtime, jsRuntime, *rootFiber, *container, lane, element, parentComponent, callback);
  return lane;
}

Lane updateContainerSync(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const ReactNodeList& element,
    FiberRoot* container,
    facebook::jsi::Object* parentComponent,
    facebook::jsi::Function* callback) {
  if (container == nullptr || container->current == nullptr) {
    return NoLane;
  }

  FiberNode* rootFiber = container->current;

  updateContainerImpl(runtime, jsRuntime, *rootFiber, *container, SyncLane, element, parentComponent, callback);
  return SyncLane;
}

bool injectIntoDevTools() {
  throwNotImplemented("injectIntoDevTools");
}

PublicInstance findHostInstance(const facebook::jsi::Object&) {
  throwNotImplemented("findHostInstance");
}

PublicInstance findHostInstanceWithWarning(const facebook::jsi::Object&, const std::string&) {
  throwNotImplemented("findHostInstanceWithWarning");
}

PublicInstance getPublicRootInstance(FiberRoot*) {
  throwNotImplemented("getPublicRootInstance");
}

void attemptSynchronousHydration(FiberNode*) {
  throwNotImplemented("attemptSynchronousHydration");
}

void attemptContinuousHydration(FiberNode*) {
  throwNotImplemented("attemptContinuousHydration");
}

void attemptHydrationAtCurrentPriority(FiberNode*) {
  throwNotImplemented("attemptHydrationAtCurrentPriority");
}

PublicInstance findHostInstanceWithNoPortals(FiberNode*) {
  throwNotImplemented("findHostInstanceWithNoPortals");
}

bool shouldError(FiberNode*) {
  throwNotImplemented("shouldError");
}

bool shouldSuspend(FiberNode*) {
  throwNotImplemented("shouldSuspend");
}

} // namespace react
