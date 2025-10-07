#include "ReactReconciler/ReactFiberWorkLoop.h"

#include "ReactReconciler/ReactCapturedValue.h"
#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberChild.h"
#include "ReactReconciler/ReactFiberErrorLogger.h"
#include "ReactReconciler/ReactFiberNewContext.h"
#include "ReactReconciler/ReactUpdateQueue.h"
#include "ReactReconciler/ReactWakeable.h"
#include "ReactReconciler/ReactFiberSuspenseContext.h"
#include "ReactReconciler/ReactFiberThrow.h"
#include "ReactReconciler/ReactFiberSuspenseContext.h"
#include "ReactReconciler/ReactFiberRootScheduler.h"
#include "ReactReconciler/ReactHostConfig.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactFeatureFlags.h"

#include "jsi/jsi.h"

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace react {

namespace {

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

struct HostRootMemoizedState {
  void* element{nullptr};
  bool isDehydrated{false};
  void* cache{nullptr};
};

const Value* asJsiValue(const void* storage) {
  return static_cast<const Value*>(storage);
}

Value cloneJsiValue(Runtime& jsRuntime, const void* storage) {
  if (storage == nullptr) {
    return Value::undefined();
  }
  return Value(jsRuntime, *asJsiValue(storage));
}

double clampNumber(double value) {
  if (std::isnan(value) || !std::isfinite(value)) {
    return 0.0;
  }
  return value;
}

std::string valueToString(Runtime& jsRuntime, const Value& value) {
  if (value.isString()) {
    return value.getString(jsRuntime).utf8(jsRuntime);
  }
  if (value.isNumber()) {
    std::ostringstream out;
    out << clampNumber(value.getNumber());
    return out.str();
  }
  if (value.isBool()) {
    return value.getBool() ? "true" : "false";
  }
  return std::string{};
}

Object ensureObject(Runtime& jsRuntime, const Value& value) {
  if (value.isObject()) {
    return value.getObject(jsRuntime);
  }
  return Object(jsRuntime);
}

std::string getFiberType(Runtime& jsRuntime, const FiberNode& fiber) {
  Value typeValue = cloneJsiValue(jsRuntime, fiber.type);
  if (!typeValue.isString()) {
    return std::string{};
  }
  return typeValue.getString(jsRuntime).utf8(jsRuntime);
}

hostconfig::HostInstance* asHostInstanceSlot(void* stateNode) {
  return static_cast<hostconfig::HostInstance*>(stateNode);
}

hostconfig::HostInstance getHostInstance(const FiberNode& fiber) {
  auto* slot = asHostInstanceSlot(fiber.stateNode);
  if (slot == nullptr) {
    return nullptr;
  }
  return *slot;
}

void setHostInstance(FiberNode& fiber, hostconfig::HostInstance instance) {
  auto* slot = asHostInstanceSlot(fiber.stateNode);
  if (slot == nullptr) {
    fiber.stateNode = new hostconfig::HostInstance(std::move(instance));
  } else {
    *slot = std::move(instance);
  }
}

void appendHostChildrenRecursive(
    ReactRuntime& runtime,
    FiberNode* node,
    const hostconfig::HostInstance& parent) {
  if (node == nullptr || !parent) {
    return;
  }

  if (node->tag == WorkTag::HostComponent || node->tag == WorkTag::HostText) {
    auto childInstance = getHostInstance(*node);
    if (childInstance) {
      hostconfig::appendInitialChild(runtime, parent, childInstance);
    }
    return;
  }

  for (FiberNode* child = node->child; child != nullptr; child = child->sibling) {
    appendHostChildrenRecursive(runtime, child, parent);
  }
}

void appendAllChildren(
    ReactRuntime& runtime,
    FiberNode& workInProgress,
    const hostconfig::HostInstance& parent) {
  for (FiberNode* child = workInProgress.child; child != nullptr; child = child->sibling) {
    appendHostChildrenRecursive(runtime, child, parent);
  }
}

void storeHostUpdatePayload(Runtime& jsRuntime, FiberNode& fiber, const Value& payload) {
  if (payload.isUndefined()) {
    fiber.updatePayload.reset();
    return;
  }
  fiber.updatePayload = std::make_unique<Value>(jsRuntime, payload);
}

void clearHostUpdatePayload(FiberNode& fiber) {
  fiber.updatePayload.reset();
}

void markRef(const FiberNode* current, FiberNode& workInProgress) {
  void* const ref = workInProgress.ref;
  if (ref == nullptr) {
    if (current != nullptr && current->ref != nullptr) {
      workInProgress.flags |= (Ref | RefStatic);
    }
    return;
  }

  if (current == nullptr || current->ref != ref) {
    workInProgress.flags |= (Ref | RefStatic);
  }
}

std::unordered_set<void*>& legacyErrorBoundariesThatAlreadyFailed() {
  static std::unordered_set<void*> instances;
  return instances;
}

std::unique_ptr<FiberNode::Dependencies> cloneDependencies(
    const std::unique_ptr<FiberNode::Dependencies>& source) {
  if (!source) {
    return nullptr;
  }

  auto clone = std::make_unique<FiberNode::Dependencies>();
  clone->lanes = source->lanes;
  clone->firstContext = cloneContextDependencies(source->firstContext);
  return clone;
}

void pushTreeContext(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: wire tree context stack once translated.
}

void pushRootMarkerInstance(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: integrate transition tracing marker stack when enabled.
}

void pushRootTransition(FiberNode& workInProgress, FiberRoot& root, Lanes renderLanes) {
  (void)workInProgress;
  (void)root;
  (void)renderLanes;
  // TODO: translate ReactFiberTransition stack push logic.
}

void pushHostContainer(FiberNode& workInProgress, void* container) {
  (void)workInProgress;
  (void)container;
  // TODO: integrate host context stack push once HostConfig is available.
}

void pushTopLevelLegacyContextObject(FiberNode& workInProgress, void* context, bool didChange) {
  (void)workInProgress;
  (void)context;
  (void)didChange;
  // TODO: translate legacy context stack push.
}

void pushCacheProvider(FiberNode& workInProgress, void* cache) {
  (void)workInProgress;
  (void)cache;
  // TODO: track cache provider stack when cache component is ported.
}

void pushHostRootContext(FiberNode& workInProgress) {
  auto* const fiberRoot = static_cast<FiberRoot*>(workInProgress.stateNode);
  if (fiberRoot == nullptr) {
    return;
  }

  if (fiberRoot->pendingContext != nullptr) {
    const bool didChange = fiberRoot->pendingContext != fiberRoot->context;
    pushTopLevelLegacyContextObject(workInProgress, fiberRoot->pendingContext, didChange);
  } else if (fiberRoot->context != nullptr) {
    pushTopLevelLegacyContextObject(workInProgress, fiberRoot->context, false);
  }

  pushHostContainer(workInProgress, fiberRoot->containerInfo);
}

void popTreeContext(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: wire tree context stack once translated.
}

void popRootMarkerInstance(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: integrate tracing marker stack when transition tracing is enabled.
}

bool hasLegacyContextChanged() {
  // TODO: consult legacy context tracking once translated.
  return false;
}

bool checkScheduledUpdateOrContext(FiberNode& current, Lanes renderLanes) {
  if (includesSomeLane(current.lanes, renderLanes)) {
    return true;
  }

  const auto* dependencies = current.dependencies.get();
  if (dependencies != nullptr) {
    if (includesSomeLane(dependencies->lanes, renderLanes)) {
      return true;
    }
  }

  return false;
}

FiberNode* attemptEarlyBailoutIfNoScheduledUpdate(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (current != nullptr) {
    workInProgress.dependencies = cloneDependencies(current->dependencies);
  }

  markSkippedUpdateLanes(runtime, workInProgress.lanes);

  if (!includesSomeLane(renderLanes, workInProgress.childLanes)) {
    return nullptr;
  }

  // TODO: push relevant context stacks for bailout cases once those stacks are ported.
  return cloneChildFibers(current, workInProgress);
}

bool isForkedChild(const FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: integrate tree id tracking for hydration.
  return false;
}

void handleForkedChildDuringHydration(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: push tree ids for forked hydration children.
}

FiberNode* mountLazyComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* elementType,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)elementType;
  (void)renderLanes;
  // TODO: translate mountLazyComponent.
  return nullptr;
}

FiberNode* updateFunctionComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* component,
    void* pendingProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)component;
  (void)pendingProps;
  (void)renderLanes;
  // TODO: translate updateFunctionComponent.
  return workInProgress.child;
}

