#include "ReactReconciler/ReactFiberWorkLoop.h"

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
#include "ReactFiberLegacyContext.h"
#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactDOM/client/ReactDOMInstance.h"
#include "ReactReconciler/ReactFiberStack.h"
#include "ReactReconciler/ReactFiberHooks.h"
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
#include "shared/ReactSymbols.h"

#include "jsi/jsi.h"

#include <array>
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

struct HostRootMemoizedState {
  void* element{nullptr};
  bool isDehydrated{false};
  void* cache{nullptr};
};

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
    &runtime, jsRuntime, currentPrimaryChild, *primaryChildFragment, *hiddenProps->children, renderLanes);

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
 