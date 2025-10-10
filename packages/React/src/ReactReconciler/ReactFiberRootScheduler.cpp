#include "ReactReconciler/ReactFiberRootScheduler.h"

#include "ReactReconciler/ReactFiberAsyncAction.h"
#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberCommitEffects.h"
#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactEventPriorities.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactProfilerTimer.h"
#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactFeatureFlags.h"
#include "shared/ReactSharedInternals.h"
#include "jsi/jsi.h"
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <utility>

namespace react {

bool performWorkOnRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes,
  bool forceSync);
bool performSyncWorkOnRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes);

namespace {

RootSchedulerState& getState(ReactRuntime& runtime) {
  return runtime.rootSchedulerState();
}

const RootSchedulerState& getState(const ReactRuntime& runtime) {
  return runtime.rootSchedulerState();
}

struct SchedulerCallbackResult {
  bool hasContinuation{false};
  std::function<SchedulerCallbackResult(facebook::jsi::Runtime&, bool)> continuation{};
};

using MicrotaskCallback = std::function<void(facebook::jsi::Runtime&)>;
using SchedulerCallback = std::function<SchedulerCallbackResult(facebook::jsi::Runtime&, bool)>;

constexpr std::uint64_t kActCallbackBit = 1ull << 63;

const std::function<void()>& noopIndicatorCallback() {
  static const std::function<void()> noop = []() {};
  return noop;
}

std::uint64_t toActCallbackKey(const TaskHandle& handle) {
  return handle.id & ~kActCallbackBit;
}

TaskHandle makeActCallbackHandle(RootSchedulerState& state) {
  const std::uint64_t key = state.nextActCallbackId++;
  return TaskHandle{kActCallbackBit | key};
}

bool isActCallbackHandle(const TaskHandle& handle) {
  return (handle.id & kActCallbackBit) != 0;
}

bool pushActQueueCallback(facebook::jsi::Runtime& jsRuntime, facebook::jsi::Function&& callback) {
  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    if (!hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kActQueue)) {
      return false;
    }

    facebook::jsi::Value queueValue = getReactSharedInternalsProperty(
        jsRuntime, internals, ReactSharedInternalsKeys::kActQueue);
    if (queueValue.isNull() || queueValue.isUndefined() || !queueValue.isObject()) {
      return false;
    }

    facebook::jsi::Object queueObject = queueValue.getObject(jsRuntime);
    if (!queueObject.isArray(jsRuntime)) {
      return false;
    }

    facebook::jsi::Array queueArray = queueObject.asArray(jsRuntime);
    const size_t length = queueArray.size(jsRuntime);
    queueArray.setValueAtIndex(jsRuntime, length, std::move(callback));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool hasActiveActQueue(facebook::jsi::Runtime& jsRuntime) {
  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    if (!hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kActQueue)) {
      return false;
    }

    facebook::jsi::Value queueValue = getReactSharedInternalsProperty(
        jsRuntime, internals, ReactSharedInternalsKeys::kActQueue);
    if (queueValue.isNull() || queueValue.isUndefined()) {
      return false;
    }

    return queueValue.isObject();
  } catch (const std::exception&) {
    return false;
  }
}

bool removeActQueueCallback(facebook::jsi::Runtime& jsRuntime, const facebook::jsi::Function& callback) {
  try {
    facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    if (!hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kActQueue)) {
      return false;
    }

    facebook::jsi::Value queueValue = getReactSharedInternalsProperty(
        jsRuntime, internals, ReactSharedInternalsKeys::kActQueue);
    if (!queueValue.isObject()) {
      return false;
    }

    facebook::jsi::Object queueObject = queueValue.getObject(jsRuntime);
    if (!queueObject.isArray(jsRuntime)) {
      return false;
    }

    facebook::jsi::Array queueArray = queueObject.asArray(jsRuntime);
    const size_t length = queueArray.size(jsRuntime);
    if (length == 0) {
      return false;
    }

    facebook::jsi::Value callbackValue(jsRuntime, callback);
    for (size_t index = 0; index < length; ++index) {
      facebook::jsi::Value entry = queueArray.getValueAtIndex(jsRuntime, index);
      if (!entry.isObject()) {
        continue;
      }

  if (facebook::jsi::Value::strictEquals(jsRuntime, entry, callbackValue)) {
        if (index + 1 < length) {
          facebook::jsi::Value last = queueArray.getValueAtIndex(jsRuntime, length - 1);
          queueArray.setValueAtIndex(jsRuntime, index, last);
        }

        queueArray.setValueAtIndex(jsRuntime, length - 1, facebook::jsi::Value::undefined());
        queueObject.setProperty(jsRuntime, "length", facebook::jsi::Value(static_cast<double>(length - 1)));
        return true;
      }
    }
  } catch (const std::exception&) {
    return false;
  }

  return false;
}

void processRootSchedule(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void processRootScheduleInMicrotask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void processRootScheduleInImmediateTask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);