FiberNode* updateClassComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* component,
    void* resolvedProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)component;
  (void)resolvedProps;
  (void)renderLanes;
  // TODO: translate updateClassComponent.
  return workInProgress.child;
}

FiberNode* updateHostComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)renderLanes;

  const std::string type = getFiberType(jsRuntime, workInProgress);
  Value nextProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  bool isDirectTextChild = false;
  if (!type.empty() && nextProps.isObject()) {
    Object nextPropsObject = nextProps.getObject(jsRuntime);
    isDirectTextChild = hostconfig::shouldSetTextContent(jsRuntime, type, nextPropsObject);
    if (isDirectTextChild) {
      workInProgress.child = nullptr;
    }
  }

  clearHostUpdatePayload(workInProgress);

  return workInProgress.child;
}

FiberNode* updateHostHoistable(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateHostHoistable.
  return workInProgress.child;
}

FiberNode* updateHostSingleton(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateHostSingleton.
  return workInProgress.child;
}

FiberNode* updateHostText(FiberNode* current, FiberNode& workInProgress) {
  (void)current;
  (void)workInProgress;
  return nullptr;
}

FiberNode* updateSuspenseComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateSuspenseComponent.
  return workInProgress.child;
}

FiberNode* updatePortalComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;

  void* containerInfo = getPortalContainerInfo(workInProgress);
  pushHostContainer(workInProgress, containerInfo);

  Value nextChildren = cloneJsiValue(jsRuntime, workInProgress.pendingProps);

  if (current == nullptr) {
    FiberNode* child = reconcileChildFibers(
        jsRuntime,
        nullptr,
        workInProgress,
        nextChildren,
        renderLanes);
    workInProgress.child = child;
  } else {
    reconcileChildren(runtime, jsRuntime, current, workInProgress, nextChildren, renderLanes);
  }

  return workInProgress.child;
}

FiberNode* updateForwardRef(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* elementType,
    void* pendingProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)elementType;
  (void)pendingProps;
  (void)renderLanes;
  // TODO: translate updateForwardRef.
  return workInProgress.child;
}

FiberNode* reconcileChildren(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    const Value& nextChildren,
    Lanes renderLanes) {
  (void)runtime;

  if (current == nullptr) {
    return mountChildFibers(jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  return reconcileChildFibers(jsRuntime, current->child, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateFragment(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (enableFragmentRefs) {
    markRef(current, workInProgress);
  }

  Value nextChildren = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  FiberNode* nextChild = reconcileChildren(runtime, jsRuntime, current, workInProgress, nextChildren, renderLanes);
  workInProgress.memoizedProps = workInProgress.pendingProps;
  return nextChild;
}

FiberNode* updateMode(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  Value nextProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value nextChildren = Value::undefined();
  if (nextProps.isObject()) {
    Object propsObject = nextProps.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "children")) {
      nextChildren = propsObject.getProperty(jsRuntime, "children");
    }
  }

  FiberNode* nextChild = reconcileChildren(runtime, jsRuntime, current, workInProgress, nextChildren, renderLanes);
  workInProgress.memoizedProps = workInProgress.pendingProps;
  return nextChild;
}

FiberNode* updateProfiler(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)current;

  if (enableProfilerTimer) {
    workInProgress.flags |= Update;

    if (enableProfilerCommitHooks) {
      workInProgress.flags |= Passive;
      // TODO: set effectDuration/passiveEffectDuration on profiler state node once available.
    }
  }

  Value nextProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value nextChildren = Value::undefined();
  if (nextProps.isObject()) {
    Object propsObject = nextProps.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "children")) {
      nextChildren = propsObject.getProperty(jsRuntime, "children");
    }
  }

  FiberNode* nextChild = reconcileChildren(runtime, jsRuntime, current, workInProgress, nextChildren, renderLanes);
  workInProgress.memoizedProps = workInProgress.pendingProps;
  return nextChild;
}

FiberNode* updateContextProvider(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;

  Value contextValue = cloneJsiValue(jsRuntime, workInProgress.type);

  Value newProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value newValue = Value::undefined();
  Value newChildren = Value::undefined();

  if (newProps.isObject()) {
    Object propsObject = newProps.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "value")) {
      newValue = propsObject.getProperty(jsRuntime, "value");
    }
    if (propsObject.hasProperty(jsRuntime, "children")) {
      newChildren = propsObject.getProperty(jsRuntime, "children");
    }
  }

  pushProvider(jsRuntime, workInProgress, contextValue, newValue);

  return reconcileChildren(runtime, jsRuntime, current, workInProgress, newChildren, renderLanes);
}

FiberNode* updateContextConsumer(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;

  Value consumerType = cloneJsiValue(jsRuntime, workInProgress.type);
  Value contextValue = Value::undefined();
  if (consumerType.isObject()) {
    Object typeObject = consumerType.getObject(jsRuntime);
    if (typeObject.hasProperty(jsRuntime, "_context")) {
      contextValue = typeObject.getProperty(jsRuntime, "_context");
    }
  }

  Value newProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value renderValue = Value::undefined();
  if (newProps.isObject()) {
    Object propsObject = newProps.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "children")) {
      renderValue = propsObject.getProperty(jsRuntime, "children");
    }
  }

  prepareToReadContext(workInProgress, renderLanes);

  const Value nextValue = readContext(jsRuntime, workInProgress, contextValue);

  Value newChildren = Value::undefined();
  if (renderValue.isObject()) {
    Object renderObject = renderValue.getObject(jsRuntime);
    if (renderObject.isFunction(jsRuntime)) {
      Function renderFunction = renderObject.asFunction(jsRuntime);
      const Value* args = &nextValue;
      newChildren = renderFunction.call(jsRuntime, Value::undefined(), args, 1);
    }
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  return reconcileChildren(runtime, jsRuntime, current, workInProgress, newChildren, renderLanes);
}

FiberNode* updateMemoComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* componentType,
    void* pendingProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)componentType;
  (void)pendingProps;
  (void)renderLanes;
  // TODO: translate updateMemoComponent.
  return workInProgress.child;
}

FiberNode* updateSimpleMemoComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* componentType,
    void* pendingProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)componentType;
  (void)pendingProps;
  (void)renderLanes;
  // TODO: translate updateSimpleMemoComponent.
  return workInProgress.child;
}

FiberNode* mountIncompleteClassComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* component,
    void* resolvedProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)component;
  (void)resolvedProps;
  (void)renderLanes;
  // TODO: translate mountIncompleteClassComponent.
  return workInProgress.child;
}

FiberNode* mountIncompleteFunctionComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* component,
    void* resolvedProps,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)component;
  (void)resolvedProps;
  (void)renderLanes;
  // TODO: translate mountIncompleteFunctionComponent.
  return workInProgress.child;
}

FiberNode* updateSuspenseListComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateSuspenseListComponent.
  return workInProgress.child;
}

FiberNode* updateScopeComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  Value nextProps = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value nextChildren = Value::undefined();
  if (nextProps.isObject()) {
    Object propsObject = nextProps.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "children")) {
      nextChildren = propsObject.getProperty(jsRuntime, "children");
    }
  }

  markRef(current, workInProgress);

  return reconcileChildren(
      runtime,
      jsRuntime,
      current,
      workInProgress,
      nextChildren,
      renderLanes);
}

FiberNode* updateActivityComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateActivityComponent.
  return workInProgress.child;
}

FiberNode* updateOffscreenComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes,
    void* pendingProps) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  (void)pendingProps;
  // TODO: translate updateOffscreenComponent.
  return workInProgress.child;
}

FiberNode* updateLegacyHiddenComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateLegacyHiddenComponent.
  return workInProgress.child;
}

FiberNode* updateCacheComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateCacheComponent.
  return workInProgress.child;
}

FiberNode* updateTracingMarkerComponent(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateTracingMarkerComponent.
  return workInProgress.child;
}

