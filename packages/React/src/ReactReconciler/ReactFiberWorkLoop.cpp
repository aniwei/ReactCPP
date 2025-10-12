#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "ReactReconciler/ReactFiberCompleteWork.h"
#include "ReactReconciler/ReactFiberCacheComponentState.h"

#include "ReactReconciler/ReactCapturedValue.h"
#include "ReactReconciler/ReactFiberChild.h"
#include "ReactReconciler/ReactFiberCommitEffects.h"
#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberClassUpdateQueue.h"
#include "ReactReconciler/ReactFiberErrorLogger.h"
#include "ReactReconciler/ReactFiberDevToolsHook.h"
#include "ReactReconciler/ReactFiberHiddenContext.h"
#include "ReactReconciler/ReactFiberOffscreenComponent.h"
#include "ReactReconciler/ReactFiberHydrationContext.h"
#include "ReactReconciler/ReactFiberHydrationContext_ext.h"
#include "ReactReconciler/ReactFiberNewContext.h"
#include "ReactReconciler/ReactFiberTreeContext.h"
#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactDOM/client/ReactDOMInstance.h"
#include "ReactReconciler/ReactFiberStack.h"
#include "ReactReconciler/ReactFiberHooks.h"
#include "ReactReconciler/ReactFiberCache.h"
#include "ReactReconciler/ReactFiberHostRootState.h"
#include "ReactReconciler/ReactFiberSuspenseComponent.h"
#include "ReactReconciler/ReactFiberSuspenseContext.h"
#include "ReactReconciler/ReactStrictModeWarnings.h"
#include "ReactReconciler/ReactFiberThrow.h"
#include "ReactReconciler/ReactUpdateQueue.h"
#include "ReactReconciler/ReactFiberSuspenseContext.h"
#include "ReactReconciler/ReactFiberRootScheduler.h"
#include "ReactReconciler/ReactHostConfig.h"
#include "ReactReconciler/ReactTypeOfMode.h"
#include "ReactReconciler/ReactFiberThenable.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactBuildConfig.h"
#include "shared/ReactFeatureFlags.h"
#include "shared/ReactSharedInternals.h"
#include "shared/ReactSymbols.h"

#include "jsi/jsi.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <stdexcept>
#include <exception>

namespace react {

namespace {

FiberNode* bailoutOffscreenComponent(FiberNode* current, FiberNode& workInProgress);
void markUpdate(FiberNode& workInProgress);

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::Array;
using facebook::jsi::Runtime;
using facebook::jsi::Symbol;
using facebook::jsi::String;
using facebook::jsi::Value;

inline WorkLoopState& getState(ReactRuntime& runtime);
bool flushPendingEffectsImpl(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  bool includeRenderPhaseUpdates);

std::string getFiberType(Runtime& jsRuntime, const FiberNode& fiber);
Value cloneJsiValue(Runtime& jsRuntime, const void* storage);

#if REACTCPP_ENABLE_DEV

std::string getComponentNameFromFiber(Runtime& jsRuntime, FiberNode& fiber) {
  Value typeValue = cloneJsiValue(jsRuntime, fiber.type);
  if (!typeValue.isUndefined() && !typeValue.isNull()) {
    return getComponentNameFromType(jsRuntime, typeValue);
  }

  const std::string intrinsicType = getFiberType(jsRuntime, fiber);
  if (!intrinsicType.empty()) {
    return intrinsicType;
  }

  return "Unknown";
}

void warnAboutRenderPhaseUpdatesInDEV(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& fiber) {
  const ExecutionContext context = getExecutionContext(runtime);
  if ((context & RenderContext) == NoContext) {
    return;
  }

  FiberNode* const renderingFiber = getWorkInProgressFiber(runtime);
  if (renderingFiber == nullptr) {
    return;
  }

  const WorkTag fiberTag = fiber.tag;
  if (fiberTag != WorkTag::FunctionComponent &&
      fiberTag != WorkTag::ForwardRef &&
      fiberTag != WorkTag::SimpleMemoComponent &&
      fiberTag != WorkTag::ClassComponent) {
    return;
  }

  static std::unordered_set<std::string> warnedComponents;
  static bool didWarnClassComponent = false;

  const std::string renderingComponentName =
      getComponentNameFromFiber(jsRuntime, *renderingFiber);
  const std::string updatedComponentName =
      getComponentNameFromFiber(jsRuntime, fiber);

  if (fiberTag == WorkTag::ClassComponent) {
    if (didWarnClassComponent) {
      return;
    }
    didWarnClassComponent = true;

    std::ostringstream message;
    message
        << "Cannot update during an existing state transition (such as within `render`). "
        << "Render methods should be a pure function of props and state.";
    logDevError(message.str());
    return;
  }

  if (!warnedComponents.emplace(renderingComponentName).second) {
    return;
  }

  std::ostringstream message;
  message
      << "Cannot update a component (`" << updatedComponentName
      << "`) while rendering a different component (`" << renderingComponentName
      << "`). To locate the bad setState() call inside `" << renderingComponentName
      << "`, follow the stack trace as described in https://react.dev/link/setstate-in-render";
  logDevError(message.str());
}

void warnIfUpdatesNotWrappedWithActDEV(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& fiber) {
  const ExecutionContext context = getExecutionContext(runtime);
  if ((context & (RenderContext | CommitContext)) != NoContext) {
    return;
  }

  if (!disableLegacyMode && (fiber.mode & ConcurrentMode) == NoMode) {
    // Legacy mode keeps compatibility with historical behavior; skip warning.
    return;
  }

  bool insideActScope = false;
  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    facebook::jsi::Value actQueueValue = getReactSharedInternalsProperty(
        jsRuntime,
        internals,
        ReactSharedInternalsKeys::kActQueue);
    insideActScope =
        actQueueValue.isObject() && !actQueueValue.isNull() && !actQueueValue.isUndefined();
  } catch (const std::exception&) {
    // If ReactSharedInternals are unavailable, fall back to warning.
  }

  if (insideActScope) {
    return;
  }

  static std::unordered_set<std::string> warnedComponents;
  const std::string componentName = getComponentNameFromFiber(jsRuntime, fiber);
  if (!warnedComponents.emplace(componentName).second) {
    return;
  }

  std::ostringstream message;
  message
      << "An update to " << componentName
      << " inside a test was not wrapped in act(...).\n\nWhen testing, code that causes React state updates should be wrapped into act(...):\n\nact(() => {\n  /* fire events that update state */\n});\n/* assert on the output */\n\nThis ensures that you're testing the behavior the user would see in the browser. "
      << "Learn more at https://react.dev/link/wrap-tests-with-act";
  logDevError(message.str());
}

bool isLegacyBatchingActive(facebook::jsi::Runtime& jsRuntime) {
  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    if (hasReactSharedInternalsProperty(
            jsRuntime,
            internals,
            ReactSharedInternalsKeys::kIsBatchingLegacy)) {
      const facebook::jsi::Value batching = getReactSharedInternalsProperty(
          jsRuntime,
          internals,
          ReactSharedInternalsKeys::kIsBatchingLegacy);
      if (batching.isBool()) {
        return batching.getBool();
      }
    }
  } catch (const std::exception& ex) {
#ifndef NDEBUG
    std::cerr << "React scheduleUpdateOnFiber failed to read isBatchingLegacy: "
              << ex.what() << std::endl;
#endif
  }
  return false;
}
#else
inline void warnAboutRenderPhaseUpdatesInDEV(
  ReactRuntime&,
  facebook::jsi::Runtime&,
  FiberNode&) {}
inline void warnIfUpdatesNotWrappedWithActDEV(
  ReactRuntime&,
  facebook::jsi::Runtime&,
  FiberNode&) {}
inline bool isLegacyBatchingActive(facebook::jsi::Runtime&) {
  return false;
}
#endif // REACTCPP_ENABLE_DEV

const Transition* getCurrentTransition(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime) {
  if (!enableTransitionTracing) {
    return nullptr;
  }

  static std::vector<std::unique_ptr<Transition>> transitionRegistry;

  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    facebook::jsi::Value transitionValue = getReactSharedInternalsProperty(
        jsRuntime,
        internals,
        ReactSharedInternalsKeys::kTransitionConfig);

    if (!transitionValue.isObject()) {
      return nullptr;
    }

    facebook::jsi::Object transitionObject = transitionValue.getObject(jsRuntime);

    std::string transitionName;
    double transitionStartTime = -1.0;

    if (transitionObject.hasProperty(jsRuntime, "name")) {
      facebook::jsi::Value nameValue = transitionObject.getProperty(jsRuntime, "name");
      if (nameValue.isString()) {
        transitionName = nameValue.getString(jsRuntime).utf8(jsRuntime);
      }
    }

    if (transitionObject.hasProperty(jsRuntime, "startTime")) {
      facebook::jsi::Value startValue = transitionObject.getProperty(jsRuntime, "startTime");
      if (startValue.isNumber()) {
        transitionStartTime = startValue.getNumber();
      }
    }

    if (transitionStartTime < 0.0) {
      transitionStartTime = runtime.now();
    }

    for (const auto& entry : transitionRegistry) {
      if (entry->value != nullptr &&
          entry->value->strictEquals(jsRuntime, transitionValue)) {
        entry->name = transitionName;
        entry->startTime = transitionStartTime;
        return entry.get();
      }
    }

    auto slot = std::make_unique<Transition>();
    slot->value = std::make_shared<facebook::jsi::Value>(jsRuntime, transitionValue);
    slot->name = transitionName;
    slot->startTime = transitionStartTime;

    transitionRegistry.push_back(std::move(slot));
    return transitionRegistry.back().get();
  } catch (const std::exception& ex) {
#ifndef NDEBUG
    std::cerr << "React getCurrentTransition failed: " << ex.what() << std::endl;
#else
    (void)ex;
#endif
  }

  return nullptr;
}

class ComponentRenderProfilerScope {
public:
  ComponentRenderProfilerScope(FiberNode& fiber, bool shouldProfile)
      : shouldProfile_(shouldProfile) {
    if (shouldProfile_) {
      markComponentRenderStarted(fiber);
    }
  }

  ~ComponentRenderProfilerScope() {
    if (shouldProfile_) {
      markComponentRenderStopped();
    }
  }

  ComponentRenderProfilerScope(const ComponentRenderProfilerScope&) = delete;
  ComponentRenderProfilerScope& operator=(const ComponentRenderProfilerScope&) = delete;

private:
  bool shouldProfile_{false};
};

#if REACTCPP_ENABLE_DEV
std::unordered_set<std::string> gDidWarnAboutBadClass{};
std::unordered_set<std::string> gDidWarnAboutContextTypes{};
std::unordered_set<std::string> gDidWarnAboutGetDerivedStateOnFunctionComponent{};
std::unordered_set<std::string> gDidWarnAboutContextTypeOnFunctionComponent{};

void logDevError(const std::string& message) {
  std::cerr << "[React] " << message << std::endl;
}

bool valueIsDefinedAndNotNull(const Value& value) {
  return !value.isUndefined() && !value.isNull();
}

std::string getComponentNameFromType(Runtime& jsRuntime, const Value& componentValue) {
  if (!componentValue.isObject()) {
    return "Unknown";
  }

  Object componentObject = componentValue.getObject(jsRuntime);

  if (componentObject.hasProperty(jsRuntime, "displayName")) {
    Value displayNameValue = componentObject.getProperty(jsRuntime, "displayName");
    if (displayNameValue.isString()) {
      std::string displayName = displayNameValue.getString(jsRuntime).utf8(jsRuntime);
      if (!displayName.empty()) {
        return displayName;
      }
    }
  }

  if (componentObject.isFunction(jsRuntime)) {
    Function componentFunction = componentObject.asFunction(jsRuntime);
    std::string functionName = componentFunction.getName(jsRuntime).utf8(jsRuntime);
    if (!functionName.empty()) {
      return functionName;
    }
  }

  if (componentObject.hasProperty(jsRuntime, "name")) {
    Value nameValue = componentObject.getProperty(jsRuntime, "name");
    if (nameValue.isString()) {
      std::string nameString = nameValue.getString(jsRuntime).utf8(jsRuntime);
      if (!nameString.empty()) {
        return nameString;
      }
    }
  }

  return "Unknown";
}

bool hasFunctionProperty(Runtime& jsRuntime, const Object& object, const char* propertyName) {
  if (!object.hasProperty(jsRuntime, propertyName)) {
    return false;
  }
  Value propertyValue = object.getProperty(jsRuntime, propertyName);
  if (!propertyValue.isObject()) {
    return false;
  }
  Object propertyObject = propertyValue.getObject(jsRuntime);
  return propertyObject.isFunction(jsRuntime);
}

void warnOnBadClass(Runtime& jsRuntime, const Value& componentValue, const std::string& componentName) {
  if (!componentValue.isObject()) {
    return;
  }

  Object componentObject = componentValue.getObject(jsRuntime);
  if (!componentObject.hasProperty(jsRuntime, "prototype")) {
    return;
  }

  Value prototypeValue = componentObject.getProperty(jsRuntime, "prototype");
  if (!prototypeValue.isObject()) {
    return;
  }

  Object prototypeObject = prototypeValue.getObject(jsRuntime);
  if (!hasFunctionProperty(jsRuntime, prototypeObject, "render")) {
    return;
  }

  if (!gDidWarnAboutBadClass.emplace(componentName).second) {
    return;
  }

  std::ostringstream message;
  message
    << "The <" << componentName
    << " /> component appears to have a render method, but doesn't extend React.Component. "
    << "This is likely to cause errors. Change " << componentName
    << " to extend React.Component instead.";
  logDevError(message.str());
}

void warnOnLegacyContextTypes(Runtime& jsRuntime, const Value& componentValue, const std::string& componentName) {
  if (!componentValue.isObject()) {
    return;
  }

  Object componentObject = componentValue.getObject(jsRuntime);
  if (!componentObject.hasProperty(jsRuntime, "contextTypes")) {
    return;
  }

  Value contextTypesValue = componentObject.getProperty(jsRuntime, "contextTypes");
  if (!valueIsDefinedAndNotNull(contextTypesValue)) {
    return;
  }

  if (!gDidWarnAboutContextTypes.emplace(componentName).second) {
    return;
  }

  std::ostringstream message;
  if (disableLegacyContext) {
    message
      << componentName
      << " uses the legacy contextTypes API which was removed in React 19. "
      << "Use React.createContext() with React.useContext() instead. (https://react.dev/link/legacy-context)";
  } else {
    message
      << componentName
      << " uses the legacy contextTypes API which will be removed soon. "
      << "Use React.createContext() with React.useContext() instead. (https://react.dev/link/legacy-context)";
  }
  logDevError(message.str());
}

void validateFunctionComponentInDev(Runtime& jsRuntime, const Value& componentValue, const std::string& componentName) {
  if (!componentValue.isObject()) {
    return;
  }

  Object componentObject = componentValue.getObject(jsRuntime);

  if (componentObject.hasProperty(jsRuntime, "childContextTypes")) {
    Value childContextTypes = componentObject.getProperty(jsRuntime, "childContextTypes");
    if (valueIsDefinedAndNotNull(childContextTypes)) {
      const std::string label = componentName == "Unknown" ? "Component" : componentName;
      std::ostringstream message;
      message
        << "childContextTypes cannot be defined on a function component.\n  "
        << label << ".childContextTypes = ...";
      logDevError(message.str());
    }
  }

  if (hasFunctionProperty(jsRuntime, componentObject, "getDerivedStateFromProps")) {
    if (gDidWarnAboutGetDerivedStateOnFunctionComponent.emplace(componentName).second) {
      std::ostringstream message;
      message << componentName << ": Function components do not support getDerivedStateFromProps.";
      logDevError(message.str());
    }
  }

  if (componentObject.hasProperty(jsRuntime, "contextType")) {
    Value contextTypeValue = componentObject.getProperty(jsRuntime, "contextType");
    if (valueIsDefinedAndNotNull(contextTypeValue) && contextTypeValue.isObject()) {
      if (gDidWarnAboutContextTypeOnFunctionComponent.emplace(componentName).second) {
        std::ostringstream message;
        message << componentName << ": Function components do not support contextType.";
        logDevError(message.str());
      }
    }
  }
}
#endif

#if !REACTCPP_ENABLE_DEV
inline std::string getComponentNameFromType(Runtime&, const Value&) {
  return "Unknown";
}

inline void warnOnBadClass(Runtime&, const Value&, const std::string&) {}

inline void validateFunctionComponentInDev(Runtime&, const Value&, const std::string&) {}