bool detectMicrotaskSupport(facebook::jsi::Runtime& jsRuntime) {
  try {
    facebook::jsi::Object global = jsRuntime.global();
    if (!global.hasProperty(jsRuntime, "queueMicrotask")) {
      return false;
    }

    facebook::jsi::Value queueValue = global.getProperty(jsRuntime, "queueMicrotask");
    if (!queueValue.isObject()) {
      return false;
    }

    facebook::jsi::Object queueObject = queueValue.getObject(jsRuntime);
    if (!queueObject.isFunction(jsRuntime)) {
      return false;
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool tryQueueMicrotask(facebook::jsi::Runtime& jsRuntime, MicrotaskCallback callback) {
  try {
    facebook::jsi::Object global = jsRuntime.global();
    if (!global.hasProperty(jsRuntime, "queueMicrotask")) {
      return false;
    }

    facebook::jsi::Value queueValue = global.getProperty(jsRuntime, "queueMicrotask");
    if (!queueValue.isObject()) {
      return false;
    }

    facebook::jsi::Object queueObject = queueValue.getObject(jsRuntime);
    if (!queueObject.isFunction(jsRuntime)) {
      return false;
    }

    facebook::jsi::Function queueFunction = queueObject.asFunction(jsRuntime);
    auto callbackPtr = std::make_shared<MicrotaskCallback>(std::move(callback));
    facebook::jsi::Function hostFunction = facebook::jsi::Function::createFromHostFunction(
        jsRuntime,
        facebook::jsi::PropNameID::forAscii(jsRuntime, "__reactScheduleRootMicrotask"),
        0,
        [callbackPtr](facebook::jsi::Runtime& runtime,
                      const facebook::jsi::Value&,
                      const facebook::jsi::Value*,
                      size_t) -> facebook::jsi::Value {
          if (callbackPtr && *callbackPtr) {
            (*callbackPtr)(runtime);
          }
          return facebook::jsi::Value::undefined();
        });

    queueFunction.call(jsRuntime, hostFunction);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void enqueueActMicrotask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  auto runtimePtr = &runtime;
  auto callbackPtr = std::make_shared<MicrotaskCallback>(
      [runtimePtr](facebook::jsi::Runtime& taskRuntime) {
        processRootScheduleInMicrotask(*runtimePtr, taskRuntime);
      });
  facebook::jsi::Function hostFunction = facebook::jsi::Function::createFromHostFunction(
      jsRuntime,
      facebook::jsi::PropNameID::forAscii(jsRuntime, "__reactActMicrotask"),
      0,
      [callbackPtr](facebook::jsi::Runtime& runtime,
                    const facebook::jsi::Value&,
                    const facebook::jsi::Value*,
                    size_t) -> facebook::jsi::Value {
        if (callbackPtr && *callbackPtr) {
          (*callbackPtr)(runtime);
        }
        return facebook::jsi::Value::null();
      });

  pushActQueueCallback(jsRuntime, std::move(hostFunction));
}

facebook::jsi::Function createActSchedulerTaskFunction(
  facebook::jsi::Runtime& jsRuntime,
  std::shared_ptr<SchedulerCallback> callbackPtr,
  RootSchedulerState& state,
  std::uint64_t actKey) {
  return facebook::jsi::Function::createFromHostFunction(
      jsRuntime,
      facebook::jsi::PropNameID::forAscii(jsRuntime, "__reactActSchedulerTask"),
      1,
      [callbackPtr = std::move(callbackPtr), &state, actKey](
          facebook::jsi::Runtime& runtime,
          const facebook::jsi::Value& thisValue,
          const facebook::jsi::Value* arguments,
          size_t count) -> facebook::jsi::Value {
        (void)thisValue;
        bool didTimeout = false;
        if (count > 0 && arguments[0].isBool()) {
          didTimeout = arguments[0].getBool();
        }

        SchedulerCallbackResult result{};
        if (callbackPtr && *callbackPtr) {
          result = (*callbackPtr)(runtime, didTimeout);
        }

        if (!result.hasContinuation || !result.continuation) {
          state.actCallbacks.erase(actKey);
          return facebook::jsi::Value::null();
        }

        auto continuationPtr = std::make_shared<SchedulerCallback>(result.continuation);
        facebook::jsi::Function continuationFunction = createActSchedulerTaskFunction(
            runtime,
            continuationPtr,
            state,
            actKey);
        auto storedContinuation = std::make_shared<facebook::jsi::Value>(
            facebook::jsi::Value(runtime, continuationFunction));
        state.actCallbacks[actKey] = storedContinuation;
        return facebook::jsi::Value(runtime, continuationFunction);
      });
}

TaskHandle scheduleCallback(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  SchedulerPriority priority,
  SchedulerCallback callback) {
  RootSchedulerState& state = getState(runtime);
  auto callbackPtr = std::make_shared<SchedulerCallback>(std::move(callback));

  const std::uint64_t actKey = state.nextActCallbackId;
  facebook::jsi::Function hostFunction = createActSchedulerTaskFunction(
      jsRuntime,
      callbackPtr,
      state,
      actKey);

  auto storedCallback = std::make_shared<facebook::jsi::Value>(facebook::jsi::Value(jsRuntime, hostFunction));

  if (pushActQueueCallback(jsRuntime, std::move(hostFunction))) {
    state.actCallbacks.emplace(actKey, std::move(storedCallback));
    return makeActCallbackHandle(state);
  }

  facebook::jsi::Runtime* capturedRuntime = &jsRuntime;
  return runtime.scheduleTask(priority, [callbackPtr, capturedRuntime]() {
    if (!callbackPtr || !*callbackPtr) {
      return;
    }

    SchedulerCallback current = *callbackPtr;
    SchedulerCallbackResult result = current(*capturedRuntime, false);
    while (result.hasContinuation && result.continuation) {
      current = result.continuation;
      result = current(*capturedRuntime, false);
    }
  });
}

void cancelCallback(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, TaskHandle handle) {
  if (!handle) {
    return;
  }

  if (isActCallbackHandle(handle)) {
    RootSchedulerState& state = getState(runtime);
    const std::uint64_t key = toActCallbackKey(handle);
    auto it = state.actCallbacks.find(key);
    if (it != state.actCallbacks.end()) {
      if (it->second && it->second->isObject()) {
        facebook::jsi::Object callbackObject = it->second->getObject(jsRuntime);
        if (callbackObject.isFunction(jsRuntime)) {
          facebook::jsi::Function storedFunction = callbackObject.asFunction(jsRuntime);
          removeActQueueCallback(jsRuntime, storedFunction);
        }
      }
      state.actCallbacks.erase(it);
    }
    return;
  }

  runtime.cancelTask(handle);
}

void scheduleImmediateTaskFallback(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  ReactRuntime* runtimePtr = &runtime;
  facebook::jsi::Runtime* capturedRuntime = &jsRuntime;
  runtime.scheduleTask(SchedulerPriority::ImmediatePriority, [runtimePtr, capturedRuntime]() {
    processRootScheduleInImmediateTask(*runtimePtr, *capturedRuntime);
  });
}

bool tryScheduleRootMicrotask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);
  if (!state.supportsMicrotasksCache.has_value()) {
    state.supportsMicrotasksCache = detectMicrotaskSupport(jsRuntime);
  }

  if (!state.supportsMicrotasksCache.value()) {
    return false;
  }

  ReactRuntime* runtimePtr = &runtime;
  const bool scheduled = tryQueueMicrotask(jsRuntime, [runtimePtr](facebook::jsi::Runtime& taskRuntime) {
    const ExecutionContext executionContext = getExecutionContext(*runtimePtr);
    if ((executionContext & (RenderContext | CommitContext)) != NoContext) {
      scheduleImmediateTaskFallback(*runtimePtr, taskRuntime);
      return;
    }

    processRootScheduleInMicrotask(*runtimePtr, taskRuntime);
  });

  if (!scheduled) {
    state.supportsMicrotasksCache = false;
  }

  return scheduled;
}

void ensureScheduleProcessing(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void ensureScheduleIsScheduledInternal(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void scheduleImmediateRootScheduleTask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void trackSchedulerEvent(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
SchedulerCallbackResult performWorkOnRootViaSchedulerTask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  TaskHandle originalCallbackHandle,
  bool didTimeout);
void flushSyncWorkAcrossRoots(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, Lanes syncTransitionLanes, bool onlyLegacy);
Lanes scheduleTaskForRootDuringMicrotask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root, int currentTime);
void startDefaultTransitionIndicatorIfNeeded(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);
void cleanupDefaultTransitionIndicatorIfNeeded(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root);

void reportDefaultIndicatorError(const std::exception& ex) {
  std::cerr << "React default transition indicator threw: " << ex.what() << std::endl;
}

void reportDefaultIndicatorUnknownError() {
  std::cerr << "React default transition indicator threw an unknown exception" << std::endl;
}

void startDefaultTransitionIndicatorIfNeeded(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  (void)jsRuntime;
  if (!enableDefaultTransitionIndicator) {
    return;
  }

  startIsomorphicDefaultIndicatorIfNeeded(runtime);

  RootSchedulerState& state = getState(runtime);
  for (FiberRoot* root = state.firstScheduledRoot; root != nullptr; root = root->next) {
    if (root->indicatorLanes == NoLanes || root->pendingIndicator) {
      continue;
    }

    if (hasOngoingIsomorphicIndicator(runtime)) {
      root->pendingIndicator = retainIsomorphicIndicator(runtime);
      continue;
    }

    const auto& onIndicator = root->onDefaultTransitionIndicator;
    if (!onIndicator) {
      root->pendingIndicator = noopIndicatorCallback();
      continue;
    }

    try {
      auto cleanup = onIndicator();
      if (cleanup) {
        root->pendingIndicator = std::move(cleanup);
      } else {
        root->pendingIndicator = noopIndicatorCallback();
      }
    } catch (const std::exception& ex) {
      root->pendingIndicator = noopIndicatorCallback();
      reportDefaultIndicatorError(ex);
    } catch (...) {
      root->pendingIndicator = noopIndicatorCallback();
      reportDefaultIndicatorUnknownError();
    }
  }
}

void cleanupDefaultTransitionIndicatorIfNeeded(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root) {
  (void)jsRuntime;
  if (!enableDefaultTransitionIndicator) {
    return;
  }

  if (!root.pendingIndicator) {
    return;
  }

  if (root.indicatorLanes != NoLanes) {
    return;
  }

  auto cleanup = std::move(root.pendingIndicator);
  root.pendingIndicator = {};

  try {
    cleanup();
  } catch (const std::exception& ex) {
    reportDefaultIndicatorError(ex);
  } catch (...) {
    reportDefaultIndicatorUnknownError();
  }
}

void addRootToSchedule(ReactRuntime& runtime, FiberRoot& root) {
  RootSchedulerState& state = getState(runtime);
  if (&root == state.lastScheduledRoot || root.next != nullptr || state.firstScheduledRoot == &root) {
    return;
  }

  root.next = nullptr;
  if (state.lastScheduledRoot == nullptr) {
    state.firstScheduledRoot = state.lastScheduledRoot = &root;
  } else {
    state.lastScheduledRoot->next = &root;
    state.lastScheduledRoot = &root;
  }
}

void removeRootFromSchedule(ReactRuntime& runtime, FiberRoot& root) {
  RootSchedulerState& state = getState(runtime);
  FiberRoot* previous = nullptr;
  FiberRoot* current = state.firstScheduledRoot;
  while (current != nullptr) {
    if (current == &root) {
      if (previous == nullptr) {
        state.firstScheduledRoot = current->next;
      } else {
        previous->next = current->next;
      }
      if (state.lastScheduledRoot == &root) {
        state.lastScheduledRoot = previous;
      }
      root.next = nullptr;
      break;
    }
    previous = current;
    current = current->next;
  }
}

SchedulerPriority toSchedulerPriority(Lane lane) {
  if (lane == NoLane) {
    return SchedulerPriority::NormalPriority;
  }

  const EventPriority eventPriority = lanesToEventPriority(laneToLanes(lane));
  switch (eventPriority) {
    case DiscreteEventPriority:
    case ContinuousEventPriority:
      return SchedulerPriority::UserBlockingPriority;
    case DefaultEventPriority:
      return SchedulerPriority::NormalPriority;
    case IdleEventPriority:
      return SchedulerPriority::IdlePriority;
    default:
      return SchedulerPriority::NormalPriority;
  }
}

void commitRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  FiberNode& finishedWork,
  Lanes lanes,
  Lanes previousPendingLanes) {
  root.cancelPendingCommit = nullptr;

  while (flushPendingEffects(runtime, jsRuntime, true)) {
    if (getPendingEffectsStatus(runtime) == PendingEffectsStatus::None) {
      break;
    }
  }

  const ExecutionContext context = getExecutionContext(runtime);
  if ((context & (RenderContext | CommitContext)) != NoContext) {
    throw std::logic_error("commitRoot should not run during render or commit context");
  }

  FiberNode* const previousCurrent = root.current;
  if (previousCurrent == &finishedWork) {
    throw std::logic_error("Cannot commit the same tree twice");
  }

  Lanes remainingLanes = mergeLanes(finishedWork.lanes, finishedWork.childLanes);
  remainingLanes = mergeLanes(remainingLanes, getConcurrentlyUpdatedLanes());
  const Lanes pendingDiff = subtractLanes(previousPendingLanes, lanes);
  remainingLanes = mergeLanes(remainingLanes, pendingDiff);

  markRootFinished(root, lanes, remainingLanes, NoLane, NoLanes, NoLanes);

  setDidIncludeCommitPhaseUpdate(runtime, false);

  if (getWorkInProgressRoot(runtime) == &root) {
    setWorkInProgressRoot(runtime, nullptr);
    setWorkInProgressFiber(runtime, nullptr);
    setWorkInProgressRootRenderLanes(runtime, NoLanes);
  }

  root.current = &finishedWork;

  finishedWork.alternate = previousCurrent;
  if (previousCurrent != nullptr) {
    previousCurrent->alternate = &finishedWork;
  }

  setPendingFinishedWork(runtime, &finishedWork);
  setPendingEffectsRoot(runtime, &root);
  setPendingEffectsLanes(runtime, lanes);
  setPendingEffectsRemainingLanes(runtime, remainingLanes);
  setPendingEffectsRenderEndTime(runtime, getCurrentTime(runtime));
  setPendingSuspendedCommitReason(runtime, SuspendedCommitReason::ImmediateCommit);

  auto& workTransitions = getWorkInProgressTransitions(runtime);
  auto& pendingTransitions = getPendingPassiveTransitions(runtime);
  pendingTransitions = workTransitions;
  workTransitions.clear();

  auto& workRecoverableErrors = getWorkInProgressRootRecoverableErrors(runtime);
  auto& pendingRecoverableErrors = getPendingRecoverableErrors(runtime);
  pendingRecoverableErrors = workRecoverableErrors;
  workRecoverableErrors.clear();

  setPendingDidIncludeRenderPhaseUpdate(
      runtime, getWorkInProgressRootDidIncludeRecursiveRenderUpdate(runtime));

  auto& pendingPassiveEffects = getPendingPassiveEffects(runtime);
  pendingPassiveEffects.clear();

  const bool hasPassiveEffects =
      (finishedWork.subtreeFlags & PassiveMask) != NoFlags ||
      (finishedWork.flags & PassiveMask) != NoFlags;

  if (hasPassiveEffects) {
    enqueuePendingPassiveEffect(runtime, finishedWork);
    setPendingEffectsStatus(runtime, PendingEffectsStatus::Passive);
  } else {
    setPendingEffectsStatus(runtime, PendingEffectsStatus::None);
  }

  setIsFlushingPassiveEffects(runtime, false);
  setDidScheduleUpdateDuringPassiveEffects(runtime, false);

  flushPendingEffects(runtime, jsRuntime, true);
}

void scheduleRootTask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root, 
  Lane lane) {
  const SchedulerPriority priority = toSchedulerPriority(lane);
  ReactRuntime* runtimePtr = &runtime;
  FiberRoot* rootPtr = &root;
  auto callbackHandleBox = std::make_shared<TaskHandle>();
  const TaskHandle handle = scheduleCallback(runtime, jsRuntime, priority, [runtimePtr, rootPtr, callbackHandleBox](facebook::jsi::Runtime& taskRuntime, bool didTimeout) -> SchedulerCallbackResult {
    SchedulerCallbackResult result{};
    if (!callbackHandleBox) {
      return result;
    }
    return performWorkOnRootViaSchedulerTask(*runtimePtr, taskRuntime, *rootPtr, *callbackHandleBox, didTimeout);
  });
  *callbackHandleBox = handle;

  root.callbackNode = handle;
  root.callbackPriority = lane;
}

SchedulerCallbackResult performWorkOnRootViaSchedulerTask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  TaskHandle originalCallbackHandle,
  bool didTimeout) {
  SchedulerCallbackResult result{};

  if (root.callbackNode != originalCallbackHandle) {
    return result;
  }

  trackSchedulerEvent(runtime, jsRuntime);

  if (hasPendingCommitEffects(runtime)) {
    root.callbackNode = {};
    root.callbackPriority = NoLane;
    ensureScheduleProcessing(runtime, jsRuntime);
    return result;
  }

  if (flushPendingEffects(runtime, jsRuntime, true)) {
    if (root.callbackNode == originalCallbackHandle) {
      root.callbackNode = {};
      root.callbackPriority = NoLane;
      ensureScheduleProcessing(runtime, jsRuntime);
    }
    return result;
  }

  const int currentTime = static_cast<int>(runtime.now());
  markStarvedLanesAsExpired(root, currentTime);

  FiberRoot* const workInProgressRoot = getWorkInProgressRoot(runtime);
  const Lanes workInProgressRenderLanes =
      workInProgressRoot == &root ? getWorkInProgressRootRenderLanes(runtime) : NoLanes;
  const bool rootHasPendingCommit = root.cancelPendingCommit != nullptr || root.timeoutHandle != noTimeout;

  const Lanes lanes = getNextLanes(root, workInProgressRenderLanes, rootHasPendingCommit);
  if (lanes == NoLanes) {
    root.callbackNode = {};
    root.callbackPriority = NoLane;
    removeRootFromSchedule(runtime, root);
    return result;
  }

  const bool forceSync = !disableSchedulerTimeoutInWorkLoop && didTimeout;
  const bool hasRemainingWork = performWorkOnRoot(runtime, jsRuntime, root, lanes, forceSync);

  if (hasRemainingWork) {
    ensureScheduleProcessing(runtime, jsRuntime);
  }

  const int postWorkTime = static_cast<int>(runtime.now());
  scheduleTaskForRootDuringMicrotask(runtime, jsRuntime, root, postWorkTime);

  if (root.callbackNode && root.callbackNode == originalCallbackHandle) {
    result.hasContinuation = true;
    result.continuation = [runtimePtr = &runtime, rootPtr = &root, originalHandle = originalCallbackHandle](
                             facebook::jsi::Runtime& continuationRuntime,
                             bool continuationDidTimeout) -> SchedulerCallbackResult {
      return performWorkOnRootViaSchedulerTask(
        *runtimePtr,
        continuationRuntime,
        *rootPtr,
        originalHandle,
        continuationDidTimeout);
    };
  }

  return result;
}