FiberNode* updateViewTransition(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)runtime;
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: translate updateViewTransition.
  return workInProgress.child;
}
FiberNode* updateHostRoot(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  auto* fiberRoot = static_cast<FiberRoot*>(workInProgress.stateNode);
  if (fiberRoot == nullptr) {
    return workInProgress.child;
  }

  pushHostRootContext(workInProgress);
  pushRootTransition(workInProgress, *fiberRoot, renderLanes);
  if (enableTransitionTracing) {
    pushRootMarkerInstance(workInProgress);
  }

  auto* prevState = current != nullptr ? static_cast<HostRootMemoizedState*>(current->memoizedState) : nullptr;
  auto* nextState = static_cast<HostRootMemoizedState*>(workInProgress.memoizedState);
  if (nextState == nullptr) {
    nextState = new HostRootMemoizedState();
    if (prevState != nullptr) {
      *nextState = *prevState;
    }
    workInProgress.memoizedState = nextState;
  } else if (prevState != nullptr && nextState != prevState) {
    *nextState = *prevState;
  }

  nextState->isDehydrated = fiberRoot->hostRootState.isDehydrated;

  pushCacheProvider(workInProgress, nextState->cache);

  suspendIfUpdateReadFromEntangledAsyncAction(runtime);

  fiberRoot->hostRootState.isDehydrated = nextState->isDehydrated;

  // Hydration and reconciliation are not yet supported in the translated runtime.
  // The child fiber will be reused as-is until the full beginWork flow is ported.
  return workInProgress.child;
}

void popRootTransition(FiberNode& workInProgress, FiberRoot& root, Lanes renderLanes) {
  (void)workInProgress;
  (void)root;
  (void)renderLanes;
  // TODO: integrate ReactFiberTransition stack once it lands.
}

void popHostContainer(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: wire host context stack when HostConfig is available.
}

void popTopLevelLegacyContextObject(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: translate legacy context stack when legacy context is supported.
}

bool popHydrationState(FiberNode& workInProgress) {
  (void)workInProgress;
  // Hydration is not yet supported; always report non-hydrated.
  return false;
}

void emitPendingHydrationWarnings() {
  // TODO: surface hydration diagnostics when hydration support is added.
}

void upgradeHydrationErrorsToRecoverable() {
  // TODO: queue hydration warnings for commit logging.
}

void popCacheProvider(FiberNode& workInProgress, void* cache) {
  (void)workInProgress;
  (void)cache;
  // TODO: release cache references when cache component is ported.
}

void markUpdate(FiberNode& workInProgress) {
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Update);
}

void updateHostContainer(FiberNode* current, FiberNode& workInProgress) {
  auto* fiberRoot = static_cast<FiberRoot*>(workInProgress.stateNode);
  if (fiberRoot == nullptr) {
    return;
  }

  void* nextContainer = workInProgress.pendingProps;
  if (nextContainer == nullptr && current != nullptr) {
    nextContainer = current->pendingProps;
  }
  if (nextContainer != nullptr) {
    fiberRoot->containerInfo = nextContainer;
  }
}

void pingSuspendedRoot(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberRoot& root,
  const Wakeable* wakeable,
  Lanes pingedLanes) {
  if (wakeable != nullptr) {
    root.pingCache.erase(wakeable);
  }

  markRootPinged(root, pingedLanes);

  FiberRoot* const workInProgressRoot = getWorkInProgressRoot(runtime);
  if (workInProgressRoot == &root) {
    const Lanes renderLanes = getWorkInProgressRootRenderLanes(runtime);
    if (isSubsetOfLanes(renderLanes, pingedLanes)) {
      const RootExitStatus exitStatus = getWorkInProgressRootExitStatus(runtime);
      const bool shouldResetStack =
          exitStatus == RootExitStatus::SuspendedWithDelay ||
          (exitStatus == RootExitStatus::Suspended &&
           includesOnlyRetries(renderLanes) &&
           (runtime.now() - getGlobalMostRecentFallbackTime(runtime)) < fallbackThrottleMs);

      if (shouldResetStack) {
        if ((getExecutionContext(runtime) & RenderContext) == NoContext) {
          prepareFreshStack(runtime, root, NoLanes);
        }
      } else {
        const Lanes accumulatedPingedLanes = mergeLanes(
            getWorkInProgressRootPingedLanes(runtime), pingedLanes);
        setWorkInProgressRootPingedLanes(runtime, accumulatedPingedLanes);
      }

      if (getWorkInProgressSuspendedRetryLanes(runtime) == renderLanes) {
        setWorkInProgressSuspendedRetryLanes(runtime, NoLanes);
      }
    }
  }

  ensureRootIsScheduled(runtime, jsRuntime, root);
}

inline WorkLoopState& getState(ReactRuntime& runtime) {
  return runtime.workLoopState();
}

inline void cancelTimeout(TimeoutHandle) {
  // TODO: integrate host-specific timeout cancellation.
}

void resetSuspendedWorkLoopOnUnwind(FiberNode* fiber) {
  (void)fiber;
  // TODO: reset module-level state for suspended work when unwind support lands.
}

void unwindInterruptedWork(FiberNode* current, FiberNode* workInProgress, Lanes renderLanes) {
  (void)current;
  (void)workInProgress;
  (void)renderLanes;
  // TODO: implement unwind semantics once effect and context stacks are available.
}

FiberFlags bubbleProperties(FiberNode& completedWork) {
  FiberFlags subtreeFlags = NoFlags;
  Lanes childLanes = NoLanes;

  for (FiberNode* child = completedWork.child; child != nullptr; child = child->sibling) {
    childLanes = mergeLanes(childLanes, child->lanes);
    childLanes = mergeLanes(childLanes, child->childLanes);

    const FiberFlags childFlags = static_cast<FiberFlags>(child->flags & static_cast<FiberFlags>(~StaticMask));
    subtreeFlags = static_cast<FiberFlags>(subtreeFlags | childFlags | child->subtreeFlags);
  }

  completedWork.childLanes = childLanes;
  const FiberFlags staticSubtreeFlags = completedWork.subtreeFlags & StaticMask;
  completedWork.subtreeFlags = static_cast<FiberFlags>(staticSubtreeFlags | subtreeFlags);
  return subtreeFlags;
}

FiberNode* completeWork(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes entangledRenderLanes) {
  if (workInProgress == nullptr) {
    return nullptr;
  }

  popTreeContext(*workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* fiberRoot = static_cast<FiberRoot*>(workInProgress->stateNode);
      if (fiberRoot == nullptr) {
        bubbleProperties(*workInProgress);
        break;
      }

      if (enableTransitionTracing) {
        if (!getWorkInProgressTransitions(runtime).empty()) {
          workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Passive);
        }
        popRootMarkerInstance(*workInProgress);
      }

      popCacheProvider(*workInProgress, nullptr);

      popRootTransition(*workInProgress, *fiberRoot, entangledRenderLanes);
      popHostContainer(*workInProgress);
      popTopLevelLegacyContextObject(*workInProgress);

      if (fiberRoot->pendingContext != nullptr) {
        fiberRoot->context = fiberRoot->pendingContext;
        fiberRoot->pendingContext = nullptr;
      }

      const bool isInitialRender = current == nullptr || current->child == nullptr;
      if (isInitialRender) {
        const bool wasHydrated = popHydrationState(*workInProgress);
        if (wasHydrated) {
          emitPendingHydrationWarnings();
          fiberRoot->hostRootState.isDehydrated = false;
          markUpdate(*workInProgress);
        } else if (current != nullptr) {
          const bool prevWasDehydrated = fiberRoot->hostRootState.isDehydrated;
          const bool wasForcedClientRender = (workInProgress->flags & ForceClientRender) != 0;
          if (!prevWasDehydrated || wasForcedClientRender) {
            workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Snapshot);
            upgradeHydrationErrorsToRecoverable();
          }
        }
      }

      updateHostContainer(current, *workInProgress);
      bubbleProperties(*workInProgress);

      if (enableTransitionTracing) {
        if ((workInProgress->subtreeFlags & Visibility) != NoFlags) {
          workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Passive);
        }
      }
      break;
    }
    case WorkTag::HostComponent: {
      const std::string type = getFiberType(jsRuntime, *workInProgress);
      Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->memoizedProps);
      if (nextPropsValue.isUndefined()) {
        nextPropsValue = cloneJsiValue(jsRuntime, workInProgress->pendingProps);
      }
      Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

      if (current != nullptr && current->stateNode != nullptr) {
        Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
        Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);
        if (!payload.isUndefined()) {
          storeHostUpdatePayload(jsRuntime, *workInProgress, payload);
          markUpdate(*workInProgress);
        } else {
          clearHostUpdatePayload(*workInProgress);
        }

        // Reuse existing host instance pointer from current fiber.
        if (workInProgress->stateNode == nullptr) {
          auto instance = getHostInstance(*current);
          if (instance) {
            setHostInstance(*workInProgress, instance);
          }
        }

        bubbleProperties(*workInProgress);
        break;
      }

      if (type.empty()) {
        bubbleProperties(*workInProgress);
        break;
      }

      auto instance = hostconfig::createInstance(runtime, jsRuntime, type, nextPropsObject);
      setHostInstance(*workInProgress, instance);

      appendAllChildren(runtime, *workInProgress, instance);

      if (hostconfig::finalizeInitialChildren(runtime, jsRuntime, instance, type, nextPropsObject)) {
        markUpdate(*workInProgress);
      }

      clearHostUpdatePayload(*workInProgress);
      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::HostText: {
      Value nextTextValue = cloneJsiValue(jsRuntime, workInProgress->memoizedProps);
      if (nextTextValue.isUndefined()) {
        nextTextValue = cloneJsiValue(jsRuntime, workInProgress->pendingProps);
      }
      const std::string nextText = valueToString(jsRuntime, nextTextValue);

      if (current != nullptr && current->stateNode != nullptr) {
  Value prevTextValue = cloneJsiValue(jsRuntime, current->memoizedProps);
  const std::string prevText = valueToString(jsRuntime, prevTextValue);
        if (nextText != prevText) {
          markUpdate(*workInProgress);
        }

        if (workInProgress->stateNode == nullptr) {
          auto instance = getHostInstance(*current);
          if (instance) {
            setHostInstance(*workInProgress, instance);
          }
        }

      } else {
        auto textInstance = hostconfig::createTextInstance(runtime, jsRuntime, nextText);
        setHostInstance(*workInProgress, textInstance);
      }

      bubbleProperties(*workInProgress);
      break;
    }
    case WorkTag::Fragment:
    case WorkTag::Mode:
    case WorkTag::ContextProvider:
    case WorkTag::ContextConsumer:
    case WorkTag::Profiler:
    case WorkTag::SuspenseComponent:
    case WorkTag::OffscreenComponent:
    case WorkTag::CacheComponent:
    case WorkTag::MemoComponent:
    case WorkTag::ForwardRef:
    case WorkTag::SimpleMemoComponent:
    case WorkTag::FunctionComponent:
    case WorkTag::ClassComponent:
    default:
      bubbleProperties(*workInProgress);
      break;
  }

  if (entangledRenderLanes != NoLanes) {
    const Lanes entangledChildren = intersectLanes(entangledRenderLanes, workInProgress->childLanes);
    if (entangledChildren != NoLanes) {
      workInProgress->childLanes = mergeLanes(workInProgress->childLanes, entangledChildren);
      workInProgress->lanes = mergeLanes(workInProgress->lanes, entangledChildren);
    }
  }

  return nullptr;
}