inline void warnOnLegacyContextTypes(Runtime&, const Value&, const std::string&) {}
#endif

struct ProfilerStateNode {
  double effectDuration{-0.0};
  double passiveEffectDuration{-0.0};
};

constexpr const char* kChildrenPropName = "children";
constexpr const char* kContextPropName = "_context";
constexpr const char* kValuePropName = "value";
constexpr const char* kNamePropName = "name";

const SuspenseState kSuspendedMarker{nullptr, nullptr, NoLane, {}};

enum class TracingMarkerTag : std::uint8_t {
  TransitionRoot = 0,
  TransitionTracingMarker = 1,
};

struct TransitionAbort {
  enum class Reason : std::uint8_t {
    Error,
    Unknown,
    Marker,
    Suspense,
  };

  Reason reason{Reason::Unknown};
  std::optional<std::string> name{};
};

struct SuspenseInfo {
  std::optional<std::string> name{};
};

using PendingBoundaries = std::unordered_map<OffscreenInstance*, SuspenseInfo>;

struct TracingMarkerInstance {
  TracingMarkerTag tag{TracingMarkerTag::TransitionTracingMarker};
  std::unordered_set<const Transition*> transitions{};
  std::unique_ptr<PendingBoundaries> pendingBoundaries{};
  std::vector<TransitionAbort> aborts{};
  std::optional<std::string> name{};
};

StackCursor<std::optional<std::vector<TracingMarkerInstance*>>> markerInstanceStack =
  createCursor<std::optional<std::vector<TracingMarkerInstance*>>>(std::nullopt);

StackCursor<void*> resumedCacheCursor = createCursor<void*>(nullptr);
StackCursor<std::optional<std::vector<const Transition*>>> transitionStackCursor =
  createCursor<std::optional<std::vector<const Transition*>>>(std::nullopt);
StackCursor<void*> cacheProviderCursor = createCursor<void*>(nullptr);

Value* cloneForFiber(Runtime& jsRuntime, const Value& source) {
  return new Value(jsRuntime, source);
}

OffscreenProps* createOffscreenProps(Runtime& jsRuntime, OffscreenMode mode, const Value& children) {
  auto* props = new OffscreenProps();
  props->mode = mode;
  props->children = cloneForFiber(jsRuntime, children);
  return props;
}

OffscreenMode resolveActivityMode(Runtime& jsRuntime, const Value& modeValue) {
  if (!modeValue.isString()) {
    return OffscreenMode::Visible;
  }

  const std::string modeString = modeValue.getString(jsRuntime).utf8(jsRuntime);
  if (modeString == "hidden") {
    return OffscreenMode::Hidden;
  }

  return OffscreenMode::Visible;
}

FiberNode* mountActivityChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& workInProgress,
  OffscreenMode mode,
  const Value& children,
  Lanes renderLanes) {
  auto* offscreenProps = createOffscreenProps(jsRuntime, mode, children);
  FiberNode* primaryChild = createFiber(WorkTag::OffscreenComponent, offscreenProps, std::string{}, workInProgress.mode);
  primaryChild->pendingProps = offscreenProps;
  primaryChild->memoizedProps = offscreenProps;
  primaryChild->returnFiber = &workInProgress;
  primaryChild->lanes = renderLanes;
  primaryChild->ref = workInProgress.ref;
  primaryChild->sibling = nullptr;

  if (offscreenProps->children != nullptr) {
    primaryChild->child = mountChildFibers(&runtime, jsRuntime, *primaryChild, *offscreenProps->children, renderLanes);
  } else {
    Value undefinedChildren = Value::undefined();
    primaryChild->child = mountChildFibers(&runtime, jsRuntime, *primaryChild, undefinedChildren, renderLanes);
  }

  workInProgress.child = primaryChild;
  return primaryChild;
}

FiberNode* updateActivityChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& workInProgress,
  FiberNode* current,
  OffscreenMode mode,
  const Value& children,
  Lanes renderLanes) {
  FiberNode* currentChild = current != nullptr ? current->child : nullptr;
  FiberNode* workChild = nullptr;

  if (currentChild != nullptr && currentChild->tag == WorkTag::OffscreenComponent) {
    workChild = createWorkInProgress(currentChild, currentChild->pendingProps);
  } else if (currentChild != nullptr) {
    workChild = createWorkInProgress(currentChild, currentChild->pendingProps);
  } else {
    workChild = createFiber(WorkTag::OffscreenComponent, nullptr, std::string{}, workInProgress.mode);
  }

  auto* offscreenProps = createOffscreenProps(jsRuntime, mode, children);
  workChild->pendingProps = offscreenProps;
  workChild->memoizedProps = offscreenProps;
  workChild->returnFiber = &workInProgress;
  workChild->lanes = currentChild != nullptr ? currentChild->lanes : renderLanes;
  workChild->ref = workInProgress.ref;
  workChild->sibling = nullptr;

  FiberNode* currentFirstChild = currentChild != nullptr ? currentChild->child : nullptr;
  Value nextChildrenValue = offscreenProps->children != nullptr
    ? Value(jsRuntime, *offscreenProps->children)
    : Value::undefined();
  workChild->child = reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, *workChild, nextChildrenValue, renderLanes);

  workInProgress.child = workChild;
  return workChild;
}

Value* createFragmentChildren(Runtime& jsRuntime, const Value& children) {
  return cloneForFiber(jsRuntime, children);
}

OffscreenState* mountSuspenseOffscreenState(Lanes baseLanes) {
  auto* state = new OffscreenState();
  state->baseLanes = baseLanes;
  state->cachePool.reset();
  return state;
}

OffscreenState* updateSuspenseOffscreenState(const OffscreenState* prevState, Lanes renderLanes) {
  auto* state = new OffscreenState();
  if (prevState != nullptr) {
    state->baseLanes = mergeLanes(prevState->baseLanes, renderLanes);
    state->cachePool = prevState->cachePool;
  } else {
    state->baseLanes = renderLanes;
    state->cachePool.reset();
  }
  return state;
}

void markChildForDeletion(FiberNode& workInProgress, FiberNode& childToDelete) {
  workInProgress.deletions.push_back(&childToDelete);
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ChildDeletion);
}

FiberNode* mountSuspensePrimaryChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& workInProgress,
  const Value& primaryChildren,
  Lanes renderLanes) {
  auto* offscreenProps = createOffscreenProps(jsRuntime, OffscreenMode::Visible, primaryChildren);
  FiberNode* primaryChildFragment = createFiber(WorkTag::OffscreenComponent, offscreenProps, std::string{}, workInProgress.mode);
  primaryChildFragment->pendingProps = offscreenProps;
  primaryChildFragment->memoizedProps = offscreenProps;
  primaryChildFragment->lanes = renderLanes;
  primaryChildFragment->returnFiber = &workInProgress;
  primaryChildFragment->sibling = nullptr;
  primaryChildFragment->memoizedState = nullptr;
  primaryChildFragment->childLanes = NoLanes;

  primaryChildFragment->child = mountChildFibers(
      &runtime, jsRuntime, *primaryChildFragment, *offscreenProps->children, renderLanes);
  workInProgress.child = primaryChildFragment;
  return primaryChildFragment;
}

FiberNode* mountSuspenseFallbackChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& workInProgress,
  const Value& primaryChildren,
  const Value& fallbackChildren,
  Lanes renderLanes) {
  auto* offscreenProps = createOffscreenProps(jsRuntime, OffscreenMode::Hidden, primaryChildren);
  FiberNode* primaryChildFragment = createFiber(WorkTag::OffscreenComponent, offscreenProps, std::string{}, workInProgress.mode);
  primaryChildFragment->pendingProps = offscreenProps;
  primaryChildFragment->memoizedProps = offscreenProps;
  primaryChildFragment->lanes = NoLanes;
  primaryChildFragment->returnFiber = &workInProgress;

  primaryChildFragment->child = mountChildFibers(
    &runtime, 
    jsRuntime, 
    *primaryChildFragment, 
    *offscreenProps->children, 
    renderLanes);

  auto* fragmentChildren = createFragmentChildren(jsRuntime, fallbackChildren);
  FiberNode* fallbackChildFragment = createFiber(WorkTag::Fragment, fragmentChildren, std::string{}, workInProgress.mode);
  fallbackChildFragment->pendingProps = fragmentChildren;
  fallbackChildFragment->memoizedProps = fragmentChildren;
  fallbackChildFragment->lanes = renderLanes;
  fallbackChildFragment->returnFiber = &workInProgress;
  fallbackChildFragment->memoizedState = nullptr;
  fallbackChildFragment->sibling = nullptr;

  fallbackChildFragment->child = mountChildFibers(
    &runtime, 
    jsRuntime, 
    *fallbackChildFragment, 
    *fragmentChildren, 
    renderLanes);

  primaryChildFragment->sibling = fallbackChildFragment;
  fallbackChildFragment->sibling = nullptr;
  workInProgress.child = primaryChildFragment;

  return fallbackChildFragment;
}

bool tryHandleSuspenseHydrationOnMount(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& workInProgress,
  const Value& primaryChildren,
  const Value& fallbackChildren,
  bool showFallback,
  Lanes renderLanes,
  Lanes primaryTreeLanes,
  FiberNode*& outNextChild) {
  (void)jsRuntime;
  (void)primaryChildren;
  (void)fallbackChildren;
  (void)renderLanes;

  if (!getIsHydrating(runtime)) {
    return false;
  }

  if (showFallback) {
    pushPrimaryTreeSuspenseHandler(workInProgress);
  } else {
    pushFallbackTreeSuspenseHandler(workInProgress);
  }

  void* dehydrated = tryToClaimNextHydratableSuspenseInstance(runtime, workInProgress);
  if (dehydrated == nullptr) {
    queueHydrationError(runtime, workInProgress, "Hydration: Suspense boundary instance not found");
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
    resetHydrationState(runtime);
    return false;
  }

  auto* suspenseState = new SuspenseState();
  suspenseState->dehydrated = dehydrated;
  suspenseState->treeContext = getSuspenseHandler();
  suspenseState->retryLane = NoLane;
  workInProgress.memoizedState = suspenseState;
  workInProgress.child = nullptr;
  workInProgress.childLanes = primaryTreeLanes;
  workInProgress.lanes = laneToLanes(OffscreenLane);
  outNextChild = nullptr;
  return true;
}

bool handleDehydratedSuspenseUpdateFallback(
  ReactRuntime& runtime,
  FiberNode& current,
  FiberNode& workInProgress,
  SuspenseState& previousState) {
  (void)current;

  queueHydrationError(runtime, workInProgress, "Hydration: Falling back to client render for Suspense boundary");
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
  resetHydrationState(runtime);
  previousState.dehydrated = nullptr;
  return false;
}

FiberNode* updateSuspensePrimaryChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& current,
  FiberNode& workInProgress,
  const Value& primaryChildren,
  Lanes renderLanes) {
  FiberNode* currentPrimaryChildFragment = current.child;
  if (currentPrimaryChildFragment == nullptr) {
    return mountSuspensePrimaryChildren(runtime, jsRuntime, workInProgress, primaryChildren, renderLanes);
  }

  FiberNode* primaryChildFragment = createWorkInProgress(currentPrimaryChildFragment, currentPrimaryChildFragment->pendingProps);
  workInProgress.child = primaryChildFragment;
  primaryChildFragment->returnFiber = &workInProgress;
  primaryChildFragment->sibling = nullptr;
  primaryChildFragment->lanes = renderLanes;

  auto* newProps = createOffscreenProps(jsRuntime, OffscreenMode::Visible, primaryChildren);
  primaryChildFragment->pendingProps = newProps;
  primaryChildFragment->memoizedProps = newProps;

  FiberNode* currentFallbackChildFragment = currentPrimaryChildFragment->sibling;
  if (currentFallbackChildFragment != nullptr) {
    markChildForDeletion(workInProgress, *currentFallbackChildFragment);
  }

  primaryChildFragment->child = reconcileChildFibers(
    &runtime,
    jsRuntime,
    currentPrimaryChildFragment->child,
    *primaryChildFragment,
    *newProps->children,
    renderLanes);
  return primaryChildFragment;
}

FiberNode* updateSuspenseFallbackChildren(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode& current,
  FiberNode& workInProgress,
  const Value& primaryChildren,
  const Value& fallbackChildren,
  Lanes renderLanes) {
  FiberNode* currentPrimaryChildFragment = current.child;
  FiberNode* currentFallbackChildFragment = currentPrimaryChildFragment != nullptr ? currentPrimaryChildFragment->sibling : nullptr;

  FiberNode* primaryChildFragment = nullptr;
  if (currentPrimaryChildFragment != nullptr) {
    primaryChildFragment = createWorkInProgress(currentPrimaryChildFragment, currentPrimaryChildFragment->pendingProps);
  } else {
    primaryChildFragment = createFiber(WorkTag::OffscreenComponent, nullptr, std::string{}, workInProgress.mode);
  }

  auto* hiddenProps = createOffscreenProps(jsRuntime, OffscreenMode::Hidden, primaryChildren);
  primaryChildFragment->pendingProps = hiddenProps;
  primaryChildFragment->memoizedProps = hiddenProps;
  primaryChildFragment->returnFiber = &workInProgress;
  primaryChildFragment->lanes = NoLanes;
  primaryChildFragment->memoizedState = nullptr;

  FiberNode* currentPrimaryChild = currentPrimaryChildFragment != nullptr ? currentPrimaryChildFragment->child : nullptr;
  primaryChildFragment->child = reconcileChildFibers(
    &runtime, 
    jsRuntime, 
    currentPrimaryChild, 
    *primaryChildFragment, 
    *hiddenProps->children, 
    renderLanes);

  FiberNode* fallbackChildFragment = nullptr;
  if (currentFallbackChildFragment != nullptr) {
    fallbackChildFragment = createWorkInProgress(currentFallbackChildFragment, currentFallbackChildFragment->pendingProps);
  } else {
    fallbackChildFragment = createFiber(WorkTag::Fragment, nullptr, std::string{}, workInProgress.mode);
    fallbackChildFragment->flags = static_cast<FiberFlags>(fallbackChildFragment->flags | Placement);
  }

  auto* fragmentChildren = createFragmentChildren(jsRuntime, fallbackChildren);
  fallbackChildFragment->pendingProps = fragmentChildren;
  fallbackChildFragment->memoizedProps = fragmentChildren;
  fallbackChildFragment->lanes = renderLanes;
  fallbackChildFragment->returnFiber = &workInProgress;

  FiberNode* currentFallbackChild = currentFallbackChildFragment != nullptr ? currentFallbackChildFragment->child : nullptr;
  fallbackChildFragment->child = reconcileChildFibers(
    &runtime, jsRuntime, currentFallbackChild, *fallbackChildFragment, *fragmentChildren, renderLanes);

  primaryChildFragment->sibling = fallbackChildFragment;
  fallbackChildFragment->sibling = nullptr;
  fallbackChildFragment->memoizedState = nullptr;
  workInProgress.child = primaryChildFragment;
  return fallbackChildFragment;
}

bool shouldRemainOnFallback(FiberNode* current) {
  if (current != nullptr) {
    const auto* suspenseState = static_cast<const SuspenseState*>(current->memoizedState);
    if (suspenseState == nullptr) {
      return false;
    }
  }

  const SuspenseContext suspenseContext = getCurrentSuspenseContext();
  return hasSuspenseListContext(suspenseContext, ForceSuspenseFallback);
}

Lanes getRemainingWorkInPrimaryTree(
  ReactRuntime& runtime,
  FiberNode* current,
  bool primaryTreeDidDefer,
  Lanes renderLanes) {
  Lanes remaining = current != nullptr ? removeLanes(current->childLanes, renderLanes) : NoLanes;
  if (primaryTreeDidDefer) {
    const Lane deferredLane = getWorkInProgressDeferredLane(runtime);
    if (deferredLane != NoLane) {
      remaining = mergeLanes(remaining, laneToLanes(deferredLane));
    }
  }
  return remaining;
}

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