void processRootSchedule(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);
  if (state.isProcessingRootSchedule) {
    return;
  }

  state.isProcessingRootSchedule = true;
  state.mightHavePendingSyncWork = false;

  while (state.didScheduleRootProcessing) {
    state.didScheduleRootProcessing = false;

    Lanes syncTransitionLanes = NoLanes;
    if (state.currentEventTransitionLane != NoLane) {
      if (runtime.shouldAttemptEagerTransition()) {
        syncTransitionLanes = state.currentEventTransitionLane;
      } else if (enableDefaultTransitionIndicator) {
        syncTransitionLanes = DefaultLane;
      }
    }

    const int currentTime = static_cast<int>(runtime.now());
    FiberRoot* prev = nullptr;
    FiberRoot* root = state.firstScheduledRoot;

    while (root != nullptr) {
      FiberRoot* const next = root->next;
      const Lanes scheduledLanes = scheduleTaskForRootDuringMicrotask(runtime, jsRuntime, *root, currentTime);

      if (scheduledLanes == NoLanes) {
        root->next = nullptr;
        if (prev == nullptr) {
          state.firstScheduledRoot = next;
        } else {
          prev->next = next;
        }
        if (next == nullptr) {
          state.lastScheduledRoot = prev;
        }
      } else {
        prev = root;

        if ((includesSyncLane(scheduledLanes) || (enableGestureTransition && isGestureRender(scheduledLanes))) &&
            !checkIfRootIsPrerendering(*root, scheduledLanes)) {
          state.mightHavePendingSyncWork = true;
        }
      }

      root = next;
    }

    state.lastScheduledRoot = prev;

    if (!hasPendingCommitEffects(runtime)) {
      flushSyncWorkAcrossRoots(runtime, jsRuntime, syncTransitionLanes, false);
    }
  }

  if (state.currentEventTransitionLane != NoLane) {
    state.currentEventTransitionLane = NoLane;
    startDefaultTransitionIndicatorIfNeeded(runtime, jsRuntime);
  }

  state.isProcessingRootSchedule = false;
}