FiberNode* unwindWork(FiberNode* current, FiberNode* workInProgress, Lanes entangledRenderLanes) {
  (void)current;
  (void)entangledRenderLanes;

  if (workInProgress == nullptr) {
    return nullptr;
  }

  resetSuspendedWorkLoopOnUnwind(workInProgress);

  workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Incomplete);
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->childLanes = NoLanes;
  workInProgress->deletions.clear();

  return workInProgress->returnFiber;
}

void startProfilerTimer(FiberNode&) {
  // TODO: integrate ReactProfilerTimer when available.
}

void stopProfilerTimerIfRunningAndRecordIncompleteDuration(FiberNode&) {
  // TODO: integrate ReactProfilerTimer when available.
}

FiberNode* beginWork(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes renderLanes) {
  if (workInProgress == nullptr) {
    return nullptr;
  }

  workInProgress->flags &= StaticMask;
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->deletions.clear();

  bool didReceiveUpdate = false;

  if (current != nullptr) {
    workInProgress->childLanes = current->childLanes;
    if (current->dependencies != nullptr) {
      workInProgress->dependencies = cloneDependencies(current->dependencies);
    }

    void* const oldProps = current->memoizedProps;
    void* const newProps = workInProgress->pendingProps;

    if (oldProps != newProps || hasLegacyContextChanged()) {
      didReceiveUpdate = true;
    } else {
      const bool hasScheduledUpdateOrContext = checkScheduledUpdateOrContext(*current, renderLanes);
      if (!hasScheduledUpdateOrContext && (workInProgress->flags & DidCapture) == 0) {
        setDidReceiveUpdate(runtime, false);
        return attemptEarlyBailoutIfNoScheduledUpdate(runtime, current, *workInProgress, renderLanes);
      }

      if ((current->flags & ForceUpdateForLegacySuspense) != 0) {
        didReceiveUpdate = true;
      }
    }

  } else {
    workInProgress->childLanes = NoLanes;

    if (getIsHydrating() && isForkedChild(*workInProgress)) {
      handleForkedChildDuringHydration(*workInProgress);
    }
  }

  setDidReceiveUpdate(runtime, didReceiveUpdate);

  workInProgress->lanes = NoLanes;

  switch (workInProgress->tag) {
    case WorkTag::LazyComponent: {
      return mountLazyComponent(runtime, current, *workInProgress, workInProgress->elementType, renderLanes);
    }
    case WorkTag::FunctionComponent: {
      return updateFunctionComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    }
    case WorkTag::ClassComponent: {
      return updateClassComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    }
    case WorkTag::HostRoot:
      return updateHostRoot(runtime, current, *workInProgress, renderLanes);
    case WorkTag::HostHoistable:
      return updateHostHoistable(runtime, current, *workInProgress, renderLanes);
    case WorkTag::HostSingleton:
      return updateHostSingleton(runtime, current, *workInProgress, renderLanes);
    case WorkTag::HostComponent:
      return updateHostComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::HostText:
      return updateHostText(current, *workInProgress);
    case WorkTag::SuspenseComponent:
      return updateSuspenseComponent(runtime, current, *workInProgress, renderLanes);
    case WorkTag::HostPortal:
      return updatePortalComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ForwardRef:
      return updateForwardRef(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    case WorkTag::Fragment:
      return updateFragment(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::Mode:
      return updateMode(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::Profiler:
      return updateProfiler(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ContextProvider:
      return updateContextProvider(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ContextConsumer:
      return updateContextConsumer(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::MemoComponent:
      return updateMemoComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    case WorkTag::SimpleMemoComponent:
      return updateSimpleMemoComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    case WorkTag::IncompleteClassComponent: {
      if (disableLegacyMode) {
        break;
      }
      return mountIncompleteClassComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    }
    case WorkTag::IncompleteFunctionComponent: {
      if (disableLegacyMode) {
        break;
      }
      return mountIncompleteFunctionComponent(
          runtime, current, *workInProgress, workInProgress->type, workInProgress->pendingProps, renderLanes);
    }
    case WorkTag::SuspenseListComponent:
      return updateSuspenseListComponent(runtime, current, *workInProgress, renderLanes);
    case WorkTag::ScopeComponent: {
      if (enableScopeAPI) {
        return updateScopeComponent(runtime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::ActivityComponent:
      return updateActivityComponent(runtime, current, *workInProgress, renderLanes);
    case WorkTag::OffscreenComponent:
      return updateOffscreenComponent(runtime, current, *workInProgress, renderLanes, workInProgress->pendingProps);
    case WorkTag::LegacyHiddenComponent: {
      if (enableLegacyHidden) {
        return updateLegacyHiddenComponent(runtime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::CacheComponent:
      return updateCacheComponent(runtime, current, *workInProgress, renderLanes);
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing) {
        return updateTracingMarkerComponent(runtime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::ViewTransitionComponent: {
      if (enableViewTransition) {
        return updateViewTransition(runtime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::Throw:
      throw workInProgress->pendingProps;
    default:
      // TODO: translate remaining work tags.
      return workInProgress->child;
  }

  return workInProgress->child;
}

void stopProfilerTimerIfRunningAndRecordDuration(FiberNode&) {
  // TODO: integrate ReactProfilerTimer when available.
}

bool shouldYield(ReactRuntime& runtime) {
  return runtime.shouldYield();
}

bool getIsHydrating() {
  // TODO: integrate hydration state once hydration support lands.
  return false;
}

} // namespace

bool isAlreadyFailedLegacyErrorBoundary(void* instance) {
  if (instance == nullptr) {
    return false;
  }
  const auto& instances = legacyErrorBoundariesThatAlreadyFailed();
  return instances.find(instance) != instances.end();
}

void markLegacyErrorBoundaryAsFailed(void* instance) {
  if (instance == nullptr) {
    return;
  }
  legacyErrorBoundariesThatAlreadyFailed().insert(instance);
}

void attachPingListener(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberRoot& root,
  Wakeable& wakeable,
  Lanes lanes) {
  auto& threadIds = root.pingCache[&wakeable];
  if (!threadIds.insert(lanes).second) {
    return;
  }

  setWorkInProgressRootDidAttachPingListener(runtime, true);

  auto ping = [&runtime, &jsRuntime, &root, wakeablePtr = &wakeable, lanes]() {
    pingSuspendedRoot(runtime, jsRuntime, root, wakeablePtr, lanes);
  };

  wakeable.then(ping, ping);
}

ExecutionContext getExecutionContext(ReactRuntime& runtime) {
  return getState(runtime).executionContext;
}

void setExecutionContext(ReactRuntime& runtime, ExecutionContext context) {
  getState(runtime).executionContext = context;
}

void pushExecutionContext(ReactRuntime& runtime, ExecutionContext context) {
  auto& state = getState(runtime);
  state.executionContext = static_cast<ExecutionContext>(state.executionContext | context);
}

void popExecutionContext(ReactRuntime& runtime, ExecutionContext context) {
  auto& state = getState(runtime);
  const auto mask = static_cast<ExecutionContext>(~context & 0xFFu);
  state.executionContext = static_cast<ExecutionContext>(state.executionContext & mask);
}

bool isAlreadyRendering(ReactRuntime& runtime) {
  const auto context = getState(runtime).executionContext;
  return (context & (RenderContext | CommitContext)) != NoContext;
}

bool isInvalidExecutionContextForEventFunction(ReactRuntime& runtime) {
  const auto context = getState(runtime).executionContext;
  return (context & RenderContext) != NoContext;
}

void setEntangledRenderLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).entangledRenderLanes = lanes;
}

Lanes getEntangledRenderLanes(ReactRuntime& runtime) {
  return getState(runtime).entangledRenderLanes;
}

FiberRoot* getWorkInProgressRoot(ReactRuntime& runtime) {
  return getState(runtime).workInProgressRoot;
}

void setWorkInProgressRoot(ReactRuntime& runtime, FiberRoot* root) {
  getState(runtime).workInProgressRoot = root;
}

FiberNode* getWorkInProgressFiber(ReactRuntime& runtime) {
  return getState(runtime).workInProgressFiber;
}

void setWorkInProgressFiber(ReactRuntime& runtime, FiberNode* fiber) {
  getState(runtime).workInProgressFiber = fiber;
}

Lanes getWorkInProgressRootRenderLanes(ReactRuntime& runtime) {
  return getState(runtime).workInProgressRootRenderLanes;
}

void setWorkInProgressRootRenderLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).workInProgressRootRenderLanes = lanes;
}

void* getWorkInProgressUpdateTask(ReactRuntime& runtime) {
  return getState(runtime).workInProgressUpdateTask;
}

void setWorkInProgressUpdateTask(ReactRuntime& runtime, void* task) {
  getState(runtime).workInProgressUpdateTask = task;
}

std::vector<const Transition*>& getWorkInProgressTransitions(ReactRuntime& runtime) {
  return getState(runtime).workInProgressTransitions;
}

void clearWorkInProgressTransitions(ReactRuntime& runtime) {
  getState(runtime).workInProgressTransitions.clear();
}

bool getDidIncludeCommitPhaseUpdate(ReactRuntime& runtime) {
  return getState(runtime).didIncludeCommitPhaseUpdate;
}

void setDidIncludeCommitPhaseUpdate(ReactRuntime& runtime, bool value) {
  getState(runtime).didIncludeCommitPhaseUpdate = value;
}

bool getDidReceiveUpdate(ReactRuntime& runtime) {
  return getState(runtime).didReceiveUpdate;
}

void setDidReceiveUpdate(ReactRuntime& runtime, bool value) {
  getState(runtime).didReceiveUpdate = value;
}

double getGlobalMostRecentFallbackTime(ReactRuntime& runtime) {
  return getState(runtime).globalMostRecentFallbackTime;
}

void setGlobalMostRecentFallbackTime(ReactRuntime& runtime, double value) {
  getState(runtime).globalMostRecentFallbackTime = value;
}

double getWorkInProgressRootRenderTargetTime(ReactRuntime& runtime) {
  return getState(runtime).workInProgressRootRenderTargetTime;
}

void setWorkInProgressRootRenderTargetTime(ReactRuntime& runtime, double value) {
  getState(runtime).workInProgressRootRenderTargetTime = value;
}

void* getCurrentPendingTransitionCallbacks(ReactRuntime& runtime) {
  return getState(runtime).currentPendingTransitionCallbacks;
}

void setCurrentPendingTransitionCallbacks(ReactRuntime& runtime, void* callbacks) {
  getState(runtime).currentPendingTransitionCallbacks = callbacks;
}

double getCurrentEndTime(ReactRuntime& runtime) {
  return getState(runtime).currentEndTime;
}

void setCurrentEndTime(ReactRuntime& runtime, double time) {
  getState(runtime).currentEndTime = time;
}

double getCurrentNewestExplicitSuspenseTime(ReactRuntime& runtime) {
  return getState(runtime).currentNewestExplicitSuspenseTime;
}

void setCurrentNewestExplicitSuspenseTime(ReactRuntime& runtime, double time) {
  getState(runtime).currentNewestExplicitSuspenseTime = time;
}

void markCommitTimeOfFallback(ReactRuntime& runtime) {
  setGlobalMostRecentFallbackTime(runtime, runtime.now());
}

void resetRenderTimer(ReactRuntime& runtime) {
  setWorkInProgressRootRenderTargetTime(runtime, runtime.now() + renderTimeoutMs);
}

double getRenderTargetTime(ReactRuntime& runtime) {
  return getWorkInProgressRootRenderTargetTime(runtime);
}

PendingEffectsStatus getPendingEffectsStatus(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsStatus;
}

void setPendingEffectsStatus(ReactRuntime& runtime, PendingEffectsStatus status) {
  getState(runtime).pendingEffectsStatus = status;
}

FiberRoot* getPendingEffectsRoot(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsRoot;
}

void setPendingEffectsRoot(ReactRuntime& runtime, FiberRoot* root) {
  getState(runtime).pendingEffectsRoot = root;
}

FiberNode* getPendingFinishedWork(ReactRuntime& runtime) {
  return getState(runtime).pendingFinishedWork;
}

void setPendingFinishedWork(ReactRuntime& runtime, FiberNode* fiber) {
  getState(runtime).pendingFinishedWork = fiber;
}

Lanes getPendingEffectsLanes(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsLanes;
}

void setPendingEffectsLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).pendingEffectsLanes = lanes;
}

Lanes getPendingEffectsRemainingLanes(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsRemainingLanes;
}

void setPendingEffectsRemainingLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).pendingEffectsRemainingLanes = lanes;
}

double getPendingEffectsRenderEndTime(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsRenderEndTime;
}

void setPendingEffectsRenderEndTime(ReactRuntime& runtime, double time) {
  getState(runtime).pendingEffectsRenderEndTime = time;
}

std::vector<const Transition*>& getPendingPassiveTransitions(ReactRuntime& runtime) {
  return getState(runtime).pendingPassiveTransitions;
}

void clearPendingPassiveTransitions(ReactRuntime& runtime) {
  getState(runtime).pendingPassiveTransitions.clear();
}

std::vector<void*>& getPendingRecoverableErrors(ReactRuntime& runtime) {
  return getState(runtime).pendingRecoverableErrors;
}

void clearPendingRecoverableErrors(ReactRuntime& runtime) {
  getState(runtime).pendingRecoverableErrors.clear();
}

void* getPendingViewTransition(ReactRuntime& runtime) {
  return getState(runtime).pendingViewTransition;
}

void setPendingViewTransition(ReactRuntime& runtime, void* transition) {
  getState(runtime).pendingViewTransition = transition;
}

std::vector<void*>& getPendingViewTransitionEvents(ReactRuntime& runtime) {
  return getState(runtime).pendingViewTransitionEvents;
}

void clearPendingViewTransitionEvents(ReactRuntime& runtime) {
  getState(runtime).pendingViewTransitionEvents.clear();
}

void* getPendingTransitionTypes(ReactRuntime& runtime) {
  return getState(runtime).pendingTransitionTypes;
}

void setPendingTransitionTypes(ReactRuntime& runtime, void* types) {
  getState(runtime).pendingTransitionTypes = types;
}

bool getPendingDidIncludeRenderPhaseUpdate(ReactRuntime& runtime) {
  return getState(runtime).pendingDidIncludeRenderPhaseUpdate;
}

void setPendingDidIncludeRenderPhaseUpdate(ReactRuntime& runtime, bool value) {
  getState(runtime).pendingDidIncludeRenderPhaseUpdate = value;
}

SuspendedCommitReason getPendingSuspendedCommitReason(ReactRuntime& runtime) {
  return getState(runtime).pendingSuspendedCommitReason;
}

void setPendingSuspendedCommitReason(ReactRuntime& runtime, SuspendedCommitReason reason) {
  getState(runtime).pendingSuspendedCommitReason = reason;
}

std::uint32_t getNestedUpdateCount(ReactRuntime& runtime) {
  return getState(runtime).nestedUpdateCount;
}

void setNestedUpdateCount(ReactRuntime& runtime, std::uint32_t count) {
  getState(runtime).nestedUpdateCount = count;
}

FiberRoot* getRootWithNestedUpdates(ReactRuntime& runtime) {
  return getState(runtime).rootWithNestedUpdates;
}

void setRootWithNestedUpdates(ReactRuntime& runtime, FiberRoot* root) {
  getState(runtime).rootWithNestedUpdates = root;
}

bool getIsFlushingPassiveEffects(ReactRuntime& runtime) {
  return getState(runtime).isFlushingPassiveEffects;
}

void setIsFlushingPassiveEffects(ReactRuntime& runtime, bool value) {
  getState(runtime).isFlushingPassiveEffects = value;
}

bool getDidScheduleUpdateDuringPassiveEffects(ReactRuntime& runtime) {
  return getState(runtime).didScheduleUpdateDuringPassiveEffects;
}

void setDidScheduleUpdateDuringPassiveEffects(ReactRuntime& runtime, bool value) {
  getState(runtime).didScheduleUpdateDuringPassiveEffects = value;
}

std::uint32_t getNestedPassiveUpdateCount(ReactRuntime& runtime) {
  return getState(runtime).nestedPassiveUpdateCount;
}

void setNestedPassiveUpdateCount(ReactRuntime& runtime, std::uint32_t count) {
  getState(runtime).nestedPassiveUpdateCount = count;
}

FiberRoot* getRootWithPassiveNestedUpdates(ReactRuntime& runtime) {
  return getState(runtime).rootWithPassiveNestedUpdates;
}

void setRootWithPassiveNestedUpdates(ReactRuntime& runtime, FiberRoot* root) {
  getState(runtime).rootWithPassiveNestedUpdates = root;
}

bool getIsRunningInsertionEffect(ReactRuntime& runtime) {
  return getState(runtime).isRunningInsertionEffect;
}

void setIsRunningInsertionEffect(ReactRuntime& runtime, bool value) {
  getState(runtime).isRunningInsertionEffect = value;
}

bool hasPendingCommitEffects(ReactRuntime& runtime) {
  const auto status = getState(runtime).pendingEffectsStatus;
  return status != PendingEffectsStatus::None && status != PendingEffectsStatus::Passive;
}

FiberRoot* getRootWithPendingPassiveEffects(ReactRuntime& runtime) {
  const auto& state = getState(runtime);
  return state.pendingEffectsStatus == PendingEffectsStatus::Passive ? state.pendingEffectsRoot : nullptr;
}

Lanes getPendingPassiveEffectsLanes(ReactRuntime& runtime) {
  return getState(runtime).pendingEffectsLanes;
}

bool isWorkLoopSuspendedOnData(ReactRuntime& runtime) {
  const auto reason = getState(runtime).suspendedReason;
  return reason == SuspendedReason::SuspendedOnData || reason == SuspendedReason::SuspendedOnAction;
}

double getCurrentTime(ReactRuntime& runtime) {
  return runtime.now();
}

void markSkippedUpdateLanes(ReactRuntime& runtime, Lanes lanes) {
  auto& state = getState(runtime);
  state.skippedLanes = mergeLanes(state.skippedLanes, lanes);
}

void renderDidSuspend(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  if (state.exitStatus == RootExitStatus::InProgress) {
    state.exitStatus = RootExitStatus::Suspended;
  }
}

void renderDidSuspendDelayIfPossible(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  state.exitStatus = RootExitStatus::SuspendedWithDelay;

  if (!state.didSkipSuspendedSiblings &&
      includesOnlyTransitions(state.workInProgressRootRenderLanes)) {
    state.isPrerendering = true;
  }

  const bool hasSkippedNonIdleWork =
      includesNonIdleWork(state.skippedLanes) ||
      includesNonIdleWork(state.interleavedUpdatedLanes);
  if (hasSkippedNonIdleWork && state.workInProgressRoot != nullptr) {
    constexpr bool didAttemptEntireTree = false;
    markRootSuspended(
        *state.workInProgressRoot,
        state.workInProgressRootRenderLanes,
        state.deferredLane,
        didAttemptEntireTree);
  }
}

void renderDidError(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  if (state.exitStatus != RootExitStatus::SuspendedWithDelay) {
    state.exitStatus = RootExitStatus::Errored;
  }
}

void queueConcurrentError(ReactRuntime& runtime, void* error) {
  auto& state = getState(runtime);
  state.concurrentErrors.push_back(error);
}

bool renderHasNotSuspendedYet(ReactRuntime& runtime) {
  return getState(runtime).exitStatus == RootExitStatus::InProgress;
}

void markSpawnedRetryLane(ReactRuntime& runtime, Lane lane) {
  auto& state = getState(runtime);
  state.suspendedRetryLanes = mergeLanes(state.suspendedRetryLanes, lane);
}

void performUnitOfWork(ReactRuntime& runtime, Runtime& jsRuntime, FiberNode& unitOfWork) {
  auto& state = getState(runtime);
  FiberNode* const current = unitOfWork.alternate;

  FiberNode* next = nullptr;
  const bool isProfiling = enableProfilerTimer && (unitOfWork.mode & ProfileMode) != NoMode;
  if (isProfiling) {
    startProfilerTimer(unitOfWork);
  }

  next = beginWork(runtime, jsRuntime, current, &unitOfWork, state.entangledRenderLanes);

  if (isProfiling) {
    stopProfilerTimerIfRunningAndRecordDuration(unitOfWork);
  }

  unitOfWork.memoizedProps = unitOfWork.pendingProps;
  if (next == nullptr) {
    completeUnitOfWork(runtime, jsRuntime, unitOfWork);
  } else {
    setWorkInProgressFiber(runtime, next);
  }
}

void workLoopSync(ReactRuntime& runtime, Runtime& jsRuntime) {
  while (FiberNode* workInProgress = getWorkInProgressFiber(runtime)) {
    performUnitOfWork(runtime, jsRuntime, *workInProgress);
  }
}

void workLoopConcurrent(ReactRuntime& runtime, Runtime& jsRuntime, bool nonIdle) {
  FiberNode* workInProgress = getWorkInProgressFiber(runtime);
  if (workInProgress == nullptr) {
    return;
  }

  const double slice = nonIdle ? 25.0 : 5.0;
  const double deadline = runtime.now() + slice;

  while ((workInProgress = getWorkInProgressFiber(runtime)) != nullptr && runtime.now() < deadline) {
    performUnitOfWork(runtime, jsRuntime, *workInProgress);
  }
}

void workLoopConcurrentByScheduler(ReactRuntime& runtime, Runtime& jsRuntime) {
  while (FiberNode* workInProgress = getWorkInProgressFiber(runtime)) {
    if (shouldYield(runtime)) {
      break;
    }
    performUnitOfWork(runtime, jsRuntime, *workInProgress);
  }
}

RootExitStatus renderRootSync(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes,
  bool shouldYieldForPrerendering) {
  (void)shouldYieldForPrerendering;

  pushExecutionContext(runtime, RenderContext);

  if (getWorkInProgressRoot(runtime) != &root ||
      getWorkInProgressRootRenderLanes(runtime) != lanes) {
    prepareFreshStack(runtime, root, lanes);
  }

  workLoopSync(runtime, jsRuntime);

  const RootExitStatus exitStatus = getWorkInProgressRootExitStatus(runtime);

  if (exitStatus == RootExitStatus::SuspendedAtTheShell &&
      !getWorkInProgressRootDidSkipSuspendedSiblings(runtime)) {
    setWorkInProgressRootDidSkipSuspendedSiblings(runtime, true);
  }

  if (exitStatus == RootExitStatus::SuspendedAtTheShell) {
    setWorkInProgressSuspendedReason(runtime, SuspendedReason::NotSuspended);
    setWorkInProgressThrownValue(runtime, nullptr);
  }

  if (getWorkInProgressFiber(runtime) == nullptr) {
    setWorkInProgressRoot(runtime, nullptr);
    setWorkInProgressRootRenderLanes(runtime, NoLanes);
    finishQueueingConcurrentUpdates();
  }

  popExecutionContext(runtime, RenderContext);

  return exitStatus;
}

RootExitStatus renderRootConcurrent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes) {
  pushExecutionContext(runtime, RenderContext);

  if (getWorkInProgressRoot(runtime) != &root ||
      getWorkInProgressRootRenderLanes(runtime) != lanes) {
    prepareFreshStack(runtime, root, lanes);
  } else {
    setWorkInProgressRootIsPrerendering(runtime, checkIfRootIsPrerendering(root, lanes));
  }

  bool shouldContinue = true;
  while (shouldContinue) {
    FiberNode* workInProgress = getWorkInProgressFiber(runtime);
    if (workInProgress == nullptr) {
      break;
    }

    const SuspendedReason suspendedReason = getWorkInProgressSuspendedReason(runtime);
    if (suspendedReason != SuspendedReason::NotSuspended) {
      void* const thrownValue = getWorkInProgressThrownValue(runtime);

      switch (suspendedReason) {
        case SuspendedReason::SuspendedOnHydration:
          resetWorkInProgressStack(runtime);
          setWorkInProgressRootExitStatus(runtime, RootExitStatus::SuspendedAtTheShell);
          shouldContinue = false;
          break;
        case SuspendedReason::SuspendedOnImmediate:
          setWorkInProgressSuspendedReason(runtime, SuspendedReason::SuspendedAndReadyToContinue);
          shouldContinue = false;
          break;
        case SuspendedReason::SuspendedAndReadyToContinue:
        case SuspendedReason::SuspendedOnInstanceAndReadyToContinue:
          setWorkInProgressSuspendedReason(runtime, SuspendedReason::NotSuspended);
          setWorkInProgressThrownValue(runtime, nullptr);
          continue;
        default:
          setWorkInProgressSuspendedReason(runtime, SuspendedReason::NotSuspended);
          setWorkInProgressThrownValue(runtime, nullptr);
          setWorkInProgressRootDidSkipSuspendedSiblings(runtime, true);
          throwAndUnwindWorkLoop(runtime, jsRuntime, root, *workInProgress, thrownValue, suspendedReason);
          break;
      }

      if (!shouldContinue) {
        break;
      }

      continue;
    }

    if (enableThrottledScheduling) {
      workLoopConcurrent(runtime, jsRuntime, includesNonIdleWork(lanes));
    } else {
      workLoopConcurrentByScheduler(runtime, jsRuntime);
    }
    shouldContinue = false;
  }

  const SuspendedReason finalSuspendedReason = getWorkInProgressSuspendedReason(runtime);
  if (
      finalSuspendedReason != SuspendedReason::SuspendedAndReadyToContinue &&
      finalSuspendedReason != SuspendedReason::SuspendedOnInstanceAndReadyToContinue) {
    setWorkInProgressSuspendedReason(runtime, SuspendedReason::NotSuspended);
    setWorkInProgressThrownValue(runtime, nullptr);
  }

  const RootExitStatus exitStatus = getWorkInProgressRootExitStatus(runtime);

  if (getWorkInProgressFiber(runtime) == nullptr) {
    setWorkInProgressRoot(runtime, nullptr);
    setWorkInProgressRootRenderLanes(runtime, NoLanes);
    finishQueueingConcurrentUpdates();
  }

  popExecutionContext(runtime, RenderContext);

  return exitStatus;
}

void throwAndUnwindWorkLoop(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
    FiberRoot& root,
    FiberNode& unitOfWork,
  void* thrownValue,
  SuspendedReason reason) {
  resetSuspendedWorkLoopOnUnwind(&unitOfWork);

  FiberNode* const returnFiber = unitOfWork.returnFiber;
  try {
    const bool didFatal = throwException(
        runtime,
        jsRuntime,
        root,
        returnFiber,
        unitOfWork,
        thrownValue,
        getWorkInProgressRootRenderLanes(runtime));
    if (didFatal) {
      panicOnRootError(runtime, root, thrownValue);
      return;
    }
  } catch (...) {
    if (returnFiber != nullptr) {
      setWorkInProgressFiber(runtime, returnFiber);
      throw;
    }

    panicOnRootError(runtime, root, thrownValue);
    return;
  }

  if ((unitOfWork.flags & Incomplete) != NoFlags) {
    bool skipSiblings = false;

    if (getIsHydrating() || reason == SuspendedReason::SuspendedOnError) {
      skipSiblings = true;
    } else if (
        !getWorkInProgressRootIsPrerendering(runtime) &&
        !includesSomeLane(
            getWorkInProgressRootRenderLanes(runtime), OffscreenLane)) {
      skipSiblings = true;
      setWorkInProgressRootDidSkipSuspendedSiblings(runtime, true);

      if (
          reason == SuspendedReason::SuspendedOnData ||
          reason == SuspendedReason::SuspendedOnAction ||
          reason == SuspendedReason::SuspendedOnImmediate ||
          reason == SuspendedReason::SuspendedOnDeprecatedThrowPromise) {
        if (FiberNode* const boundary = getSuspenseHandler()) {
          if (boundary->tag == WorkTag::SuspenseComponent) {
            boundary->flags = static_cast<FiberFlags>(boundary->flags | ScheduleRetry);

          }
        }
      }
    }

    unwindUnitOfWork(runtime, unitOfWork, skipSiblings);
  } else {
    completeUnitOfWork(runtime, jsRuntime, unitOfWork);
  }
}

void panicOnRootError(ReactRuntime& runtime, FiberRoot& root, void* error) {
  setWorkInProgressRootExitStatus(runtime, RootExitStatus::FatalErrored);

  CapturedValue captured{};
  if (root.current != nullptr) {
    captured = createCapturedValueAtFiber(error, root.current);
  } else {
    captured = createCapturedValueFromError(error, std::string{});
  }

  logUncaughtError(root, captured);
  setWorkInProgressFiber(runtime, nullptr);
}

void completeUnitOfWork(ReactRuntime& runtime, Runtime& jsRuntime, FiberNode& unitOfWork) {
  auto& state = getState(runtime);
  FiberNode* completedWork = &unitOfWork;

  do {
    if ((completedWork->flags & Incomplete) != NoFlags) {
      const bool skipSiblings = state.didSkipSuspendedSiblings;
      unwindUnitOfWork(runtime, *completedWork, skipSiblings);
      return;
    }

    FiberNode* current = completedWork->alternate;
    FiberNode* returnFiber = completedWork->returnFiber;

    startProfilerTimer(*completedWork);
  FiberNode* next = completeWork(runtime, jsRuntime, current, completedWork, state.entangledRenderLanes);
    if (enableProfilerTimer && (completedWork->mode & ProfileMode) != NoMode) {
      stopProfilerTimerIfRunningAndRecordIncompleteDuration(*completedWork);
    }

    if (next != nullptr) {
      setWorkInProgressFiber(runtime, next);
      return;
    }

    FiberNode* siblingFiber = completedWork->sibling;
    if (siblingFiber != nullptr) {
      setWorkInProgressFiber(runtime, siblingFiber);
      return;
    }

    completedWork = returnFiber;
    setWorkInProgressFiber(runtime, completedWork);
  } while (completedWork != nullptr);

  if (state.exitStatus == RootExitStatus::InProgress) {
    setWorkInProgressRootExitStatus(runtime, RootExitStatus::Completed);
  }
}

void unwindUnitOfWork(ReactRuntime& runtime, FiberNode& unitOfWork, bool skipSiblings) {
  auto& state = getState(runtime);
  FiberNode* incompleteWork = &unitOfWork;

  do {
    FiberNode* current = incompleteWork->alternate;
    FiberNode* next = unwindWork(current, incompleteWork, state.entangledRenderLanes);

    if (next != nullptr) {
      next->flags &= HostEffectMask;
      setWorkInProgressFiber(runtime, next);
      return;
    }

    if (enableProfilerTimer && (incompleteWork->mode & ProfileMode) != NoMode) {
      stopProfilerTimerIfRunningAndRecordIncompleteDuration(*incompleteWork);
      double actualDuration = incompleteWork->actualDuration;
      for (FiberNode* child = incompleteWork->child; child != nullptr; child = child->sibling) {
        actualDuration += child->actualDuration;
      }
      incompleteWork->actualDuration = actualDuration;
    }

    FiberNode* returnFiber = incompleteWork->returnFiber;
    if (returnFiber != nullptr) {
      returnFiber->flags |= Incomplete;
      returnFiber->subtreeFlags = NoFlags;
      returnFiber->deletions.clear();
    }

    if (!skipSiblings) {
      FiberNode* siblingFiber = incompleteWork->sibling;
      if (siblingFiber != nullptr) {
        setWorkInProgressFiber(runtime, siblingFiber);
        return;
      }
    }

    incompleteWork = returnFiber;
    setWorkInProgressFiber(runtime, incompleteWork);
  } while (incompleteWork != nullptr);

  setWorkInProgressRootExitStatus(runtime, RootExitStatus::SuspendedAtTheShell);
  setWorkInProgressFiber(runtime, nullptr);
}

FiberNode* prepareFreshStack(ReactRuntime& runtime, FiberRoot& root, Lanes lanes) {
  if (root.timeoutHandle != noTimeout) {
    cancelTimeout(root.timeoutHandle);
    root.timeoutHandle = noTimeout;
  }

  if (root.cancelPendingCommit) {
    auto cancel = std::move(root.cancelPendingCommit);
    root.cancelPendingCommit = nullptr;
    cancel();
  }

  resetWorkInProgressStack(runtime);

  setWorkInProgressRoot(runtime, &root);
  FiberNode* rootWorkInProgress = createWorkInProgress(root.current, nullptr);
  setWorkInProgressFiber(runtime, rootWorkInProgress);
  setWorkInProgressRootRenderLanes(runtime, lanes);
  setWorkInProgressSuspendedReason(runtime, SuspendedReason::NotSuspended);
  setWorkInProgressThrownValue(runtime, nullptr);
  setWorkInProgressRootDidSkipSuspendedSiblings(runtime, false);
  setWorkInProgressRootIsPrerendering(runtime, checkIfRootIsPrerendering(root, lanes));
  setWorkInProgressRootDidAttachPingListener(runtime, false);
  setWorkInProgressRootExitStatus(runtime, RootExitStatus::InProgress);
  setWorkInProgressRootSkippedLanes(runtime, NoLanes);
  setWorkInProgressRootInterleavedUpdatedLanes(runtime, NoLanes);
  setWorkInProgressRootRenderPhaseUpdatedLanes(runtime, NoLanes);
  setWorkInProgressRootPingedLanes(runtime, NoLanes);
  setWorkInProgressDeferredLane(runtime, NoLane);
  setWorkInProgressSuspendedRetryLanes(runtime, NoLanes);
  clearWorkInProgressRootConcurrentErrors(runtime);
  clearWorkInProgressRootRecoverableErrors(runtime);
  setWorkInProgressRootDidIncludeRecursiveRenderUpdate(runtime, false);
  setWorkInProgressUpdateTask(runtime, nullptr);
  clearWorkInProgressTransitions(runtime);
  setDidIncludeCommitPhaseUpdate(runtime, false);
  setCurrentPendingTransitionCallbacks(runtime, nullptr);
  setCurrentEndTime(runtime, 0.0);
  setWorkInProgressRootRenderTargetTime(runtime, std::numeric_limits<double>::infinity());

  setEntangledRenderLanes(runtime, getEntangledLanes(root, lanes));

  finishQueueingConcurrentUpdates();

  return rootWorkInProgress;
}

void resetWorkInProgressStack(ReactRuntime& runtime) {
  FiberNode* const workInProgress = getWorkInProgressFiber(runtime);
  if (workInProgress == nullptr) {
    return;
  }

  FiberNode* interruptedWork = nullptr;
  if (getWorkInProgressSuspendedReason(runtime) == SuspendedReason::NotSuspended) {
    interruptedWork = workInProgress->returnFiber;
  } else {
    resetSuspendedWorkLoopOnUnwind(workInProgress);
    interruptedWork = workInProgress;
  }

  const Lanes renderLanes = getWorkInProgressRootRenderLanes(runtime);
  while (interruptedWork != nullptr) {
    FiberNode* current = interruptedWork->alternate;
    unwindInterruptedWork(current, interruptedWork, renderLanes);
    interruptedWork = interruptedWork->returnFiber;
  }

  setWorkInProgressFiber(runtime, nullptr);
}

SuspendedReason getWorkInProgressSuspendedReason(ReactRuntime& runtime) {
  return getState(runtime).suspendedReason;
}

void setWorkInProgressSuspendedReason(ReactRuntime& runtime, SuspendedReason reason) {
  getState(runtime).suspendedReason = reason;
}

void* getWorkInProgressThrownValue(ReactRuntime& runtime) {
  return getState(runtime).thrownValue;
}

void setWorkInProgressThrownValue(ReactRuntime& runtime, void* value) {
  getState(runtime).thrownValue = value;
}

bool getWorkInProgressRootDidSkipSuspendedSiblings(ReactRuntime& runtime) {
  return getState(runtime).didSkipSuspendedSiblings;
}

void setWorkInProgressRootDidSkipSuspendedSiblings(ReactRuntime& runtime, bool value) {
  getState(runtime).didSkipSuspendedSiblings = value;
}

bool getWorkInProgressRootIsPrerendering(ReactRuntime& runtime) {
  return getState(runtime).isPrerendering;
}

void setWorkInProgressRootIsPrerendering(ReactRuntime& runtime, bool value) {
  getState(runtime).isPrerendering = value;
}

bool getWorkInProgressRootDidAttachPingListener(ReactRuntime& runtime) {
  return getState(runtime).didAttachPingListener;
}

void setWorkInProgressRootDidAttachPingListener(ReactRuntime& runtime, bool value) {
  getState(runtime).didAttachPingListener = value;
}

RootExitStatus getWorkInProgressRootExitStatus(ReactRuntime& runtime) {
  return getState(runtime).exitStatus;
}

void setWorkInProgressRootExitStatus(ReactRuntime& runtime, RootExitStatus status) {
  getState(runtime).exitStatus = status;
}

Lanes getWorkInProgressRootSkippedLanes(ReactRuntime& runtime) {
  return getState(runtime).skippedLanes;
}

void setWorkInProgressRootSkippedLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).skippedLanes = lanes;
}

Lanes getWorkInProgressRootInterleavedUpdatedLanes(ReactRuntime& runtime) {
  return getState(runtime).interleavedUpdatedLanes;
}

void setWorkInProgressRootInterleavedUpdatedLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).interleavedUpdatedLanes = lanes;
}

Lanes getWorkInProgressRootRenderPhaseUpdatedLanes(ReactRuntime& runtime) {
  return getState(runtime).renderPhaseUpdatedLanes;
}

void setWorkInProgressRootRenderPhaseUpdatedLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).renderPhaseUpdatedLanes = lanes;
}