Value propsMapToValue(
    Runtime& jsRuntime,
    const std::unordered_map<std::string, Value>& propsMap) {
  Object object(jsRuntime);
  for (const auto& entry : propsMap) {
    const auto& name = entry.first;
    const auto& storedValue = entry.second;
    object.setProperty(jsRuntime, name.c_str(), Value(jsRuntime, storedValue));
  }
  return Value(jsRuntime, object);
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

using CachePoolPtr = std::shared_ptr<SpawnedCachePool>;

struct CacheData {
  std::atomic<std::uint32_t> refCount{0};
};

CacheComponentUpdateQueue* getCacheComponentUpdateQueue(const FiberNode& fiber) {
  return static_cast<CacheComponentUpdateQueue*>(fiber.updateQueue);
}

CacheComponentUpdateQueue* initializeCacheComponentUpdateQueue(
    FiberNode& fiber,
    const CacheComponentState& baseState) {
  auto* queue = new CacheComponentUpdateQueue();
  queue->baseState = baseState;
  fiber.updateQueue = queue;
  return queue;
}

CacheComponentUpdateQueue* cloneCacheComponentUpdateQueue(
    const CacheComponentUpdateQueue& source) {
  auto* queue = new CacheComponentUpdateQueue();
  queue->baseState = source.baseState;
  return queue;
}

CacheComponentUpdateQueue* ensureClonedCacheComponentUpdateQueue(
    FiberNode& current,
    FiberNode& workInProgress) {
  auto* currentQueue = getCacheComponentUpdateQueue(current);
  if (currentQueue == nullptr) {
    return nullptr;
  }

  auto* workQueue = getCacheComponentUpdateQueue(workInProgress);
  if (workQueue == nullptr || workQueue == currentQueue) {
    auto* cloned = cloneCacheComponentUpdateQueue(*currentQueue);
    workInProgress.updateQueue = cloned;
    return cloned;
  }

  return workQueue;
}

void updateCacheComponentBaseState(
    FiberNode& fiber,
    const CacheComponentState& state) {
  if (auto* queue = getCacheComponentUpdateQueue(fiber)) {
    queue->baseState = state;
  }
}

void* createCacheInstance() {
  return new CacheData();
}

void retainCache(void* cache) {
  if (cache == nullptr) {
    return;
  }

  auto* data = static_cast<CacheData*>(cache);
  data->refCount.fetch_add(1, std::memory_order_relaxed);
}

void releaseCache(void* cache) {
  if (cache == nullptr) {
    return;
  }

  auto* data = static_cast<CacheData*>(cache);
  std::uint32_t expected = data->refCount.load(std::memory_order_relaxed);
  while (expected != 0) {
    if (data->refCount.compare_exchange_weak(
            expected,
            expected - 1,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      if (expected == 1) {
        delete data;
      }
      return;
    }
  }
}

void* peekCacheFromPool(ReactRuntime& runtime) {
  if (resumedCacheCursor.current != nullptr) {
    return resumedCacheCursor.current;
  }

  FiberRoot* const root = getWorkInProgressRoot(runtime);
  if (root == nullptr) {
    return nullptr;
  }

  if (root->pooledCache == nullptr) {
    root->pooledCache = createCacheInstance();
  }

  return root->pooledCache;
}

CacheComponentState* cloneCacheComponentState(const CacheComponentState* source) {
  if (source == nullptr) {
    return new CacheComponentState();
  }

  auto* cloned = new CacheComponentState();
  cloned->parent = source->parent;
  cloned->cache = source->cache;
  return cloned;
}

CacheComponentState* ensureCacheComponentState(FiberNode& workInProgress, const CacheComponentState* source) {
  auto* state = static_cast<CacheComponentState*>(workInProgress.memoizedState);
  if (state != nullptr) {
    return state;
  }

  state = cloneCacheComponentState(source);
  workInProgress.memoizedState = state;
  return state;
}

void* requestCacheFromPool(ReactRuntime& runtime, Lanes renderLanes) {
  void* cacheFromPool = peekCacheFromPool(runtime);
  if (cacheFromPool != nullptr) {
    return cacheFromPool;
  }

  void* freshCache = createCacheInstance();
  FiberRoot* const root = getWorkInProgressRoot(runtime);
  if (root != nullptr) {
    root->pooledCache = freshCache;
    root->pooledCacheLanes = mergeLanes(root->pooledCacheLanes, renderLanes);
  }
  return freshCache;
}

CachePoolPtr makeCachePool(void* parent, void* cache) {
  if (cache == nullptr) {
    return CachePoolPtr{};
  }

  auto pool = std::make_shared<SpawnedCachePool>();
  pool->parent = parent;
  pool->pool = cache;
  return pool;
}

CachePoolPtr getOffscreenDeferredCache(ReactRuntime& runtime) {
  void* const cacheFromPool = peekCacheFromPool(runtime);
  if (cacheFromPool == nullptr) {
    return CachePoolPtr{};
  }

  return makeCachePool(cacheProviderCursor.current, cacheFromPool);
}

CachePoolPtr acquireDeferredCache(ReactRuntime& runtime) {
  return getOffscreenDeferredCache(runtime);
}

ProfilerStateNode* ensureProfilerStateNode(FiberNode& fiber) {
  auto* state = static_cast<ProfilerStateNode*>(fiber.stateNode);
  if (state == nullptr) {
    state = new ProfilerStateNode();
    fiber.stateNode = state;
  }
  return state;
}

void pushTransition(
    ReactRuntime& runtime,
    FiberNode& fiber,
    const CachePoolPtr& cachePool,
    const std::vector<const Transition*>* transitions) {
  (void)runtime;

  void* const nextCache = cachePool != nullptr ? cachePool->pool : resumedCacheCursor.current;
  push(resumedCacheCursor, nextCache, &fiber);

  if (!enableTransitionTracing) {
    return;
  }

  std::optional<std::vector<const Transition*>> nextTransitions = std::nullopt;

  if (!transitionStackCursor.current.has_value()) {
    if (transitions != nullptr) {
      nextTransitions = *transitions;
    }
  } else if (transitions == nullptr) {
    nextTransitions = transitionStackCursor.current;
  } else {
    std::vector<const Transition*> merged = transitionStackCursor.current.value();
    merged.insert(merged.end(), transitions->begin(), transitions->end());
    nextTransitions = std::move(merged);
  }

  push(transitionStackCursor, std::move(nextTransitions), &fiber);
}

void popTransition(FiberNode& workInProgress, FiberNode* current) {
  if (current == nullptr) {
    return;
  }

  if (enableTransitionTracing) {
    pop(transitionStackCursor, &workInProgress);
  }

  pop(resumedCacheCursor, &workInProgress);
}

bool isHiddenMode(OffscreenMode mode) {
  return mode == OffscreenMode::Hidden || (enableLegacyHidden && mode == OffscreenMode::UnstableDeferWithoutHiding);
}

OffscreenInstance* ensureOffscreenInstance(FiberNode& fiber) {
  auto* instance = static_cast<OffscreenInstance*>(fiber.stateNode);
  if (instance == nullptr) {
    instance = new OffscreenInstance();
    instance->_visibility = OffscreenVisible;
    instance->_pendingMarkers = nullptr;
    instance->_retryCache = nullptr;
    instance->_transitions = nullptr;
    fiber.stateNode = instance;
  }
  return instance;
}

bool isCallable(Runtime& jsRuntime, const Value& value) {
  if (!value.isObject()) {
    return false;
  }
  return value.getObject(jsRuntime).isFunction(jsRuntime);
}

Value createEmptyContextObject(Runtime& jsRuntime) {
  Object empty(jsRuntime);
  return Value(jsRuntime, empty);
}

Value maskLegacyContextForComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    const Value& componentValue) {
  auto& state = getState(runtime);
  Value unmaskedContextValue = cloneJsiValue(jsRuntime, state.legacyContextCursor.current.context);
  Object unmaskedContextObject = ensureObject(jsRuntime, unmaskedContextValue);

  Object componentObject = ensureObject(jsRuntime, componentValue);
  if (!componentObject.hasProperty(jsRuntime, "contextTypes")) {
    return createEmptyContextObject(jsRuntime);
  }

  Value contextTypesValue = componentObject.getProperty(jsRuntime, "contextTypes");
  if (!contextTypesValue.isObject()) {
    return createEmptyContextObject(jsRuntime);
  }

  Object contextTypesObject = contextTypesValue.getObject(jsRuntime);
  Array keys = contextTypesObject.getPropertyNames(jsRuntime);
  const size_t keyCount = keys.size(jsRuntime);

  Object maskedContextObject(jsRuntime);
  for (size_t index = 0; index < keyCount; ++index) {
    Value keyValue = keys.getValueAtIndex(jsRuntime, index);
    if (!keyValue.isString()) {
      continue;
    }

    const std::string key = keyValue.getString(jsRuntime).utf8(jsRuntime);
    Value propertyValue = Value::undefined();
    if (unmaskedContextObject.hasProperty(jsRuntime, key.c_str())) {
      propertyValue = unmaskedContextObject.getProperty(jsRuntime, key.c_str());
    }

    maskedContextObject.setProperty(jsRuntime, key.c_str(), propertyValue);
  }

  return Value(jsRuntime, maskedContextObject);
}

Value callFunctionComponent(
    Runtime& jsRuntime,
    const Value& componentValue,
    const Value& propsValue,
    const Value& secondArg) {
  if (!componentValue.isObject()) {
    return Value::undefined();
  }

  Object componentObject = componentValue.getObject(jsRuntime);
  if (!componentObject.isFunction(jsRuntime)) {
    return Value::undefined();
  }

  Function componentFunction = componentObject.asFunction(jsRuntime);
  std::array<Value, 2> args{
    Value(jsRuntime, propsValue),
    Value(jsRuntime, secondArg),
  };
  return componentFunction.call(jsRuntime, args.data(), args.size());
}

Value callMethodWithThis(Runtime& jsRuntime, const Object& instanceObject, const char* methodName) {
  if (!instanceObject.hasProperty(jsRuntime, methodName)) {
    return Value::undefined();
  }

  Value methodValue = instanceObject.getProperty(jsRuntime, methodName);
  if (!methodValue.isObject()) {
    return Value::undefined();
  }

  Object methodObject = methodValue.getObject(jsRuntime);
  if (!methodObject.isFunction(jsRuntime)) {
    return Value::undefined();
  }

  Function methodFunction = methodObject.asFunction(jsRuntime);
  return methodFunction.callWithThis(jsRuntime, instanceObject, nullptr, 0);
}

OffscreenState* ensureOffscreenState(FiberNode& fiber) {
  auto* state = static_cast<OffscreenState*>(fiber.memoizedState);
  if (state == nullptr) {
    state = new OffscreenState();
    state->baseLanes = NoLanes;
    state->cachePool.reset();
    fiber.memoizedState = state;
  }
  return state;
}

HiddenContext makeHiddenContextFromState(const OffscreenState& state) {
  HiddenContext context;
  context.baseLanes = state.baseLanes;
  return context;
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

void markRef(FiberNode* current, FiberNode& workInProgress) {
  void* const newRef = workInProgress.ref;
  if (newRef == nullptr) {
    if (current != nullptr && current->ref != nullptr) {
      workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Ref);
    }
    return;
  }

  if (current == nullptr || current->ref != newRef) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Ref);
  }
}

bool hasSymbolType(Runtime& jsRuntime, const Value& value, const ReactSymbolDescriptor& descriptor) {
  if (!value.isSymbol()) {
    return false;
  }

  Symbol descriptorSymbol = resolveSymbol(jsRuntime, descriptor);
  Value symbolValue(jsRuntime, value.getSymbol(jsRuntime));
  Value descriptorValue(jsRuntime, descriptorSymbol);
  return Value::strictEquals(jsRuntime, symbolValue, descriptorValue);
}

bool isClassComponent(Runtime& jsRuntime, const Value& componentValue) {
  if (!componentValue.isObject()) {
    return false;
  }

  Object componentObject = componentValue.getObject(jsRuntime);
  if (!componentObject.hasProperty(jsRuntime, "prototype")) {
    return false;
  }

  Value prototypeValue = componentObject.getProperty(jsRuntime, "prototype");
  if (!prototypeValue.isObject()) {
    return false;
  }

  Object prototypeObject = prototypeValue.getObject(jsRuntime);
  if (!prototypeObject.hasProperty(jsRuntime, "isReactComponent")) {
    return false;
  }

  Value markerValue = prototypeObject.getProperty(jsRuntime, "isReactComponent");
  return !markerValue.isUndefined() && !markerValue.isNull();
}

Object clonePropsObject(Runtime& jsRuntime, const Object& source, bool omitRefProperty) {
  Object clone(jsRuntime);
  Array names = source.getPropertyNames(jsRuntime);
  const size_t length = names.size(jsRuntime);

  for (size_t index = 0; index < length; ++index) {
    Value nameValue = names.getValueAtIndex(jsRuntime, index);
    if (!nameValue.isString()) {
      continue;
    }

    const std::string key = nameValue.getString(jsRuntime).utf8(jsRuntime);
    if (omitRefProperty && key == "ref") {
      continue;
    }

    Value propValue = source.getProperty(jsRuntime, key.c_str());
    clone.setProperty(jsRuntime, key.c_str(), propValue);
  }

  return clone;
}

Value resolveClassComponentProps(Runtime& jsRuntime, const Value& componentValue, const Value& basePropsValue) {
  if (!basePropsValue.isObject()) {
    return Value(jsRuntime, basePropsValue);
  }

  Object baseProps = basePropsValue.getObject(jsRuntime);
  bool hasRef = baseProps.hasProperty(jsRuntime, "ref");

  std::unique_ptr<Object> clonedProps;
  Object* workingProps = &baseProps;

  if (hasRef) {
    clonedProps = std::make_unique<Object>(clonePropsObject(jsRuntime, baseProps, true));
    workingProps = clonedProps.get();
  }

  if (componentValue.isObject()) {
    Object componentObject = componentValue.getObject(jsRuntime);
    if (componentObject.hasProperty(jsRuntime, "defaultProps")) {
      Value defaultPropsValue = componentObject.getProperty(jsRuntime, "defaultProps");
      if (defaultPropsValue.isObject()) {
        Object defaultPropsObject = defaultPropsValue.getObject(jsRuntime);
        if (clonedProps == nullptr) {
          clonedProps = std::make_unique<Object>(clonePropsObject(jsRuntime, baseProps, false));
          workingProps = clonedProps.get();
        }

        Array defaultNames = defaultPropsObject.getPropertyNames(jsRuntime);
        const size_t defaultsLength = defaultNames.size(jsRuntime);
        for (size_t index = 0; index < defaultsLength; ++index) {
          Value nameValue = defaultNames.getValueAtIndex(jsRuntime, index);
          if (!nameValue.isString()) {
            continue;
          }

          const std::string key = nameValue.getString(jsRuntime).utf8(jsRuntime);
          Value existing = workingProps->getProperty(jsRuntime, key.c_str());
          if (existing.isUndefined()) {
            Value defaultValue = defaultPropsObject.getProperty(jsRuntime, key.c_str());
            workingProps->setProperty(jsRuntime, key.c_str(), defaultValue);
          }
        }
      }
    }
  }

  if (clonedProps != nullptr) {
    return Value(jsRuntime, *workingProps);
  }

  return Value(jsRuntime, basePropsValue);
}

bool isTruthy(Runtime& jsRuntime, const Value& value) {
  if (value.isBool()) {
    return value.getBool();
  }
  if (value.isNull() || value.isUndefined()) {
    return false;
  }
  if (value.isNumber()) {
    return value.getNumber() != 0;
  }
  if (value.isString()) {
    return !value.getString(jsRuntime).utf8(jsRuntime).empty();
  }
  if (value.isObject() || value.isSymbol()) {
    return true;
  }
  return false;
}

bool shallowEqual(Runtime& jsRuntime, const Value& left, const Value& right) {
  if (Value::strictEquals(jsRuntime, left, right)) {
    return true;
  }

  if (!left.isObject() || !right.isObject()) {
    return false;
  }

  Object leftObject = left.getObject(jsRuntime);
  Object rightObject = right.getObject(jsRuntime);

  Array leftKeys = leftObject.getPropertyNames(jsRuntime);
  Array rightKeys = rightObject.getPropertyNames(jsRuntime);

  const size_t leftLength = leftKeys.size(jsRuntime);
  const size_t rightLength = rightKeys.size(jsRuntime);
  if (leftLength != rightLength) {
    return false;
  }

  for (size_t index = 0; index < leftLength; ++index) {
    Value keyValue = leftKeys.getValueAtIndex(jsRuntime, index);
    if (!keyValue.isString()) {
      return false;
    }

    const std::string key = keyValue.getString(jsRuntime).utf8(jsRuntime);
    if (!rightObject.hasProperty(jsRuntime, key.c_str())) {
      return false;
    }

    Value leftProp = leftObject.getProperty(jsRuntime, key.c_str());
    Value rightProp = rightObject.getProperty(jsRuntime, key.c_str());
    if (!Value::strictEquals(jsRuntime, leftProp, rightProp)) {
      return false;
    }
  }

  return true;
}