void processRootScheduleInMicrotask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);
  state.didScheduleMicrotask = false;
  state.didScheduleMicrotaskAct = false;
  processRootSchedule(runtime, jsRuntime);
}

void processRootScheduleInImmediateTask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  trackSchedulerEvent(runtime, jsRuntime);
  processRootScheduleInMicrotask(runtime, jsRuntime);
}

void ensureScheduleProcessing(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);
  if (state.didScheduleRootProcessing) {
    return;
  }

  state.didScheduleRootProcessing = true;
  ensureScheduleIsScheduled(runtime, jsRuntime);
}

void flushSyncWorkAcrossRoots(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  Lanes syncTransitionLanes,
  bool onlyLegacy) {
  RootSchedulerState& state = getState(runtime);
  if (state.isFlushingWork) {
    return;
  }

  if (!state.mightHavePendingSyncWork && syncTransitionLanes == NoLanes) {
    return;
  }

  bool didPerformSomeWork = false;
  bool shouldProcessSchedule = false;

  state.isFlushingWork = true;

  while (flushPendingEffects(runtime, jsRuntime, true)) {
    shouldProcessSchedule = true;
  }

  do {
    didPerformSomeWork = false;
    FiberRoot* root = state.firstScheduledRoot;
    while (root != nullptr) {
      FiberRoot* const next = root->next;

      if (onlyLegacy && (disableLegacyMode || root->tag != RootTag::LegacyRoot)) {
        root = next;
        continue;
      }

      Lanes nextLanes = NoLanes;
      if (syncTransitionLanes != NoLanes) {
        nextLanes = getNextLanesToFlushSync(*root, syncTransitionLanes);
      } else {
        FiberRoot* const workInProgressRoot = getWorkInProgressRoot(runtime);
        const Lanes workInProgressRenderLanes = workInProgressRoot == root ? getWorkInProgressRootRenderLanes(runtime) : NoLanes;

        const bool rootHasPendingCommit = root->cancelPendingCommit != nullptr || root->timeoutHandle != noTimeout;
        nextLanes = getNextLanes(*root, workInProgressRenderLanes, rootHasPendingCommit);
      }

      if (nextLanes != NoLanes) {
        const bool shouldFlushSync =
          syncTransitionLanes != NoLanes ||
          ((!checkIfRootIsPrerendering(*root, nextLanes)) &&
            (includesSyncLane(nextLanes) || (enableGestureTransition && isGestureRender(nextLanes))));

        if (shouldFlushSync) {
          didPerformSomeWork = true;
          facebook::jsi::Runtime& runtimeForTask = jsRuntime;
          const bool hasRemainingWork = performSyncWorkOnRoot(runtime, runtimeForTask, *root, nextLanes);
          if (hasRemainingWork) {
            shouldProcessSchedule = true;
          }
        }
      }

      root = next;
    }
  } while (didPerformSomeWork);

  state.isFlushingWork = false;
  state.mightHavePendingSyncWork = false;

  if (shouldProcessSchedule) {
    ensureScheduleProcessing(runtime, jsRuntime);
  }
}