Lanes getWorkInProgressRootPingedLanes(ReactRuntime& runtime) {
  return getState(runtime).pingedLanes;
}

void setWorkInProgressRootPingedLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).pingedLanes = lanes;
}

Lane getWorkInProgressDeferredLane(ReactRuntime& runtime) {
  return getState(runtime).deferredLane;
}

void setWorkInProgressDeferredLane(ReactRuntime& runtime, Lane lane) {
  getState(runtime).deferredLane = lane;
}

Lanes getWorkInProgressSuspendedRetryLanes(ReactRuntime& runtime) {
  return getState(runtime).suspendedRetryLanes;
}

void setWorkInProgressSuspendedRetryLanes(ReactRuntime& runtime, Lanes lanes) {
  getState(runtime).suspendedRetryLanes = lanes;
}

std::vector<void*>& getWorkInProgressRootConcurrentErrors(ReactRuntime& runtime) {
  return getState(runtime).concurrentErrors;
}

void clearWorkInProgressRootConcurrentErrors(ReactRuntime& runtime) {
  getState(runtime).concurrentErrors.clear();
}

std::vector<void*>& getWorkInProgressRootRecoverableErrors(ReactRuntime& runtime) {
  return getState(runtime).recoverableErrors;
}

void clearWorkInProgressRootRecoverableErrors(ReactRuntime& runtime) {
  getState(runtime).recoverableErrors.clear();
}

bool getWorkInProgressRootDidIncludeRecursiveRenderUpdate(ReactRuntime& runtime) {
  return getState(runtime).didIncludeRecursiveRenderUpdate;
}

void setWorkInProgressRootDidIncludeRecursiveRenderUpdate(ReactRuntime& runtime, bool value) {
  getState(runtime).didIncludeRecursiveRenderUpdate = value;
}

} // namespace react