bool isSimpleFunctionComponentType(Runtime& jsRuntime, const Value& typeValue) {
  if (!typeValue.isObject()) {
    return false;
  }

  Object typeObject = typeValue.getObject(jsRuntime);
  if (!typeObject.isFunction(jsRuntime)) {
    return false;
  }

  if (isClassComponent(jsRuntime, typeValue)) {
    return false;
  }

  if (typeObject.hasProperty(jsRuntime, "defaultProps")) {
    Value defaultPropsValue = typeObject.getProperty(jsRuntime, "defaultProps");
    if (!defaultPropsValue.isUndefined()) {
      return false;
    }
  }

  return true;
}

bool comparePropsWithCustomComparator(Runtime& jsRuntime, const Value& compareValue, const Value& prevProps, const Value& nextProps) {
  if (!compareValue.isObject()) {
    return false;
  }

  Object compareObject = compareValue.getObject(jsRuntime);
  if (!compareObject.isFunction(jsRuntime)) {
    return false;
  }

  Function compareFunction = compareObject.asFunction(jsRuntime);
  std::array<Value, 2> args{Value(jsRuntime, prevProps), Value(jsRuntime, nextProps)};
  Value result = compareFunction.call(jsRuntime, args.data(), args.size());
  return isTruthy(jsRuntime, result);
}

void resetSuspendedCurrentOnMountInLegacyMode(FiberNode* current, FiberNode& workInProgress) {
  if (!disableLegacyMode && (workInProgress.mode & ConcurrentMode) == NoMode) {
    if (current != nullptr) {
      current->alternate = nullptr;
      workInProgress.alternate = nullptr;
      workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Placement);
    }
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
  clone->firstContext = source->firstContext;
  return clone;
}

void pushTreeContext(FiberNode& workInProgress) {
  (void)workInProgress;
  // TODO: wire tree context stack once translated.
}

void pushRootMarkerInstance(FiberNode& workInProgress) {
  if (!enableTransitionTracing) {
    return;
  }

  push(markerInstanceStack, markerInstanceStack.current, &workInProgress);
}

void pushMarkerInstance(FiberNode& workInProgress, TracingMarkerInstance& markerInstance) {
  if (!enableTransitionTracing) {
    return;
  }

  std::optional<std::vector<TracingMarkerInstance*>> nextStack;
  if (markerInstanceStack.current.has_value()) {
    nextStack = markerInstanceStack.current;
    nextStack->push_back(&markerInstance);
  } else {
    nextStack = std::vector<TracingMarkerInstance*>{&markerInstance};
  }

  push(markerInstanceStack, std::move(nextStack), &workInProgress);
}

void pushRootTransition(
    ReactRuntime& runtime,
    FiberNode& workInProgress,
    FiberRoot& root,
    Lanes renderLanes) {
  (void)root;
  (void)renderLanes;
  if (!enableTransitionTracing) {
    return;
  }

  const auto& rootTransitions = getWorkInProgressTransitions(runtime);
  std::optional<std::vector<const Transition*>> nextTransitions;
  if (!rootTransitions.empty()) {
    nextTransitions = rootTransitions;
  } else {
    nextTransitions = std::nullopt;
  }

  push(transitionStackCursor, std::move(nextTransitions), &workInProgress);
}

void pushHostContainer(ReactRuntime& runtime, FiberNode& workInProgress, void* container) {
  auto& state = getState(runtime);
  push(state.rootHostContainerCursor, container, &workInProgress);
  push(state.hostContextFiberCursor, &workInProgress, &workInProgress);

  push(state.hostContextCursor, static_cast<void*>(nullptr), &workInProgress);
  void* const nextRootContext = hostconfig::getRootHostContext(runtime, container);
  pop(state.hostContextCursor, &workInProgress);
  push(state.hostContextCursor, nextRootContext, &workInProgress);
}

void pushHostContext(ReactRuntime& runtime, Runtime& jsRuntime, FiberNode& workInProgress) {
  auto& state = getState(runtime);
  void* const parentContext = state.hostContextCursor.current;
  if (parentContext == nullptr) {
    return;
  }

  const std::string type = getFiberType(jsRuntime, workInProgress);
  if (type.empty()) {
    return;
  }

  void* const nextContext = hostconfig::getChildHostContext(runtime, parentContext, type);
  if (nextContext == parentContext) {
    return;
  }

  push(state.hostContextFiberCursor, &workInProgress, &workInProgress);
  push(state.hostContextCursor, nextContext, &workInProgress);
}

void popHostContext(ReactRuntime& runtime, FiberNode& workInProgress) {
  auto& state = getState(runtime);
  if (state.hostContextFiberCursor.current != &workInProgress) {
    return;
  }

  pop(state.hostContextCursor, &workInProgress);
  pop(state.hostContextFiberCursor, &workInProgress);
}

void pushTopLevelLegacyContextObject(
    ReactRuntime& runtime,
    FiberNode& workInProgress,
    void* context,
    bool didChange) {
  LegacyContextEntry entry{context, didChange};
  auto& state = getState(runtime);
  push(state.legacyContextCursor, entry, &workInProgress);
}

void pushCacheProvider(FiberNode& workInProgress, void* cache) {
  push(cacheProviderCursor, cache, &workInProgress);
}

void pushHostRootContext(ReactRuntime& runtime, FiberNode& workInProgress) {
  auto* const fiberRoot = static_cast<FiberRoot*>(workInProgress.stateNode);
  if (fiberRoot == nullptr) {
    return;
  }

  if (fiberRoot->pendingContext != nullptr) {
    const bool didChange = fiberRoot->pendingContext != fiberRoot->context;
    pushTopLevelLegacyContextObject(runtime, workInProgress, fiberRoot->pendingContext, didChange);
  } else if (fiberRoot->context != nullptr) {
    pushTopLevelLegacyContextObject(runtime, workInProgress, fiberRoot->context, false);
  }

  pushHostContainer(runtime, workInProgress, fiberRoot->containerInfo);
}

void popRootMarkerInstance(FiberNode& workInProgress) {
  if (!enableTransitionTracing) {
    return;
  }

  pop(markerInstanceStack, &workInProgress);
}

void popMarkerInstance(FiberNode& workInProgress) {
  if (!enableTransitionTracing) {
    return;
  }

  pop(markerInstanceStack, &workInProgress);
}

bool hasLegacyContextChanged(ReactRuntime& runtime) {
  return getState(runtime).legacyContextCursor.current.didChange;
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

FiberNode* bailoutOnAlreadyFinishedWork(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (current != nullptr) {
    workInProgress.dependencies = cloneDependencies(current->dependencies);
  }

  if (enableProfilerTimer) {
    stopProfilerTimerIfRunning(workInProgress);
  }

  markSkippedUpdateLanes(runtime, workInProgress.lanes);

  if (!includesSomeLane(renderLanes, workInProgress.childLanes)) {
    if (current != nullptr) {
      lazilyPropagateParentContextChanges(jsRuntime, *current, workInProgress, renderLanes);
      if (!includesSomeLane(renderLanes, workInProgress.childLanes)) {
        return nullptr;
      }
    } else {
      return nullptr;
    }
  }

  cloneChildFibers(current, workInProgress);
  return workInProgress.child;
}

FiberNode* attemptEarlyBailoutIfNoScheduledUpdate(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (current == nullptr) {
    return nullptr;
  }

  switch (workInProgress.tag) {
    case WorkTag::HostRoot: {
      pushHostRootContext(runtime, workInProgress);
      auto* const root = static_cast<FiberRoot*>(workInProgress.stateNode);
      if (root != nullptr) {
        pushRootTransition(runtime, workInProgress, *root, renderLanes);
      }

      if (enableTransitionTracing) {
        pushRootMarkerInstance(workInProgress);
      }

      auto* const currentState = static_cast<HostRootMemoizedState*>(current->memoizedState);
      void* const cache = currentState != nullptr ? currentState->cache : nullptr;
      pushCacheProvider(workInProgress, cache);
      resetHydrationState(runtime);
      break;
    }
    case WorkTag::HostSingleton:
    case WorkTag::HostComponent:
      pushHostContext(runtime, jsRuntime, workInProgress);
      break;
    case WorkTag::ClassComponent:
      break;
    case WorkTag::HostPortal: {
      void* const containerInfo = getPortalContainerInfo(workInProgress);
      pushHostContainer(runtime, workInProgress, containerInfo);
      break;
    }
    case WorkTag::ContextProvider: {
      Value contextValue = cloneJsiValue(jsRuntime, workInProgress.type);
      Value memoizedPropsValue = cloneJsiValue(jsRuntime, workInProgress.memoizedProps);
      Object memoizedPropsObject = ensureObject(jsRuntime, memoizedPropsValue);

      Value nextValue = Value::undefined();
      if (memoizedPropsObject.hasProperty(jsRuntime, kValuePropName)) {
        nextValue = memoizedPropsObject.getProperty(jsRuntime, kValuePropName);
      }

      pushProvider(jsRuntime, workInProgress, contextValue, nextValue);
      break;
    }
    case WorkTag::Profiler:
      if (enableProfilerTimer) {
        const bool hasChildWork = includesSomeLane(renderLanes, workInProgress.childLanes);
        if (hasChildWork) {
          workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Update);
        }

        if (enableProfilerCommitHooks) {
          workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Passive);
          auto* state = ensureProfilerStateNode(workInProgress);
          state->effectDuration = -0.0;
          state->passiveEffectDuration = -0.0;
        }
      }
      break;
    case WorkTag::ActivityComponent: {
      if (workInProgress.memoizedState != nullptr) {
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | DidCapture);
        pushDehydratedActivitySuspenseHandler(workInProgress);
        return nullptr;
      }
      break;
    }
    case WorkTag::SuspenseComponent: {
      auto* const state = static_cast<SuspenseState*>(workInProgress.memoizedState);
      if (state != nullptr) {
        if (state->dehydrated != nullptr) {
          pushPrimaryTreeSuspenseHandler(workInProgress);
          workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | DidCapture);
          return nullptr;
        }

        FiberNode* const primaryChildFragment = workInProgress.child;
        const Lanes primaryChildLanes =
            primaryChildFragment != nullptr ? primaryChildFragment->childLanes : NoLanes;
        if (includesSomeLane(renderLanes, primaryChildLanes)) {
          return updateSuspenseComponent(runtime, jsRuntime, current, workInProgress, renderLanes);
        }

        pushPrimaryTreeSuspenseHandler(workInProgress);
        FiberNode* const child = bailoutOnAlreadyFinishedWork(
            runtime, jsRuntime, current, workInProgress, renderLanes);
        return child != nullptr ? child->sibling : nullptr;
      }

      pushPrimaryTreeSuspenseHandler(workInProgress);
      break;
    }
    case WorkTag::SuspenseListComponent: {
      const bool didSuspendBefore = (current->flags & DidCapture) != 0;
      bool hasChildWork = includesSomeLane(renderLanes, workInProgress.childLanes);

      if (!hasChildWork) {
        lazilyPropagateParentContextChanges(jsRuntime, *current, workInProgress, renderLanes);
        hasChildWork = includesSomeLane(renderLanes, workInProgress.childLanes);
      }

      if (didSuspendBefore) {
        if (hasChildWork) {
          return updateSuspenseListComponent(runtime, jsRuntime, current, workInProgress, renderLanes);
        }
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | DidCapture);
      }

      pushSuspenseListContext(workInProgress, getCurrentSuspenseContext());

      if (!hasChildWork) {
        return nullptr;
      }
      break;
    }
    case WorkTag::OffscreenComponent:
      workInProgress.lanes = NoLanes;
      return updateOffscreenComponent(
          runtime,
          jsRuntime,
          current,
          workInProgress,
          renderLanes,
          workInProgress.pendingProps);
    case WorkTag::CacheComponent: {
      auto* const cacheState = static_cast<CacheComponentState*>(current->memoizedState);
      void* const cache = cacheState != nullptr ? cacheState->cache : nullptr;
      pushCacheProvider(workInProgress, cache);
      break;
    }
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing) {
        auto* const instance = static_cast<TracingMarkerInstance*>(workInProgress.stateNode);
        if (instance != nullptr) {
          pushMarkerInstance(workInProgress, *instance);
        }
        break;
      }
      // Intentional fallthrough to LegacyHiddenComponent.
    }
    case WorkTag::LegacyHiddenComponent: {
      if (enableLegacyHidden) {
        workInProgress.lanes = NoLanes;
        return updateLegacyHiddenComponent(
            runtime, jsRuntime, current, workInProgress, renderLanes);
      }
      break;
    }
    default:
      break;
  }

  return bailoutOnAlreadyFinishedWork(runtime, jsRuntime, current, workInProgress, renderLanes);
}

FiberNode* mountLazyComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* elementType,
    Lanes renderLanes) {
  resetSuspendedCurrentOnMountInLegacyMode(current, workInProgress);

  Value propsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Value lazyValue = cloneJsiValue(jsRuntime, elementType);
  Value componentValue = resolveLazy(jsRuntime, lazyValue);

  workInProgress.type = cloneForFiber(jsRuntime, componentValue);

  if (componentValue.isObject()) {
    Object componentObject = componentValue.getObject(jsRuntime);

    if (componentObject.isFunction(jsRuntime)) {
      if (isClassComponent(jsRuntime, componentValue)) {
        Value resolvedPropsValue = resolveClassComponentProps(jsRuntime, componentValue, propsValue);
        auto* resolvedPropsStorage = cloneForFiber(jsRuntime, resolvedPropsValue);

        workInProgress.tag = WorkTag::ClassComponent;
        FiberNode* result = updateClassComponent(
            runtime,
            jsRuntime,
            nullptr,
            workInProgress,
            const_cast<void*>(workInProgress.type),
            resolvedPropsStorage,
            renderLanes);
        delete resolvedPropsStorage;
        return result;
      }

      workInProgress.tag = WorkTag::FunctionComponent;
      return updateFunctionComponent(
          runtime,
          jsRuntime,
          nullptr,
          workInProgress,
          const_cast<void*>(workInProgress.type),
          workInProgress.pendingProps,
          renderLanes);
    }

    if (componentObject.hasProperty(jsRuntime, "$$typeof")) {
      Value typeOfValue = componentObject.getProperty(jsRuntime, "$$typeof");

      if (hasSymbolType(jsRuntime, typeOfValue, REACT_FORWARD_REF_TYPE)) {
        workInProgress.tag = WorkTag::ForwardRef;
        return updateForwardRef(
            runtime,
            jsRuntime,
            nullptr,
            workInProgress,
            const_cast<void*>(workInProgress.type),
            workInProgress.pendingProps,
            renderLanes);
      }

      if (hasSymbolType(jsRuntime, typeOfValue, REACT_MEMO_TYPE)) {
        workInProgress.tag = WorkTag::MemoComponent;
        return updateMemoComponent(
            runtime,
            jsRuntime,
            nullptr,
            workInProgress,
            const_cast<void*>(workInProgress.type),
            workInProgress.pendingProps,
            renderLanes);
      }
    }
  }

  std::string hint;
  if (componentValue.isObject()) {
    hint = valueToString(jsRuntime, componentValue);
  }

  throw std::runtime_error(
      std::string("Element type is invalid in mountLazyComponent. ") +
      (hint.empty() ? std::string{} : std::string("Received: ") + hint));
}