Lanes scheduleTaskForRootDuringMicrotask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  int currentTime) {
  markStarvedLanesAsExpired(root, currentTime);

  const FiberRoot* const rootWithPendingPassiveEffects = getRootWithPendingPassiveEffects(runtime);
  const Lanes pendingPassiveEffectsLanes = getPendingPassiveEffectsLanes(runtime);
  FiberRoot* const workInProgressRoot = getWorkInProgressRoot(runtime);
  const Lanes workInProgressRenderLanes =
      workInProgressRoot == &root ? getWorkInProgressRootRenderLanes(runtime) : NoLanes;
  const bool rootHasPendingCommit = root.cancelPendingCommit != nullptr || root.timeoutHandle != noTimeout;

  Lanes nextLanes = NoLanes;
  if (enableYieldingBeforePassive && rootWithPendingPassiveEffects == &root) {
    nextLanes = pendingPassiveEffectsLanes;
  } else {
    nextLanes = getNextLanes(root, workInProgressRenderLanes, rootHasPendingCommit);
  }

  const TaskHandle existingCallbackNode = root.callbackNode;
  const Lane existingCallbackPriority = root.callbackPriority;

  if (nextLanes == NoLanes ||
      (workInProgressRoot == &root && isWorkLoopSuspendedOnData(runtime)) ||
      root.cancelPendingCommit != nullptr) {
    if (existingCallbackNode) {
      cancelCallback(runtime, jsRuntime, existingCallbackNode);
    }
    root.callbackNode = {};
    root.callbackPriority = NoLane;
    return NoLanes;
  }

  if (includesSyncLane(nextLanes) && !checkIfRootIsPrerendering(root, nextLanes)) {
    if (existingCallbackNode) {
      cancelCallback(runtime, jsRuntime, existingCallbackNode);
    }
    root.callbackNode = {};
    root.callbackPriority = SyncLane;
    return nextLanes;
  }

  const Lane newCallbackPriority = getHighestPriorityLane(nextLanes);
  const bool actQueueActive = hasActiveActQueue(jsRuntime);
  if (existingCallbackNode && existingCallbackPriority == newCallbackPriority) {
    const bool shouldRescheduleOnActQueue = actQueueActive && !isActCallbackHandle(existingCallbackNode);
    if (!shouldRescheduleOnActQueue) {
      return nextLanes;
    }
    cancelCallback(runtime, jsRuntime, existingCallbackNode);
  } else if (existingCallbackNode) {
    cancelCallback(runtime, jsRuntime, existingCallbackNode);
  }

  scheduleRootTask(runtime, jsRuntime, root, newCallbackPriority);
  return nextLanes;
}