FiberNode* updateHostComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {

  const std::string type = getFiberType(jsRuntime, workInProgress);
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  bool isDirectTextChild = false;
  if (!type.empty()) {
    pushHostContext(runtime, jsRuntime, workInProgress);
    isDirectTextChild = hostconfig::shouldSetTextContent(jsRuntime, type, nextPropsObject);
  }

  if (current == nullptr && !type.empty() && getIsHydrating(runtime)) {
    auto* hydratableInstance = tryToClaimNextHydratableInstance(runtime, workInProgress, type);
    if (hydratableInstance != nullptr) {
      auto sharedInstance = hydratableInstance->shared_from_this();
      setHostInstance(workInProgress, sharedInstance);
      clearHostUpdatePayload(workInProgress);

      auto componentInstance = std::dynamic_pointer_cast<ReactDOMComponent>(sharedInstance);
      if (componentInstance) {
        Value prevPropsValue = propsMapToValue(jsRuntime, componentInstance->getProps());
        Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);
        if (!payload.isUndefined()) {
          queueHydrationError(runtime, workInProgress, "Hydration: host component prop mismatch");
          storeHostUpdatePayload(jsRuntime, workInProgress, payload);
          markUpdate(workInProgress);
        }

        if (isDirectTextChild && nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
          Value textValue = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
          std::string nextTextContent = valueToString(jsRuntime, textValue);
          if (nextTextContent != componentInstance->getTextContent()) {
            queueHydrationError(runtime, workInProgress, "Hydration: host component text content mismatch");
            componentInstance->setTextContent(nextTextContent);
            markUpdate(workInProgress);
          }
        }

        componentInstance->setProps(jsRuntime, nextPropsObject);
      }
    } else {
      workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
      resetHydrationState(runtime);
    }
  }

  Value nextChildren = Value::undefined();
  if (isDirectTextChild) {
    nextChildren = Value::null();
  } else if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  if (!isDirectTextChild && current != nullptr && !type.empty()) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    if (prevPropsValue.isObject()) {
      Object prevPropsObject = prevPropsValue.getObject(jsRuntime);
      if (hostconfig::shouldSetTextContent(jsRuntime, type, prevPropsObject)) {
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ContentReset);
      }
    }
  }

  // TODO: integrate hydration-aware host context transitions once those subsystems are available.

  markRef(current, workInProgress);
  clearHostUpdatePayload(workInProgress);

  if (current == nullptr) {
    return mountChildFibers(nullptr, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(nullptr, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateHostHoistable(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  (void)renderLanes;

  markRef(current, workInProgress);

  // Resource-aware hoistable support is not yet available; memoized state is cleared
  // so the complete phase can treat the fiber as instance-backed when needed.
  workInProgress.memoizedState = nullptr;

  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);
  const std::string type = getFiberType(jsRuntime, workInProgress);

  if (current == nullptr) {
    if (getIsHydrating(runtime)) {
      if (!type.empty()) {
        auto* hydratableInstance = tryToClaimNextHydratableInstance(runtime, workInProgress, type);
        if (hydratableInstance != nullptr) {
          auto sharedInstance = hydratableInstance->shared_from_this();
          setHostInstance(workInProgress, sharedInstance);
          auto componentInstance = std::dynamic_pointer_cast<ReactDOMComponent>(sharedInstance);
          if (componentInstance) {
            clearHostUpdatePayload(workInProgress);
            Value prevPropsValue = propsMapToValue(jsRuntime, componentInstance->getProps());
            Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);
            if (!payload.isUndefined()) {
              queueHydrationError(runtime, workInProgress, "Hydration: hoistable prop mismatch");
              storeHostUpdatePayload(jsRuntime, workInProgress, payload);
              markUpdate(workInProgress);
            }
            componentInstance->setProps(jsRuntime, nextPropsObject);
          } else {
            clearHostUpdatePayload(workInProgress);
          }
        } else {
          queueHydrationError(runtime, workInProgress, "Hydration: missing hydratable hoistable instance");
          clearHostUpdatePayload(workInProgress);
          workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
          resetHydrationState(runtime);
        }
      }
    } else if (!type.empty()) {
      auto instance = hostconfig::createHoistableInstance(runtime, jsRuntime, type, nextPropsObject);
      setHostInstance(workInProgress, instance);
      clearHostUpdatePayload(workInProgress);
    }
    return nullptr;
  }

  if (!getIsHydrating(runtime)) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    Value payload = hostconfig::prepareUpdate(runtime, jsRuntime, prevPropsValue, nextPropsValue, false);

    if (!payload.isUndefined()) {
      storeHostUpdatePayload(jsRuntime, workInProgress, payload);
      markUpdate(workInProgress);
    } else {
      clearHostUpdatePayload(workInProgress);
    }

    if (workInProgress.stateNode == nullptr) {
      auto instance = getHostInstance(*current);
      if (instance) {
        setHostInstance(workInProgress, instance);
      }
    }
  }

  return nullptr;
}

FiberNode* updateHostSingleton(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  pushHostContext(runtime, jsRuntime, workInProgress);

  Value nextChildren = Value::undefined();
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  if (nextPropsValue.isObject()) {
    Object nextPropsObject = nextPropsValue.getObject(jsRuntime);
    if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
      nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
    }
  }

  const std::string type = getFiberType(jsRuntime, workInProgress);
  markRef(current, workInProgress);

  if (current == nullptr) {
    if (!type.empty() && getIsHydrating(runtime)) {
      auto* hydratableSingleton = claimHydratableSingleton(runtime, workInProgress, type);
      if (hydratableSingleton != nullptr) {
        auto sharedInstance = hydratableSingleton->shared_from_this();
        setHostInstance(workInProgress, sharedInstance);
      } else {
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
        resetHydrationState(runtime);
      }
    }

    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | LayoutStatic);
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateHostText(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress) {
  const bool isHydrating = getIsHydrating(runtime);
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  const std::string nextText = valueToString(jsRuntime, nextPropsValue);

  if (current == nullptr) {
    if (isHydrating) {
      auto* hydratableText = tryToClaimNextHydratableTextInstance(runtime, workInProgress);
      if (hydratableText != nullptr) {
        auto sharedInstance = hydratableText->shared_from_this();
        setHostInstance(workInProgress, sharedInstance);
        const bool needsUpdate = hostconfig::prepareToHydrateHostTextInstance(runtime, sharedInstance, nextText);
        if (needsUpdate) {
          queueHydrationError(runtime, workInProgress, "Hydration: text content mismatch");
          markUpdate(workInProgress);
        }
      } else {
        queueHydrationError(runtime, workInProgress, "Hydration: missing hydratable text instance");
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | ForceClientRender);
        resetHydrationState(runtime);
      }
    }
    return nullptr;
  }

  if (!isHydrating) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    const std::string prevText = valueToString(jsRuntime, prevPropsValue);
    if (nextText != prevText) {
      markUpdate(workInProgress);
    }
  }

  return nullptr;
}

FiberNode* updateSuspenseComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

#ifdef DEBUG {
  if (shouldSuspend(workInProgress)) {
    
  }
}
#endif

  Value nextPrimaryChildren = Value::undefined();
  if (nextPropsValue.isObject() && nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextPrimaryChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  Value nextFallbackChildren = Value::undefined();
  if (nextPropsValue.isObject() && nextPropsObject.hasProperty(jsRuntime, "fallback")) {
    nextFallbackChildren = nextPropsObject.getProperty(jsRuntime, "fallback");
  }

  if (nextFallbackChildren.isUndefined()) {
    nextFallbackChildren = Value::null();
  }

  const bool isHydrating = getIsHydrating(runtime);

  const bool didSuspend = (workInProgress.flags & DidCapture) != 0;
  bool showFallback = didSuspend || shouldRemainOnFallback(current);

  if (showFallback) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags & ~DidCapture);
  }

  const bool didPrimaryChildrenDefer = (workInProgress.flags & DidDefer) != 0;
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags & ~DidDefer);

  const Lanes primaryTreeLanes = getRemainingWorkInPrimaryTree(runtime, current, didPrimaryChildrenDefer, renderLanes);
  const Lanes remainingPrimaryLanes = showFallback ? primaryTreeLanes : NoLanes;

  FiberNode* nextChild = nullptr;

  if (current == nullptr) {
    if (isHydrating) {
      if (tryHandleSuspenseHydrationOnMount(
              runtime,
              jsRuntime,
              workInProgress,
              nextPrimaryChildren,
              nextFallbackChildren,
              showFallback,
              renderLanes,
              primaryTreeLanes,
              nextChild)) {
        return nextChild;
      }
    }

    if (showFallback) {
      pushFallbackTreeSuspenseHandler(workInProgress);
      workInProgress.memoizedState = const_cast<SuspenseState*>(&kSuspendedMarker);
      mountSuspenseFallbackChildren(
          runtime, jsRuntime, workInProgress, nextPrimaryChildren, nextFallbackChildren, renderLanes);
      FiberNode* primaryChildFragment = workInProgress.child;
      if (primaryChildFragment != nullptr) {
        primaryChildFragment->memoizedState = mountSuspenseOffscreenState(renderLanes);
        primaryChildFragment->childLanes = primaryTreeLanes;
        nextChild = bailoutOffscreenComponent(nullptr, *primaryChildFragment);
      } else {
        nextChild = nullptr;
      }
    } else if (enableCPUSuspense && nextPropsValue.isObject() && nextPropsObject.hasProperty(jsRuntime, "unstable_expectedLoadTime")) {
      Value expectedLoadTimeValue = nextPropsObject.getProperty(jsRuntime, "unstable_expectedLoadTime");
      if (expectedLoadTimeValue.isNumber()) {
        // CPU-bound树：跳过主内容，挂起 primary，立刻调度重试
        pushFallbackTreeSuspenseHandler(workInProgress);
        mountSuspenseFallbackChildren(
            runtime, jsRuntime, workInProgress, nextPrimaryChildren, nextFallbackChildren, renderLanes);
        FiberNode* primaryChildFragment = workInProgress.child;
        if (primaryChildFragment != nullptr) {
          primaryChildFragment->memoizedState = mountSuspenseOffscreenState(renderLanes);
          primaryChildFragment->childLanes = primaryTreeLanes;
          workInProgress.memoizedState = const_cast<SuspenseState*>(&kSuspendedMarker);
          workInProgress.lanes = laneToLanes(SomeRetryLane);
          nextChild = bailoutOffscreenComponent(nullptr, *primaryChildFragment);
        } else {
          nextChild = nullptr;
        }
      } else {
        pushPrimaryTreeSuspenseHandler(workInProgress);
        workInProgress.memoizedState = nullptr;
        nextChild = mountSuspensePrimaryChildren(runtime, jsRuntime, workInProgress, nextPrimaryChildren, renderLanes);
      }
    } else {
      pushPrimaryTreeSuspenseHandler(workInProgress);
      workInProgress.memoizedState = nullptr;
      nextChild = mountSuspensePrimaryChildren(runtime, jsRuntime, workInProgress, nextPrimaryChildren, renderLanes);
    }
  } else {
    SuspenseState* prevSuspenseState = current->memoizedState != nullptr
        ? static_cast<SuspenseState*>(current->memoizedState)
        : nullptr;
    bool wasDehydrated = prevSuspenseState != nullptr && prevSuspenseState != &kSuspendedMarker &&
        prevSuspenseState->dehydrated != nullptr;
    if (wasDehydrated) {
      handleDehydratedSuspenseUpdateFallback(runtime, *current, workInProgress, *prevSuspenseState);
      showFallback = true;
    }

    if (showFallback) {
      pushFallbackTreeSuspenseHandler(workInProgress);
      workInProgress.memoizedState = const_cast<SuspenseState*>(&kSuspendedMarker);
      updateSuspenseFallbackChildren(
          runtime, jsRuntime, *current, workInProgress, nextPrimaryChildren, nextFallbackChildren, renderLanes);
      FiberNode* primaryChildFragment = workInProgress.child;
      OffscreenState* prevOffscreenState = current->child != nullptr
          ? static_cast<OffscreenState*>(current->child->memoizedState)
          : nullptr;
      if (primaryChildFragment != nullptr) {
        primaryChildFragment->memoizedState = updateSuspenseOffscreenState(prevOffscreenState, renderLanes);
        primaryChildFragment->childLanes = primaryTreeLanes;
        nextChild = bailoutOffscreenComponent(current->child, *primaryChildFragment);
      } else {
        nextChild = nullptr;
      }
    } else {
      pushPrimaryTreeSuspenseHandler(workInProgress);
      workInProgress.memoizedState = nullptr;
      nextChild = updateSuspensePrimaryChildren(
          runtime, jsRuntime, *current, workInProgress, nextPrimaryChildren, renderLanes);
    }
  }

  if (!showFallback) {
    workInProgress.childLanes = remainingPrimaryLanes;
  }


  return nextChild;
}

FiberNode* updatePortalComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  void* containerInfo = getPortalContainerInfo(workInProgress);
  pushHostContainer(runtime, workInProgress, containerInfo);

  Value nextChildren = cloneJsiValue(jsRuntime, workInProgress.pendingProps);

  if (current == nullptr) {
    return reconcileChildFibers(&runtime, jsRuntime, nullptr, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateForwardRef(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  void* elementType,
  void* pendingProps,
  Lanes renderLanes) {
  Value componentValue = cloneJsiValue(jsRuntime, elementType);
  if (!componentValue.isObject()) {
    return workInProgress.child;
  }

  Object componentObject = componentValue.getObject(jsRuntime);
  if (!componentObject.hasProperty(jsRuntime, "render")) {
    return workInProgress.child;
  }

  Value renderValue = componentObject.getProperty(jsRuntime, "render");
  if (!isCallable(jsRuntime, renderValue)) {
    return workInProgress.child;
  }

  Function renderFunction = renderValue.getObject(jsRuntime).asFunction(jsRuntime);

  Value propsValue = cloneJsiValue(jsRuntime, pendingProps);
  Value propsWithoutRefValue = Value(jsRuntime, propsValue);
  if (propsValue.isObject()) {
    Object propsObject = propsValue.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, "ref")) {
      Object sanitizedProps = clonePropsObject(jsRuntime, propsObject, true);
      propsWithoutRefValue = Value(jsRuntime, sanitizedProps);
    }
  }

  Value refValue = cloneJsiValue(jsRuntime, workInProgress.ref);

  prepareToReadContext(workInProgress, renderLanes);

  FunctionComponentRender renderCallback = [&]() -> Value {
    std::array<Value, 2> args{Value(jsRuntime, propsWithoutRefValue), Value(jsRuntime, refValue)};
    return renderFunction.call(jsRuntime, args.data(), args.size());
  };

  Value nextChildren = renderWithHooks(runtime, jsRuntime, workInProgress, current, renderLanes, renderCallback);
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateFragment(
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  Value nextChildren = cloneJsiValue(jsRuntime, workInProgress.pendingProps);

  if (enableFragmentRefs) {
    markRef(current, workInProgress);
  }

  if (current == nullptr) {
    return mountChildFibers(nullptr, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(nullptr, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateMode(
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  Value nextChildren = Value::undefined();

  Value pendingPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  if (pendingPropsValue.isObject()) {
    Object propsObject = pendingPropsValue.getObject(jsRuntime);
    if (propsObject.hasProperty(jsRuntime, kChildrenPropName)) {
      nextChildren = propsObject.getProperty(jsRuntime, kChildrenPropName);
    }
  }

  if (current == nullptr) {
    return mountChildFibers(nullptr, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(nullptr, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateProfiler(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (enableProfilerTimer) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Update);

    if (enableProfilerCommitHooks) {
      workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Passive);
      ProfilerStateNode* state = ensureProfilerStateNode(workInProgress);
      state->effectDuration = -0.0;
      state->passiveEffectDuration = -0.0;
    }
  }

  Value nextChildren = Value::undefined();
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  if (nextPropsValue.isObject()) {
    Object nextPropsObject = nextPropsValue.getObject(jsRuntime);
    if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
      nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
    }
  }

  if (current == nullptr) {
    return mountChildFibers(nullptr, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(nullptr, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateContextProvider(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {
  (void)runtime;

  Value contextValue = cloneJsiValue(jsRuntime, workInProgress.type);
  Value newPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object newPropsObject = ensureObject(jsRuntime, newPropsValue);

  Value nextValue = Value::undefined();
  if (newPropsObject.hasProperty(jsRuntime, kValuePropName)) {
    nextValue = newPropsObject.getProperty(jsRuntime, kValuePropName);
  }

  pushProvider(jsRuntime, workInProgress, contextValue, nextValue);

  Value nextChildren = Value::undefined();
  if (newPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = newPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateContextConsumer(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {
  Value consumerTypeValue = cloneJsiValue(jsRuntime, workInProgress.type);
  Object consumerTypeObject = ensureObject(jsRuntime, consumerTypeValue);

  Value contextValue = Value::undefined();
  if (consumerTypeObject.hasProperty(jsRuntime, kContextPropName)) {
    contextValue = consumerTypeObject.getProperty(jsRuntime, kContextPropName);
  }

  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  Value renderValue = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    renderValue = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  prepareToReadContext(workInProgress, renderLanes);
  Value newValue = readContext(jsRuntime, workInProgress, contextValue);

  Value nextChildren = Value::undefined();
  if (renderValue.isObject()) {
    Object renderObject = renderValue.getObject(jsRuntime);
    if (renderObject.isFunction(jsRuntime)) {
  Function renderFunction = renderObject.asFunction(jsRuntime);
  nextChildren = renderFunction.call(jsRuntime, Value(jsRuntime, newValue));
    }
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateMemoComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* componentType,
    void* pendingProps,
    Lanes renderLanes) {
  Value memoTypeValue = cloneJsiValue(jsRuntime, componentType);
  Object memoTypeObject = ensureObject(jsRuntime, memoTypeValue);

  Value nextPropsValue = cloneJsiValue(jsRuntime, pendingProps);
  Value compareValue = Value::undefined();
  bool hasCompare = false;
  bool useDefaultCompare = true;

  if (memoTypeObject.hasProperty(jsRuntime, "compare")) {
    compareValue = memoTypeObject.getProperty(jsRuntime, "compare");
    if (compareValue.isNull() || compareValue.isUndefined()) {
      useDefaultCompare = true;
    } else if (isCallable(jsRuntime, compareValue)) {
      hasCompare = true;
      useDefaultCompare = false;
    } else {
      useDefaultCompare = false;
    }
  }

  Value innerTypeValue = memoTypeObject.hasProperty(jsRuntime, "type")
      ? memoTypeObject.getProperty(jsRuntime, "type")
      : Value(jsRuntime, memoTypeValue);

  if (current != nullptr) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    const bool hasScheduledUpdateOrContext = checkScheduledUpdateOrContext(*current, renderLanes);
    if (!hasScheduledUpdateOrContext) {
      bool areEqual = false;
      if (hasCompare) {
        areEqual = comparePropsWithCustomComparator(jsRuntime, compareValue, prevPropsValue, nextPropsValue);
      } else if (useDefaultCompare) {
        areEqual = shallowEqual(jsRuntime, prevPropsValue, nextPropsValue);
      }

      if (areEqual && current->ref == workInProgress.ref) {
        workInProgress.child = current->child;
        workInProgress.memoizedProps = current->memoizedProps;
        workInProgress.pendingProps = current->memoizedProps;
        workInProgress.lanes = current->lanes;
        workInProgress.childLanes = current->childLanes;
        setDidReceiveUpdate(runtime, false);
        return workInProgress.child;
      }
    }
  } else if (isSimpleFunctionComponentType(jsRuntime, innerTypeValue) && useDefaultCompare) {
    auto* resolvedTypeStorage = cloneForFiber(jsRuntime, innerTypeValue);
    workInProgress.tag = WorkTag::SimpleMemoComponent;
    workInProgress.type = resolvedTypeStorage;
    return updateSimpleMemoComponent(
        runtime,
        jsRuntime,
        current,
        workInProgress,
        resolvedTypeStorage,
        pendingProps,
        renderLanes);
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  if (innerTypeValue.isObject()) {
    Object innerTypeObject = innerTypeValue.getObject(jsRuntime);

    if (innerTypeObject.isFunction(jsRuntime)) {
      if (isClassComponent(jsRuntime, innerTypeValue)) {
        Value resolvedPropsValue = resolveClassComponentProps(jsRuntime, innerTypeValue, nextPropsValue);
        auto* typeStorage = cloneForFiber(jsRuntime, innerTypeValue);
        auto* resolvedPropsStorage = cloneForFiber(jsRuntime, resolvedPropsValue);
        FiberNode* result = updateClassComponent(
            runtime,
            jsRuntime,
            current,
            workInProgress,
            typeStorage,
            resolvedPropsStorage,
            renderLanes);
        delete resolvedPropsStorage;
        delete typeStorage;
        return result;
      }

      auto* typeStorage = cloneForFiber(jsRuntime, innerTypeValue);
      FiberNode* result = updateFunctionComponent(
          runtime,
          jsRuntime,
          current,
          workInProgress,
          typeStorage,
          pendingProps,
          renderLanes);
      delete typeStorage;
      return result;
    }

    if (innerTypeObject.hasProperty(jsRuntime, "$$typeof")) {
      Value innerTypeofValue = innerTypeObject.getProperty(jsRuntime, "$$typeof");

      if (hasSymbolType(jsRuntime, innerTypeofValue, REACT_FORWARD_REF_TYPE)) {
        auto* typeStorage = cloneForFiber(jsRuntime, innerTypeValue);
        FiberNode* result = updateForwardRef(
            runtime,
            jsRuntime,
            current,
            workInProgress,
            typeStorage,
            pendingProps,
            renderLanes);
        delete typeStorage;
        return result;
      }

      if (hasSymbolType(jsRuntime, innerTypeofValue, REACT_MEMO_TYPE)) {
        auto* typeStorage = cloneForFiber(jsRuntime, innerTypeValue);
        FiberNode* result = updateMemoComponent(
            runtime,
            jsRuntime,
            current,
            workInProgress,
            typeStorage,
            pendingProps,
            renderLanes);
        delete typeStorage;
        return result;
      }
    }
  }

  std::string hint;
  if (innerTypeValue.isObject()) {
    hint = valueToString(jsRuntime, innerTypeValue);
  }

  throw std::runtime_error(
      std::string("Element type is invalid in updateMemoComponent. ") +
      (hint.empty() ? std::string{} : std::string("Received: ") + hint));
}

FiberNode* updateFunctionComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* componentType,
    void* pendingProps,
    Lanes renderLanes) {
  Value componentValue = cloneJsiValue(jsRuntime, componentType);
  Value propsValue = cloneJsiValue(jsRuntime, pendingProps);

  if constexpr (kDevBuild) {
    const std::string componentName = getComponentNameFromType(jsRuntime, componentValue);
    warnOnBadClass(jsRuntime, componentValue, componentName);
    if (current == nullptr) {
      validateFunctionComponentInDev(jsRuntime, componentValue, componentName);
      warnOnLegacyContextTypes(jsRuntime, componentValue, componentName);
    }

    if ((workInProgress.mode & StrictLegacyMode) != NoMode) {
      recordLegacyContextWarning(workInProgress, nullptr);
    }
  }

  prepareToReadContext(workInProgress, renderLanes);

  Value contextValue = Value::undefined();
  if (!disableLegacyContext && !disableLegacyContextForFunctionComponents) {
    contextValue = maskLegacyContextForComponent(runtime, jsRuntime, componentValue);
  }

  FunctionComponentRender renderCallback = [&]() -> Value {
    return callFunctionComponent(jsRuntime, componentValue, propsValue, contextValue);
  };

  ComponentRenderProfilerScope profilerScope(workInProgress, enableSchedulingProfiler);
  Value nextChildren = renderWithHooks(
    runtime,
    jsRuntime,
    workInProgress,
    current,
    renderLanes,
    renderCallback);

  const bool hasId = checkDidRenderIdHook(runtime);

  if (current != nullptr && !getDidReceiveUpdate(runtime)) {
    bailoutHooks(*current, workInProgress, renderLanes);
    setDidReceiveUpdate(runtime, false);
    return bailoutOnAlreadyFinishedWork(runtime, jsRuntime, current, workInProgress, renderLanes);
  }

  if (getIsHydrating(runtime) && hasId) {
    pushMaterializedTreeId(runtime, workInProgress);
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateClassComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  void* componentType,
  void* pendingProps,
  Lanes renderLanes) {
  Value componentValue = cloneJsiValue(jsRuntime, componentType);
  Value propsValue = cloneJsiValue(jsRuntime, pendingProps);

  Value instanceValue = Value::undefined();
  if (current != nullptr && current->stateNode != nullptr) {
    workInProgress.stateNode = current->stateNode;
  }

  if (workInProgress.stateNode == nullptr) {
    if (!isCallable(jsRuntime, componentValue)) {
      return workInProgress.child;
    }
    Object ctorObject = componentValue.getObject(jsRuntime);
    Function ctorFunction = ctorObject.asFunction(jsRuntime);
  instanceValue = ctorFunction.callAsConstructor(jsRuntime, Value(jsRuntime, propsValue));
    workInProgress.stateNode = cloneForFiber(jsRuntime, instanceValue);
  } else {
    auto* storedInstance = static_cast<Value*>(workInProgress.stateNode);
    instanceValue = Value(jsRuntime, *storedInstance);
  }

  if (!instanceValue.isObject()) {
    return workInProgress.child;
  }

  if (current != nullptr) {
    cloneUpdateQueue(*current, workInProgress);
  } else {
    initializeUpdateQueue(workInProgress);
  }
  ensureUpdateQueue(workInProgress);

  Object instanceObject = instanceValue.getObject(jsRuntime);
  instanceObject.setProperty(jsRuntime, "props", propsValue);

  resetHasForceUpdateBeforeProcessing(runtime);
  processUpdateQueue(runtime, jsRuntime, workInProgress, propsValue, instanceValue, renderLanes);
  suspendIfUpdateReadFromEntangledAsyncAction(runtime);

  const bool hasForceUpdate = checkHasForceUpdateAfterProcessing(runtime);
  (void)hasForceUpdate;

  Value nextChildren = callMethodWithThis(jsRuntime, instanceObject, "render");
  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | PerformedWork);

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateSimpleMemoComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* componentType,
    void* pendingProps,
    Lanes renderLanes) {
  if (current != nullptr) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    Value nextPropsValue = cloneJsiValue(jsRuntime, pendingProps);

    if (shallowEqual(jsRuntime, prevPropsValue, nextPropsValue) && current->ref == workInProgress.ref) {
      setDidReceiveUpdate(runtime, false);
      workInProgress.pendingProps = current->memoizedProps;
      workInProgress.memoizedProps = current->memoizedProps;
      workInProgress.child = current->child;
      workInProgress.lanes = current->lanes;
      workInProgress.childLanes = current->childLanes;

      if (!checkScheduledUpdateOrContext(*current, renderLanes)) {
        return workInProgress.child;
      }

      if ((current->flags & ForceUpdateForLegacySuspense) != 0) {
        setDidReceiveUpdate(runtime, true);
      }
    }
  }

  return updateFunctionComponent(
    runtime, 
    jsRuntime, 
    current, 
    workInProgress, 
    componentType, 
    pendingProps, 
    renderLanes);
}

FiberNode* mountIncompleteClassComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  void* component,
  void* resolvedProps,
  Lanes renderLanes) {
  resetSuspendedCurrentOnMountInLegacyMode(current, workInProgress);
  workInProgress.tag = WorkTag::ClassComponent;

  return updateClassComponent(
      runtime,
      jsRuntime,
      nullptr,
      workInProgress,
      component,
      resolvedProps,
      renderLanes);
}

FiberNode* mountIncompleteFunctionComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    void* component,
    void* resolvedProps,
    Lanes renderLanes) {
  resetSuspendedCurrentOnMountInLegacyMode(current, workInProgress);
  workInProgress.tag = WorkTag::FunctionComponent;

  return updateFunctionComponent(
      runtime,
      jsRuntime,
      nullptr,
      workInProgress,
      component,
      resolvedProps,
      renderLanes);
}

FiberNode* updateSuspenseListComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  Value nextChildren = Value::undefined();
  if (nextPropsValue.isObject() && nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  SuspenseContext parentContext = getCurrentSuspenseContext();
  const bool shouldForceFallback = hasSuspenseListContext(parentContext, ForceSuspenseFallback);
  const SuspenseContext nextContext = shouldForceFallback
      ? setShallowSuspenseListContext(parentContext, ForceSuspenseFallback)
      : setDefaultShallowSuspenseListContext(parentContext);

  if (shouldForceFallback) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | DidCapture);
  }

  pushSuspenseListContext(workInProgress, nextContext);

  FiberNode* firstChild = nullptr;
  if (current == nullptr) {
    firstChild = mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  } else {
    firstChild = reconcileChildFibers(&runtime, jsRuntime, current->child, workInProgress, nextChildren, renderLanes);
  }

  workInProgress.memoizedState = nullptr;
  return firstChild;
}

FiberNode* updateScopeComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {
  Value nextChildren = Value::undefined();
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  if (nextPropsValue.isObject()) {
    Object nextPropsObject = nextPropsValue.getObject(jsRuntime);
    if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
      nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
    }
  }

  markRef(current, workInProgress);

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateActivityComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  Value nextChildrenValue = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildrenValue = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  Value modeValue = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, "mode")) {
    modeValue = nextPropsObject.getProperty(jsRuntime, "mode");
  }

  const OffscreenMode offscreenMode = resolveActivityMode(jsRuntime, modeValue);

  if (getIsHydrating(runtime)) {
    queueHydrationError(runtime, workInProgress, "Hydration for Activity boundaries is not yet supported");
    resetHydrationState(runtime);
  }

  workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags & ~DidCapture);
  workInProgress.memoizedState = nullptr;

  if (current == nullptr) {
    return mountActivityChildren(runtime, jsRuntime, workInProgress, offscreenMode, nextChildrenValue, renderLanes);
  }

  return updateActivityChildren(runtime, jsRuntime, workInProgress, current, offscreenMode, nextChildrenValue, renderLanes);
}

FiberNode* deferHiddenOffscreenComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes nextBaseLanes,
  Lanes renderLanes) {
  OffscreenState* state = ensureOffscreenState(workInProgress);
  state->baseLanes = nextBaseLanes;
  state->cachePool = acquireDeferredCache(runtime);

  if (current != nullptr) {
    pushTransition(runtime, workInProgress, CachePoolPtr{}, nullptr);
  }

  reuseHiddenContextOnStack(runtime, workInProgress);
  pushOffscreenSuspenseHandler(workInProgress);

  if (current != nullptr) {
    propagateParentContextChangesToDeferredTree(
      jsRuntime, 
      *current, 
      workInProgress, 
      renderLanes);
  }

  return nullptr;
}

FiberNode* updateOffscreenComponent(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes,
  void* pendingProps) {
  auto* nextProps = static_cast<OffscreenProps*>(pendingProps);
  const OffscreenMode nextMode = nextProps != nullptr ? nextProps->mode : OffscreenMode::Visible;
  facebook::jsi::Value* nextChildrenPointer = nextProps != nullptr ? nextProps->children : nullptr;

  ensureOffscreenInstance(workInProgress);

  OffscreenState* prevState = current != nullptr ? static_cast<OffscreenState*>(current->memoizedState) : nullptr;

  const bool hiddenMode = isHiddenMode(nextMode);
  const bool didSuspend = (workInProgress.flags & DidCapture) != 0;

  if (hiddenMode) {
    if (didSuspend) {
      const Lanes nextBaseLanes = prevState != nullptr ? mergeLanes(prevState->baseLanes, renderLanes) : renderLanes;

      if (current != nullptr) {
        workInProgress.child = current->child;
        Lanes currentChildLanes = NoLanes;
        for (FiberNode* child = workInProgress.child; child != nullptr; child = child->sibling) {
          currentChildLanes = mergeLanes(currentChildLanes, child->lanes);
          currentChildLanes = mergeLanes(currentChildLanes, child->childLanes);
        }
        const Lanes remainingChildLanes = removeLanes(currentChildLanes, nextBaseLanes);
        workInProgress.childLanes = remainingChildLanes;
      } else {
        workInProgress.child = nullptr;
        workInProgress.childLanes = NoLanes;
      }

      return deferHiddenOffscreenComponent(runtime, jsRuntime, current, workInProgress, nextBaseLanes, renderLanes);
    }

    if (!disableLegacyMode && (workInProgress.mode & ConcurrentMode) == NoMode) {
      OffscreenState* nextState = ensureOffscreenState(workInProgress);
      nextState->baseLanes = NoLanes;
      nextState->cachePool.reset();

      if (current != nullptr) {
        pushTransition(runtime, workInProgress, CachePoolPtr{}, nullptr);
      }

      reuseHiddenContextOnStack(runtime, workInProgress);
      pushOffscreenSuspenseHandler(workInProgress);
    } else if (!includesSomeLane(renderLanes, OffscreenLane)) {
      const Lanes offscreenLanes = laneToLanes(OffscreenLane);
      workInProgress.lanes = offscreenLanes;
      workInProgress.childLanes = offscreenLanes;

      const Lanes nextBaseLanes = prevState != nullptr ? mergeLanes(prevState->baseLanes, renderLanes) : renderLanes;
    return deferHiddenOffscreenComponent(runtime, jsRuntime, current, workInProgress, nextBaseLanes, renderLanes);
    } else {
      OffscreenState* nextState = ensureOffscreenState(workInProgress);
      nextState->baseLanes = NoLanes;
      nextState->cachePool = prevState != nullptr ? prevState->cachePool : CachePoolPtr{};

      if (current != nullptr) {
        CachePoolPtr cachePool = prevState != nullptr ? prevState->cachePool : CachePoolPtr{};
        pushTransition(runtime, workInProgress, cachePool, nullptr);
      }

      if (prevState != nullptr) {
        pushHiddenContext(runtime, workInProgress, makeHiddenContextFromState(*prevState));
      } else {
        reuseHiddenContextOnStack(runtime, workInProgress);
      }
      pushOffscreenSuspenseHandler(workInProgress);
    }
  } else {
    if (prevState != nullptr) {
      CachePoolPtr cachePool = prevState->cachePool;
      std::vector<const Transition*>* transitions = nullptr;
      if (enableTransitionTracing) {
        auto* instance = static_cast<OffscreenInstance*>(workInProgress.stateNode);
        if (instance != nullptr) {
          transitions = instance->_transitions;
        }
      }
      pushTransition(runtime, workInProgress, cachePool, transitions);

      pushHiddenContext(runtime, workInProgress, makeHiddenContextFromState(*prevState));
      reuseSuspenseHandlerOnStack(workInProgress);

      workInProgress.memoizedState = nullptr;
    } else {
      if (current != nullptr) {
        pushTransition(runtime, workInProgress, CachePoolPtr{}, nullptr);
      }

      reuseHiddenContextOnStack(runtime, workInProgress);
      reuseSuspenseHandlerOnStack(workInProgress);
    }
  }

  Value nextChildren;
  if (nextChildrenPointer != nullptr) {
    nextChildren = Value(jsRuntime, *nextChildrenPointer);
  }

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateLegacyHiddenComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  return updateOffscreenComponent(runtime, jsRuntime, current, workInProgress, renderLanes, workInProgress.pendingProps);
}

FiberNode* bailoutOffscreenComponent(FiberNode* current, FiberNode& workInProgress) {
  if ((current == nullptr || current->tag != WorkTag::OffscreenComponent) && workInProgress.stateNode == nullptr) {
    ensureOffscreenInstance(workInProgress);
  }

  return workInProgress.sibling;
}

FiberNode* updateCacheComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  prepareToReadContext(workInProgress, renderLanes);

  void* const parentCache = cacheProviderCursor.current;

  auto* prevState = current != nullptr
      ? static_cast<CacheComponentState*>(current->memoizedState)
      : nullptr;
  CacheComponentState* nextState = ensureCacheComponentState(workInProgress, prevState);
  CacheComponentUpdateQueue* queue = nullptr;

  if (current == nullptr) {
    void* nextCache = requestCacheFromPool(runtime, renderLanes);
    if (nextCache == nullptr) {
      nextCache = parentCache;
    }

    nextState->parent = parentCache;
    nextState->cache = nextCache;

    initializeCacheComponentUpdateQueue(workInProgress, *nextState);
    pushCacheProvider(workInProgress, nextCache);
  } else {
    workInProgress.updateQueue = current->updateQueue;
    queue = ensureClonedCacheComponentUpdateQueue(*current, workInProgress);

    if (queue != nullptr) {
      *nextState = queue->baseState;
    }

    if (includesSomeLane(current->lanes, renderLanes)) {
      suspendIfUpdateReadFromEntangledAsyncAction(runtime);
    }

    void* nextCache = prevState != nullptr ? prevState->cache : nullptr;
    if (prevState != nullptr && prevState->parent != parentCache) {
      nextState->parent = parentCache;
      nextCache = parentCache;
    } else {
      nextState->parent = prevState != nullptr ? prevState->parent : parentCache;
      if (nextCache == nullptr) {
        nextCache = parentCache;
      }
    }

    nextState->cache = nextCache;
    pushCacheProvider(workInProgress, nextCache);
    if (queue != nullptr && workInProgress.lanes == NoLanes) {
      updateCacheComponentBaseState(workInProgress, *nextState);
    }
    // TODO: propagate cache refresh context changes once context propagation utilities support void* payloads.
  }

  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);
  Value nextChildren = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(&runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
}

FiberNode* updateTracingMarkerComponent(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode* current,
    FiberNode& workInProgress,
    Lanes renderLanes) {
  if (!enableTransitionTracing) {
    return nullptr;
  }

  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  Value nameValue = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kNamePropName)) {
    nameValue = nextPropsObject.getProperty(jsRuntime, kNamePropName);
  }

  std::optional<std::string> markerName;
  if (!nameValue.isUndefined() && !nameValue.isNull()) {
    markerName = valueToString(jsRuntime, nameValue);
  }

  auto* markerInstance = static_cast<TracingMarkerInstance*>(workInProgress.stateNode);

  if (current == nullptr) {
    const auto& currentTransitions = getWorkInProgressTransitions(runtime);
    if (!currentTransitions.empty()) {
      auto* instance = new TracingMarkerInstance();
      instance->tag = TracingMarkerTag::TransitionTracingMarker;
      instance->transitions.insert(currentTransitions.begin(), currentTransitions.end());
      instance->name = markerName;
      workInProgress.stateNode = instance;
      markerInstance = instance;

      workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Passive);
    } else {
      workInProgress.stateNode = nullptr;
      markerInstance = nullptr;
    }
  } else {
    workInProgress.stateNode = current->stateNode;
    markerInstance = static_cast<TracingMarkerInstance*>(workInProgress.stateNode);
#ifndef NDEBUG
    if (markerInstance != nullptr) {
      if (markerInstance->name.has_value() && markerName.has_value() && markerInstance->name.value() != markerName.value()) {
        std::cerr
            << "Changing the name of a tracing marker after mount is not supported. "
            << "To remount the tracing marker, pass it a new key." << std::endl;
      }
    }
#endif
  }

  Value nextChildren = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  FiberNode* resultingChild = nullptr;

  if (markerInstance != nullptr) {
    pushMarkerInstance(workInProgress, *markerInstance);
  }

  if (current == nullptr) {
    resultingChild = mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  if (resultingChild == nullptr && current != nullptr) {
    FiberNode* currentFirstChild = current->child;
    resultingChild = reconcileChildFibers(
        &runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
  }

  if (markerInstance != nullptr) {
    popMarkerInstance(workInProgress);
  }

  return resultingChild;
}

FiberNode* updateViewTransition(
  ReactRuntime& runtime,
  Runtime& jsRuntime,
  FiberNode* current,
  FiberNode& workInProgress,
  Lanes renderLanes) {

  Value nextPropsValue = cloneJsiValue(jsRuntime, workInProgress.pendingProps);
  Object nextPropsObject = ensureObject(jsRuntime, nextPropsValue);

  Value nameValue = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kNamePropName)) {
    nameValue = nextPropsObject.getProperty(jsRuntime, kNamePropName);
  }

  bool hasExplicitName = false;
  if (!nameValue.isUndefined() && !nameValue.isNull()) {
    if (nameValue.isString()) {
      const std::string nameString = nameValue.getString(jsRuntime).utf8(jsRuntime);
      hasExplicitName = nameString != "auto";
    } else {
      hasExplicitName = true;
    }
  }

  if (hasExplicitName) {
    FiberFlags flagsToSet = ViewTransitionNamedStatic;
    if (current == nullptr) {
      flagsToSet = static_cast<FiberFlags>(flagsToSet | ViewTransitionNamedMount);
    }
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | flagsToSet);
  } else if (getIsHydrating(runtime)) {
    pushMaterializedTreeId(runtime, workInProgress);
  }

  bool nameChanged = false;
  if (current != nullptr) {
    Value prevPropsValue = cloneJsiValue(jsRuntime, current->memoizedProps);
    Object prevPropsObject = ensureObject(jsRuntime, prevPropsValue);
    Value prevNameValue = Value::undefined();
    if (prevPropsObject.hasProperty(jsRuntime, kNamePropName)) {
      prevNameValue = prevPropsObject.getProperty(jsRuntime, kNamePropName);
    }
    nameChanged = !Value::strictEquals(jsRuntime, prevNameValue, nameValue);
  }

  if (nameChanged) {
    workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Ref | RefStatic);
  } else {
    markRef(current, workInProgress);
  }

  Value nextChildren = Value::undefined();
  if (nextPropsObject.hasProperty(jsRuntime, kChildrenPropName)) {
    nextChildren = nextPropsObject.getProperty(jsRuntime, kChildrenPropName);
  }

  if (current == nullptr) {
    return mountChildFibers(&runtime, jsRuntime, workInProgress, nextChildren, renderLanes);
  }

  FiberNode* currentFirstChild = current->child;
  return reconcileChildFibers(
      &runtime, jsRuntime, currentFirstChild, workInProgress, nextChildren, renderLanes);
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

  pushHostRootContext(runtime, workInProgress);
  pushRootTransition(runtime, workInProgress, *fiberRoot, renderLanes);
  if (enableTransitionTracing) {
    pushRootMarkerInstance(workInProgress);
  }

  auto* prevState = current != nullptr ? static_cast<HostRootMemoizedState*>(current->memoizedState) : nullptr;
  auto* nextState = static_cast<HostRootMemoizedState*>(workInProgress.memoizedState);
  auto copyStateFrom = [](const HostRootMemoizedState& source, HostRootMemoizedState& target) {
    if (source.element != nullptr) {
      target.element = std::make_unique<facebook::jsi::Value>(*source.element);
    } else {
      target.element.reset();
    }
    target.isDehydrated = source.isDehydrated;
    target.cache = source.cache;
  };
  if (nextState == nullptr) {
    nextState = new HostRootMemoizedState();
    if (prevState != nullptr) {
      copyStateFrom(*prevState, *nextState);
    }
    workInProgress.memoizedState = nextState;
  } else if (prevState != nullptr && nextState != prevState) {
    copyStateFrom(*prevState, *nextState);
  }

  nextState->isDehydrated = fiberRoot->hostRootState.isDehydrated;

  pushCacheProvider(workInProgress, nextState->cache);

  suspendIfUpdateReadFromEntangledAsyncAction(runtime);

  fiberRoot->hostRootState.isDehydrated = nextState->isDehydrated;

    if (nextState->isDehydrated) {
      void* firstHydratable = hostconfig::getFirstHydratableChildWithinContainer(runtime, fiberRoot->containerInfo);
      if (!enterHydrationState(runtime, workInProgress, firstHydratable)) {
        resetHydrationState(runtime);
      }
    } else {
      resetHydrationState(runtime);
  }

  // Hydration and reconciliation are not yet supported in the translated runtime.
  // The child fiber will be reused as-is until the full beginWork flow is ported.
  return workInProgress.child;
}