void ensureScheduleIsScheduledInternal(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);
  if (state.didScheduleMicrotask) {
    return;
  }

  state.didScheduleMicrotask = true;
  scheduleImmediateRootScheduleTask(runtime, jsRuntime);
}

void scheduleImmediateRootScheduleTask(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  enqueueActMicrotask(runtime, jsRuntime);

  if (tryScheduleRootMicrotask(runtime, jsRuntime)) {
    return;
  }

  scheduleImmediateTaskFallback(runtime, jsRuntime);
}

void trackSchedulerEvent(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  if (!enableProfilerTimer || !enableComponentPerformanceTrack) {
    return;
  }

  RootSchedulerState& state = getState(runtime);
  state.hasTrackedSchedulerEvent = false;
  state.lastTrackedSchedulerEventType.clear();
  state.lastTrackedSchedulerEventTimestamp = -1.0;

  try {
    facebook::jsi::Object global = jsRuntime.global();
    if (!global.hasProperty(jsRuntime, "event")) {
      return;
    }

    facebook::jsi::Value eventValue = global.getProperty(jsRuntime, "event");
    if (!eventValue.isObject()) {
      return;
    }

    facebook::jsi::Object eventObject = eventValue.getObject(jsRuntime);
    std::string eventType;
    double timestamp = -1.0;

    if (eventObject.hasProperty(jsRuntime, "type")) {
      facebook::jsi::Value typeValue = eventObject.getProperty(jsRuntime, "type");
      if (typeValue.isString()) {
        eventType = typeValue.getString(jsRuntime).utf8(jsRuntime);
      }
    }

    if (eventObject.hasProperty(jsRuntime, "timeStamp")) {
      facebook::jsi::Value timeValue = eventObject.getProperty(jsRuntime, "timeStamp");
      if (timeValue.isNumber()) {
        timestamp = timeValue.getNumber();
      }
    }

    state.lastTrackedSchedulerEventType = std::move(eventType);
    state.lastTrackedSchedulerEventTimestamp = timestamp;
    state.hasTrackedSchedulerEvent = true;
  } catch (const std::exception&) {
    state.hasTrackedSchedulerEvent = false;
    state.lastTrackedSchedulerEventType.clear();
    state.lastTrackedSchedulerEventTimestamp = -1.0;
  }
}

} // namespace

bool performSyncWorkOnRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes) {
  if (flushPendingEffects(runtime, jsRuntime, false)) {
    return true;
  }

  if (enableProfilerTimer && enableProfilerNestedUpdatePhase) {
    syncNestedUpdateFlag();
  }

  return performWorkOnRoot(runtime, jsRuntime, root, lanes, true);
}

bool performWorkOnRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes,
  bool forceSync) {
  const Lanes previousPendingLanes = root.pendingLanes;
  const bool shouldRenderSync = forceSync || includesBlockingLane(lanes) || includesSyncLane(lanes);

  RootExitStatus status = shouldRenderSync
      ? renderRootSync(runtime, jsRuntime, root, lanes, false)
      : renderRootConcurrent(runtime, jsRuntime, root, lanes);

  switch (status) {
    case RootExitStatus::Completed: {
      FiberNode* const finishedWork = root.current != nullptr ? root.current->alternate : nullptr;
      if (finishedWork != nullptr) {
        commitRoot(runtime, jsRuntime, root, *finishedWork, lanes, previousPendingLanes);

        if (enableDefaultTransitionIndicator && includesLoadingIndicatorLanes(lanes)) {
          markIndicatorHandled(runtime, jsRuntime, root);
        }
      }

      cleanupDefaultTransitionIndicatorIfNeeded(runtime, jsRuntime, root);
      break;
    }
    case RootExitStatus::Suspended:
    case RootExitStatus::SuspendedWithDelay:
    case RootExitStatus::SuspendedAtTheShell: {
      constexpr bool didAttemptEntireTree = false;
      markRootSuspended(root, lanes, NoLane, didAttemptEntireTree);
      break;
    }
    case RootExitStatus::Errored:
    case RootExitStatus::FatalErrored: {
      const Lanes remainingLanes = subtractLanes(previousPendingLanes, lanes);
      markRootFinished(root, lanes, remainingLanes, NoLane, NoLanes, NoLanes);
      break;
    }
    case RootExitStatus::InProgress:
      break;
  }

  root.callbackNode = {};
  root.callbackPriority = NoLane;

  const bool hasRemainingWork = getHighestPriorityPendingLanes(root) != NoLanes;
  if (!hasRemainingWork) {
    removeRootFromSchedule(runtime, root);
  }

  return hasRemainingWork;
}