void popRootTransition(
    ReactRuntime& runtime,
    FiberNode& workInProgress,
    FiberRoot& root,
    Lanes renderLanes) {
  (void)root;
  (void)renderLanes;
  if (!enableTransitionTracing) {
    return;
  }

  pop(transitionStackCursor, &workInProgress);
}

void popHostContainer(ReactRuntime& runtime, FiberNode& workInProgress) {
  auto& state = getState(runtime);
  pop(state.hostContextCursor, &workInProgress);
  pop(state.hostContextFiberCursor, &workInProgress);
  pop(state.rootHostContainerCursor, &workInProgress);
}

void popTopLevelLegacyContextObject(ReactRuntime& runtime, FiberNode& workInProgress) {
  auto& state = getState(runtime);
  pop(state.legacyContextCursor, &workInProgress);
}

void emitPendingHydrationWarningsInternal(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  if (state.hydrationErrors.empty() && state.pendingRecoverableErrors.empty()) {
    return;
  }

  auto logError = [](const HydrationErrorInfo& info) {
    const std::string key = info.fiber != nullptr ? info.fiber->key : std::string{};
    std::cerr << "[HydrationWarning] Fiber key: " << key << " - " << info.message << std::endl;
  };

  for (const auto& error : state.hydrationErrors) {
    runtime.notifyHydrationError(error);
    logError(error);
  }

  for (const auto& error : state.pendingRecoverableErrors) {
    runtime.notifyHydrationError(error);
    logError(error);
  }
}

void upgradeHydrationErrorsToRecoverable(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  if (state.hydrationErrors.empty()) {
    return;
  }

  auto& pending = state.pendingRecoverableErrors;
  pending.insert(
      pending.end(),
      std::make_move_iterator(state.hydrationErrors.begin()),
      std::make_move_iterator(state.hydrationErrors.end()));

  state.hydrationErrors.clear();
}

void popCacheProvider(FiberNode& workInProgress, void* cache) {
  (void)cache;
  pop(cacheProviderCursor, &workInProgress);
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

void resetSuspendedWorkLoopOnUnwind(ReactRuntime& runtime, FiberNode& fiber) {
  resetContextDependencies();
  resetHooksOnUnwind(runtime, fiber);
  resetChildReconcilerOnUnwind();
  clearPendingRenderPhaseUpdates(runtime);
  setPendingDidIncludeRenderPhaseUpdate(runtime, false);
}

void unwindInterruptedWork(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode* workInProgress,
    Lanes renderLanes) {
  if (workInProgress == nullptr) {
    return;
  }

  popTreeContext(runtime, *workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* const root = static_cast<FiberRoot*>(workInProgress->stateNode);
      auto* const rootState = static_cast<HostRootMemoizedState*>(workInProgress->memoizedState);
      void* cache = rootState != nullptr ? rootState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);

      if (enableTransitionTracing) {
        popRootMarkerInstance(*workInProgress);
      }

      if (root != nullptr) {
        popRootTransition(runtime, *workInProgress, *root, renderLanes);
        popHostContainer(runtime, *workInProgress);
      }

      popTopLevelLegacyContextObject(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostComponent:
    case WorkTag::HostHoistable:
    case WorkTag::HostSingleton: {
      popHostContext(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostPortal: {
      popHostContainer(runtime, *workInProgress);
      break;
    }
    case WorkTag::SuspenseComponent: {
      popSuspenseHandler(*workInProgress);
      auto* suspenseState = static_cast<SuspenseState*>(workInProgress->memoizedState);
      if (suspenseState != nullptr && suspenseState->dehydrated != nullptr) {
        resetHydrationState(runtime);
      }
      break;
    }
    case WorkTag::ActivityComponent: {
      if (workInProgress->memoizedState != nullptr) {
        popSuspenseHandler(*workInProgress);
        resetHydrationState(runtime);
      }
      break;
    }
    case WorkTag::SuspenseListComponent: {
      popSuspenseListContext(*workInProgress);
      break;
    }
    case WorkTag::OffscreenComponent:
    case WorkTag::LegacyHiddenComponent: {
      popSuspenseHandler(*workInProgress);
      popHiddenContext(runtime, *workInProgress);
      popTransition(*workInProgress, current);
      break;
    }
    case WorkTag::CacheComponent: {
      auto* cacheState = static_cast<CacheComponentState*>(workInProgress->memoizedState);
      void* cache = cacheState != nullptr ? cacheState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);
      break;
    }
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing && workInProgress->stateNode != nullptr) {
        popMarkerInstance(*workInProgress);
      }
      break;
    }
    default:
      break;
  }
}

FiberNode* unwindWork(
    ReactRuntime& runtime,
    FiberNode* current,
    FiberNode* workInProgress,
    Lanes entangledRenderLanes) {
  (void)entangledRenderLanes;

  if (workInProgress == nullptr) {
    return nullptr;
  }

  resetSuspendedWorkLoopOnUnwind(runtime, *workInProgress);
  popTreeContext(runtime, *workInProgress);

  switch (workInProgress->tag) {
    case WorkTag::HostRoot: {
      auto* const root = static_cast<FiberRoot*>(workInProgress->stateNode);
      auto* const rootState = static_cast<HostRootMemoizedState*>(workInProgress->memoizedState);
      void* cache = rootState != nullptr ? rootState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);

      if (enableTransitionTracing) {
        popRootMarkerInstance(*workInProgress);
      }

      if (root != nullptr) {
        popRootTransition(runtime, *workInProgress, *root, getWorkInProgressRootRenderLanes(runtime));
        popHostContainer(runtime, *workInProgress);
      }

      popTopLevelLegacyContextObject(runtime, *workInProgress);

      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    case WorkTag::HostComponent:
    case WorkTag::HostHoistable:
    case WorkTag::HostSingleton: {
      popHostContext(runtime, *workInProgress);
      break;
    }
    case WorkTag::HostPortal: {
      popHostContainer(runtime, *workInProgress);
      break;
    }
    case WorkTag::SuspenseComponent: {
      popSuspenseHandler(*workInProgress);
      auto* suspenseState = static_cast<SuspenseState*>(workInProgress->memoizedState);
      if (suspenseState != nullptr && suspenseState->dehydrated != nullptr) {
        resetHydrationState(runtime);
      }

      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    case WorkTag::SuspenseListComponent: {
      popSuspenseListContext(*workInProgress);
      break;
    }
    case WorkTag::OffscreenComponent:
    case WorkTag::LegacyHiddenComponent: {
      popSuspenseHandler(*workInProgress);
      popHiddenContext(runtime, *workInProgress);
      popTransition(*workInProgress, current);
      break;
    }
    case WorkTag::CacheComponent: {
      auto* cacheState = static_cast<CacheComponentState*>(workInProgress->memoizedState);
      void* cache = cacheState != nullptr ? cacheState->cache : nullptr;
      popCacheProvider(*workInProgress, cache);
      break;
    }
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing && workInProgress->stateNode != nullptr) {
        popMarkerInstance(*workInProgress);
      }
      break;
    }
    case WorkTag::ClassComponent: {
      const FiberFlags flags = workInProgress->flags;
      if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
        workInProgress->flags = static_cast<FiberFlags>((flags & ~ShouldCapture) | DidCapture);
        return workInProgress;
      }
      break;
    }
    default:
      break;
  }

  workInProgress->flags = static_cast<FiberFlags>(workInProgress->flags | Incomplete);
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->childLanes = NoLanes;
  workInProgress->deletions.clear();

  return workInProgress->returnFiber;
}