void ensureRootIsScheduled(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root) {
  addRootToSchedule(runtime, root);
  getState(runtime).mightHavePendingSyncWork = true;
  ensureScheduleProcessing(runtime, jsRuntime);

  if (!disableLegacyMode && root.tag == RootTag::LegacyRoot) {
    bool expectedLegacyFlagUpdate = false;
    bool didSetLegacyFlag = false;
    try {
      facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
      if (hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kIsBatchingLegacy)) {
        const facebook::jsi::Value batching = getReactSharedInternalsProperty(
          jsRuntime,
          internals,
          ReactSharedInternalsKeys::kIsBatchingLegacy);
        if (batching.isBool() && batching.getBool()) {
          expectedLegacyFlagUpdate = true;
          facebook::jsi::Value flagValue(true);
          setReactSharedInternalsProperty(
            jsRuntime,
            internals,
            ReactSharedInternalsKeys::kDidScheduleLegacyUpdate,
            std::move(flagValue));
          didSetLegacyFlag = true;
        }
      }
  } catch (const std::exception& ex) {
#ifndef NDEBUG
      std::cerr << "React ensureRootIsScheduled failed to flag legacy update: " << ex.what() << std::endl;
#endif
    }

#ifndef NDEBUG
    if (expectedLegacyFlagUpdate && !didSetLegacyFlag) {
      std::cerr << "React ensureRootIsScheduled could not update ReactSharedInternals.didScheduleLegacyUpdate." << std::endl;
    }
#endif
  }
}

void flushSyncWorkOnAllRoots(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  Lanes syncTransitionLanes) {
  flushSyncWorkAcrossRoots(runtime, jsRuntime, syncTransitionLanes, false);
}

void flushSyncWorkOnLegacyRootsOnly(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  if (!disableLegacyMode) {
    flushSyncWorkAcrossRoots(runtime, jsRuntime, NoLanes, true);
  }
}

Lane requestTransitionLane(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const Transition* transition) {
  (void)jsRuntime;
  (void)transition;
  RootSchedulerState& state = getState(runtime);
  if (state.currentEventTransitionLane == NoLane) {
    const Lane actionScopeLane = peekEntangledActionLane(runtime);
    state.currentEventTransitionLane =
        actionScopeLane != NoLane ? actionScopeLane : claimNextTransitionLane();
  }
  return state.currentEventTransitionLane;
}

bool didCurrentEventScheduleTransition(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  (void)jsRuntime;
  return getState(runtime).currentEventTransitionLane != NoLane;
}

void markIndicatorHandled(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root) {
  (void)jsRuntime;
  if (!enableDefaultTransitionIndicator) {
    return;
  }

  RootSchedulerState& state = getState(runtime);
  if (state.currentEventTransitionLane != NoLane) {
    root.indicatorLanes &= ~state.currentEventTransitionLane;
  }
  markIsomorphicIndicatorHandled(runtime);
}

void ensureScheduleIsScheduled(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime) {
  RootSchedulerState& state = getState(runtime);

  bool handledByActQueue = false;
  try {
    const facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
    if (hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kActQueue)) {
      const facebook::jsi::Value queue = getReactSharedInternalsProperty(
        jsRuntime,
        internals,
        ReactSharedInternalsKeys::kActQueue);
      if (!queue.isNull() && !queue.isUndefined()) {
        handledByActQueue = true;
        if (!state.didScheduleMicrotaskAct) {
          state.didScheduleMicrotaskAct = true;
          scheduleImmediateRootScheduleTask(runtime, jsRuntime);
        }
      }
    }
  } catch (const std::exception&) {
    handledByActQueue = false;
  }

  if (!handledByActQueue) {
    ensureScheduleIsScheduledInternal(runtime, jsRuntime);
  }
}

void registerRootDefaultIndicator(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  std::function<std::function<void()>()> onDefaultTransitionIndicator) {
  (void)jsRuntime;
  if (!enableDefaultTransitionIndicator) {
    root.onDefaultTransitionIndicator = {};
    return;
  }

  root.onDefaultTransitionIndicator = std::move(onDefaultTransitionIndicator);
  if (root.onDefaultTransitionIndicator) {
    registerDefaultIndicator(runtime, &root, root.onDefaultTransitionIndicator);
  }
}

} // namespace react