void startProfilerTimer(FiberNode&) {
  // TODO: integrate ReactProfilerTimer when available.
}

void stopProfilerTimerIfRunning(FiberNode&) {
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

  if (oldProps != newProps || hasLegacyContextChanged(runtime)) {
      didReceiveUpdate = true;
    } else {
      const bool hasScheduledUpdateOrContext = checkScheduledUpdateOrContext(*current, renderLanes);
      if (!hasScheduledUpdateOrContext && (workInProgress->flags & DidCapture) == 0) {
        setDidReceiveUpdate(runtime, false);
  return attemptEarlyBailoutIfNoScheduledUpdate(runtime, jsRuntime, current, *workInProgress, renderLanes);
      }

      if ((current->flags & ForceUpdateForLegacySuspense) != 0) {
        didReceiveUpdate = true;
      }
    }

  } else {
    workInProgress->childLanes = NoLanes;

  if (getIsHydrating(runtime) && isForkedChild(*workInProgress)) {
  handleForkedChildDuringHydration(runtime, *workInProgress);
    }
  }

  setDidReceiveUpdate(runtime, didReceiveUpdate);

  workInProgress->lanes = NoLanes;

  switch (workInProgress->tag) {
    case WorkTag::LazyComponent: {
    return mountLazyComponent(
      runtime,
      jsRuntime,
      current,
      *workInProgress,
      const_cast<void*>(workInProgress->elementType),
      renderLanes);
    }
    case WorkTag::FunctionComponent: {
      return updateFunctionComponent(
        runtime,
        jsRuntime,
        current,
        *workInProgress,
        const_cast<void*>(workInProgress->type),
        workInProgress->pendingProps,
        renderLanes);
    }
    case WorkTag::ClassComponent: {
      return updateClassComponent(
        runtime,
        jsRuntime,
        current,
        *workInProgress,
        const_cast<void*>(workInProgress->type),
        workInProgress->pendingProps,
        renderLanes);
    }
    case WorkTag::HostRoot:
      return updateHostRoot(runtime, current, *workInProgress, renderLanes);
    case WorkTag::HostHoistable:
      return updateHostHoistable(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::HostSingleton:
      return updateHostSingleton(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::HostComponent:
      return updateHostComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::HostText:
  return updateHostText(runtime, jsRuntime, current, *workInProgress);
    case WorkTag::SuspenseComponent:
      return updateSuspenseComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::HostPortal:
      return updatePortalComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ForwardRef:
      return updateForwardRef(
          runtime,
          jsRuntime,
          current,
          *workInProgress,
          const_cast<void*>(workInProgress->type),
          workInProgress->pendingProps,
          renderLanes);
    case WorkTag::Fragment:
      return updateFragment(jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::Mode:
      return updateMode(jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::Profiler:
      return updateProfiler(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ContextProvider:
      return updateContextProvider(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ContextConsumer:
      return updateContextConsumer(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::MemoComponent:
      return updateMemoComponent(
        runtime,
        jsRuntime,
        current,
        *workInProgress,
        const_cast<void*>(workInProgress->type),
        workInProgress->pendingProps,
        renderLanes);
    case WorkTag::SimpleMemoComponent:
      return updateSimpleMemoComponent(
        runtime,
        jsRuntime,
        current,
        *workInProgress,
        const_cast<void*>(workInProgress->type),
        workInProgress->pendingProps,
        renderLanes);
    case WorkTag::IncompleteClassComponent: {
      if (disableLegacyMode) {
        break;
      }
      return mountIncompleteClassComponent(
        runtime,
        jsRuntime,
        current,
        *workInProgress,
        const_cast<void*>(workInProgress->type),
        workInProgress->pendingProps,
        renderLanes);
    }
    case WorkTag::IncompleteFunctionComponent: {
      if (disableLegacyMode) {
        break;
      }
      return mountIncompleteFunctionComponent(
          runtime,
          jsRuntime,
          current,
          *workInProgress,
          const_cast<void*>(workInProgress->type),
          workInProgress->pendingProps,
          renderLanes);
    }
    case WorkTag::SuspenseListComponent:
      return updateSuspenseListComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::ScopeComponent: {
      if (enableScopeAPI) {
        return updateScopeComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::ActivityComponent:
      return updateActivityComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::OffscreenComponent:
      return updateOffscreenComponent(runtime, jsRuntime, current, *workInProgress, renderLanes, workInProgress->pendingProps);
    case WorkTag::LegacyHiddenComponent: {
      if (enableLegacyHidden) {
        return updateLegacyHiddenComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::CacheComponent:
      return updateCacheComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
    case WorkTag::TracingMarkerComponent: {
      if (enableTransitionTracing) {
        return updateTracingMarkerComponent(runtime, jsRuntime, current, *workInProgress, renderLanes);
      }
      break;
    }
    case WorkTag::ViewTransitionComponent: {
      if (enableViewTransition) {
        return updateViewTransition(runtime, jsRuntime, current, *workInProgress, renderLanes);
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

bool flushPendingEffectsImpl(ReactRuntime& runtime, Runtime& jsRuntime, bool includeRenderPhaseUpdates) {
  auto& state = getState(runtime);

  if (includeRenderPhaseUpdates) {
    PendingRenderPhaseUpdateNode* renderPhaseNode = state.pendingRenderPhaseUpdates;
    state.pendingRenderPhaseUpdates = nullptr;

    while (renderPhaseNode != nullptr) {
      PendingRenderPhaseUpdateNode* nextNode = renderPhaseNode->next;
      if (renderPhaseNode->fiber != nullptr) {
        performUnitOfWork(runtime, jsRuntime, *renderPhaseNode->fiber);
      }
      delete renderPhaseNode;
      renderPhaseNode = nextNode;
    }

    state.pendingDidIncludeRenderPhaseUpdate = false;
  }

  if (state.pendingEffectsStatus != PendingEffectsStatus::Passive) {
    clearPendingPassiveEffects(runtime);
    return false;
  }

  state.pendingEffectsStatus = PendingEffectsStatus::None;
  state.pendingEffectsRoot = nullptr;
  state.pendingFinishedWork = nullptr;
  state.pendingEffectsLanes = NoLanes;
  state.pendingEffectsRemainingLanes = NoLanes;
  state.pendingEffectsRenderEndTime = -0.0;
  state.pendingViewTransition = nullptr;
  state.pendingViewTransitionEvents.clear();
  state.pendingTransitionTypes = nullptr;
  state.pendingPassiveTransitions.clear();
  state.pendingRecoverableErrors.clear();
  state.pendingSuspendedCommitReason = SuspendedCommitReason::ImmediateCommit;

  if (state.pendingPassiveEffects.empty()) {
    return false;
  }

  std::vector<FiberNode*> effects;
  effects.swap(state.pendingPassiveEffects);

  state.isFlushingPassiveEffects = true;
  state.didScheduleUpdateDuringPassiveEffects = false;

  for (FiberNode* fiber : effects) {
    if (fiber != nullptr) {
      commitHookEffects(runtime, jsRuntime, *fiber);
    }
  }

  state.isFlushingPassiveEffects = false;
  return true;
}

} // namespace

void emitPendingHydrationWarnings(ReactRuntime& runtime) {
  emitPendingHydrationWarningsInternal(runtime);
}

bool flushPendingEffects(ReactRuntime& runtime, Runtime& jsRuntime, bool includeRenderPhaseUpdates) {
  return flushPendingEffectsImpl(runtime, jsRuntime, includeRenderPhaseUpdates);
}

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
  (void)jsRuntime;
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

Lane requestUpdateLane(ReactRuntime& runtime, FiberNode& fiber) {
  const auto mode = fiber.mode;
  if (!disableLegacyMode && (mode & ConcurrentMode) == NoMode) {
    return SyncLane;
  }

  const ExecutionContext context = getExecutionContext(runtime);
  if ((context & RenderContext) != NoContext) {
    const Lanes renderLanes = getWorkInProgressRootRenderLanes(runtime);
    if (renderLanes != NoLanes) {
      return getHighestPriorityLane(renderLanes);
    }
  }

  return DefaultLane;
}

Lane requestRetryLane(ReactRuntime& runtime, FiberNode& fiber) {
  const auto mode = fiber.mode;
  if (!disableLegacyMode && (mode & ConcurrentMode) == NoMode) {
    return SyncLane;
  }
  return claimNextRetryLane();
}

Lane requestDeferredLane(ReactRuntime& runtime) {
  Lane lane = getWorkInProgressDeferredLane(runtime);
  if (lane == NoLane) {
    lane = DefaultLane;
    setWorkInProgressDeferredLane(runtime, lane);
  }
  return lane;
}

Lane peekDeferredLane(ReactRuntime& runtime) {
  return getWorkInProgressDeferredLane(runtime);
}

void scheduleUpdateOnFiber(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberRoot& root,
    FiberNode& fiber,
    Lane lane) {
#if REACTCPP_ENABLE_DEV
  if (getIsRunningInsertionEffect(runtime)) {
    std::cerr << "useInsertionEffect must not schedule updates." << std::endl;
  }
  if (getIsFlushingPassiveEffects(runtime)) {
    setDidScheduleUpdateDuringPassiveEffects(runtime, true);
  }
#endif

  FiberRoot* const workInProgressRoot = getWorkInProgressRoot(runtime);
  const bool isRenderingSuspended =
      workInProgressRoot == &root && isWorkLoopSuspendedOnData(runtime);
  const bool hasPendingCommitCancellation =
      static_cast<bool>(root.cancelPendingCommit);

  if (isRenderingSuspended || hasPendingCommitCancellation) {
    prepareFreshStack(runtime, root, NoLanes);
    constexpr bool didAttemptEntireTree = false;
    markRootSuspended(
        root,
        getWorkInProgressRootRenderLanes(runtime),
        getWorkInProgressDeferredLane(runtime),
        didAttemptEntireTree);
  }

  markRootUpdated(root, lane);

  const ExecutionContext executionContext = getExecutionContext(runtime);
  if ((executionContext & RenderContext) != NoContext &&
      workInProgressRoot == &root) {
#if REACTCPP_ENABLE_DEV
  warnAboutRenderPhaseUpdatesInDEV(runtime, jsRuntime, fiber);
#endif
    const Lanes updated = mergeLanes(
        getWorkInProgressRootRenderPhaseUpdatedLanes(runtime),
        lane);
    setWorkInProgressRootRenderPhaseUpdatedLanes(runtime, updated);
    return;
  }

  addFiberToLanesMap(root, &fiber, lane);

#if REACTCPP_ENABLE_DEV
  warnIfUpdatesNotWrappedWithActDEV(runtime, jsRuntime, fiber);
#endif

  if (enableTransitionTracing) {
    if (const Transition* transition = getCurrentTransition(runtime, jsRuntime)) {
      if (!transition->name.empty()) {
        addTransitionToLanesMap(root, transition, lane);
      }
    }
  }

  if (workInProgressRoot == &root) {
    if ((executionContext & RenderContext) == NoContext) {
      const Lanes updated = mergeLanes(
          getWorkInProgressRootInterleavedUpdatedLanes(runtime),
          lane);
      setWorkInProgressRootInterleavedUpdatedLanes(runtime, updated);
    }

    if (getWorkInProgressRootExitStatus(runtime) ==
        RootExitStatus::SuspendedWithDelay) {
      constexpr bool didAttemptEntireTree = false;
      markRootSuspended(
          root,
          getWorkInProgressRootRenderLanes(runtime),
          getWorkInProgressDeferredLane(runtime),
          didAttemptEntireTree);
    }
  }

  ensureRootIsScheduled(runtime, jsRuntime, root);

  if (!disableLegacyMode &&
      lane == SyncLane &&
      (fiber.mode & ConcurrentMode) == NoMode &&
      (executionContext & (RenderContext | CommitContext)) == NoContext) {
#if REACTCPP_ENABLE_DEV
    if (isLegacyBatchingActive(jsRuntime)) {
      return;
    }
#endif
    resetRenderTimer(runtime);
    flushSyncWorkOnLegacyRootsOnly(runtime, jsRuntime);
  }
}

void scheduleInitialHydrationOnRoot(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberRoot& root,
    Lane lane) {
  FiberNode* const current = root.current;
  if (current != nullptr) {
    current->lanes = mergeLanes(current->lanes, lane);
  }

  markRootUpdated(root, lane);
  ensureRootIsScheduled(runtime, jsRuntime, root);
}

void flushRoot(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberRoot& root,
    Lanes lanes) {
  if (lanes == NoLanes) {
    return;
  }

  upgradePendingLanesToSync(root, lanes);
  ensureRootIsScheduled(runtime, jsRuntime, root);

  if ((getExecutionContext(runtime) & (RenderContext | CommitContext)) == NoContext) {
    resetRenderTimer(runtime);
    flushSyncWorkOnAllRoots(runtime, jsRuntime, lanes);
  }
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

PendingRenderPhaseUpdateNode* getPendingRenderPhaseUpdates(ReactRuntime& runtime) {
  return getState(runtime).pendingRenderPhaseUpdates;
}

void enqueuePendingRenderPhaseUpdate(ReactRuntime& runtime, FiberNode* fiber) {
  auto& state = getState(runtime);
  auto* node = new PendingRenderPhaseUpdateNode();
  node->fiber = fiber;
  node->next = nullptr;

  if (state.pendingRenderPhaseUpdates == nullptr) {
    state.pendingRenderPhaseUpdates = node;
    return;
  }

  PendingRenderPhaseUpdateNode* tail = state.pendingRenderPhaseUpdates;
  while (tail->next != nullptr) {
    tail = tail->next;
  }
  tail->next = node;
}

void clearPendingRenderPhaseUpdates(ReactRuntime& runtime) {
  auto& state = getState(runtime);
  PendingRenderPhaseUpdateNode* node = state.pendingRenderPhaseUpdates;
  while (node != nullptr) {
    PendingRenderPhaseUpdateNode* next = node->next;
    delete node;
    node = next;
  }
  state.pendingRenderPhaseUpdates = nullptr;
}

std::vector<FiberNode*>& getPendingPassiveEffects(ReactRuntime& runtime) {
  return getState(runtime).pendingPassiveEffects;
}

void enqueuePendingPassiveEffect(ReactRuntime& runtime, FiberNode& fiber) {
  getState(runtime).pendingPassiveEffects.push_back(&fiber);
}

void clearPendingPassiveEffects(ReactRuntime& runtime) {
  getState(runtime).pendingPassiveEffects.clear();
}

std::vector<HydrationErrorInfo>& getPendingRecoverableErrors(ReactRuntime& runtime) {
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
  resetSuspendedWorkLoopOnUnwind(runtime, unitOfWork);

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

  if (getIsHydrating(runtime) || reason == SuspendedReason::SuspendedOnError) {
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
  FiberNode* next = unwindWork(runtime, current, incompleteWork, state.entangledRenderLanes);

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
    resetSuspendedWorkLoopOnUnwind(runtime, *workInProgress);
    interruptedWork = workInProgress;
  }

  const Lanes renderLanes = getWorkInProgressRootRenderLanes(runtime);
  while (interruptedWork != nullptr) {
    FiberNode* current = interruptedWork->alternate;

    unwindInterruptedWork(runtime, current, interruptedWork, renderLanes);

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

std::vector<HydrationErrorInfo>& getWorkInProgressRootRecoverableErrors(ReactRuntime& runtime) {
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
