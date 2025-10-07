#include "ReactReconciler/ReactFiberRootScheduler.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberAsyncAction.h"
#include "ReactReconciler/ReactEventPriorities.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactFeatureFlags.h"
#include "shared/ReactSharedInternals.h"
#include "jsi/jsi.h"
#include <exception>
#include <iostream>
#include <memory>
#include <limits>
#include <utility>
#include <utility>

namespace react {
namespace {

RootSchedulerState& getState(ReactRuntime& runtime) {
  return runtime.rootSchedulerState();
}

const RootSchedulerState& getState(const ReactRuntime& runtime) {
  return runtime.rootSchedulerState();
}

using MicrotaskCallback = std::function<void(facebook::jsi::Runtime&)>;
using SchedulerCallback = std::function<void(facebook::jsi::Runtime&, bool)>;

constexpr std::uint64_t kActCallbackBit = 1ull << 63;

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

      if (entry.strictEquals(jsRuntime, callbackValue)) {
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

TaskHandle scheduleCallback(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  SchedulerPriority priority,
  SchedulerCallback callback) {
  RootSchedulerState& state = getState(runtime);
  auto callbackPtr = std::make_shared<SchedulerCallback>(std::move(callback));

  const std::uint64_t actKey = state.nextActCallbackId;
  facebook::jsi::Function hostFunction = facebook::jsi::Function::createFromHostFunction(
      jsRuntime,
      facebook::jsi::PropNameID::forAscii(jsRuntime, "__reactActSchedulerTask"),
      1,
      [callbackPtr, &state, actKey](facebook::jsi::Runtime& runtime,
                                    const facebook::jsi::Value& thisValue,
                                    const facebook::jsi::Value* arguments,
                                    size_t count) -> facebook::jsi::Value {
        (void)thisValue;
        bool didTimeout = false;
        if (count > 0 && arguments[0].isBool()) {
          didTimeout = arguments[0].getBool();
        }
        state.actCallbacks.erase(actKey);
        if (callbackPtr && *callbackPtr) {
          (*callbackPtr)(runtime, didTimeout);
        }
        return facebook::jsi::Value::null();
      });

  facebook::jsi::Function queueFunction(hostFunction);
  if (pushActQueueCallback(jsRuntime, std::move(queueFunction))) {
    state.actCallbacks.emplace(actKey, hostFunction);
    return makeActCallbackHandle(state);
  }

  facebook::jsi::Runtime* capturedRuntime = &jsRuntime;
  return runtime.scheduleTask(priority, [callbackPtr, capturedRuntime]() {
    if (callbackPtr && *callbackPtr) {
      (*callbackPtr)(*capturedRuntime, false);
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
      removeActQueueCallback(jsRuntime, it->second);
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
bool performWorkOnRoot(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root, Lanes lanes, bool forceSync);
bool performSyncWorkOnRoot(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime, FiberRoot& root, Lanes lanes);
void performWorkOnRootViaSchedulerTask(
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
      root->pendingIndicator = []() {};
      continue;
    }

    try {
      auto cleanup = onIndicator();
      if (cleanup) {
        root->pendingIndicator = std::move(cleanup);
      } else {
        root->pendingIndicator = []() {};
      }
    } catch (const std::exception& ex) {
      root->pendingIndicator = []() {};
      reportDefaultIndicatorError(ex);
    } catch (...) {
      root->pendingIndicator = []() {};
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
  FiberRoot& root, 
  FiberNode& finishedWork) {
  FiberNode* const previousCurrent = root.current;

  if (previousCurrent == &finishedWork) {
    return;
  }

  root.current = &finishedWork;

  finishedWork.alternate = previousCurrent;
  if (previousCurrent != nullptr) {
    previousCurrent->alternate = &finishedWork;
  }
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
  const TaskHandle handle = scheduleCallback(runtime, jsRuntime, priority, [runtimePtr, rootPtr, callbackHandleBox](facebook::jsi::Runtime& taskRuntime, bool didTimeout) {
    if (!callbackHandleBox) {
      return;
    }
    performWorkOnRootViaSchedulerTask(*runtimePtr, taskRuntime, *rootPtr, *callbackHandleBox, didTimeout);
  });
  *callbackHandleBox = handle;

  root.callbackNode = handle;
  root.callbackPriority = lane;
}

void performWorkOnRootViaSchedulerTask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  TaskHandle originalCallbackHandle,
  bool didTimeout) {
  if (root.callbackNode != originalCallbackHandle) {
    return;
  }

  if (hasPendingCommitEffects(runtime)) {
    root.callbackNode = {};
    root.callbackPriority = NoLane;
    ensureScheduleProcessing(runtime, jsRuntime);
    return;
  }

  if (flushPendingEffects(runtime, jsRuntime, true)) {
    if (root.callbackNode == originalCallbackHandle) {
      root.callbackNode = {};
      root.callbackPriority = NoLane;
      ensureScheduleProcessing(runtime, jsRuntime);
    }
    return;
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
    return;
  }

  const bool forceSync = !disableSchedulerTimeoutInWorkLoop && didTimeout;
  const bool hasRemainingWork = performWorkOnRoot(runtime, jsRuntime, root, lanes, forceSync);

  if (hasRemainingWork) {
    ensureScheduleProcessing(runtime, jsRuntime);
  }
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

bool performSyncWorkOnRoot(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  Lanes lanes) {
  if (flushPendingEffects(runtime, jsRuntime, false)) {
    return true;
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

  const bool shouldRenderSync =
      forceSync || includesBlockingLane(lanes) || includesSyncLane(lanes);

  RootExitStatus status = shouldRenderSync
      ? renderRootSync(runtime, jsRuntime, root, lanes, false)
      : renderRootConcurrent(runtime, jsRuntime, root, lanes);

  switch (status) {
    case RootExitStatus::Completed: {
      FiberNode* const finishedWork = root.current != nullptr ? root.current->alternate : nullptr;
      const Lanes remainingLanes = subtractLanes(previousPendingLanes, lanes);
      markRootFinished(root, lanes, remainingLanes, NoLane, NoLanes, NoLanes);

      if (finishedWork != nullptr) {
        commitRoot(root, *finishedWork);

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
        const Lanes workInProgressRenderLanes =
            workInProgressRoot == root ? getWorkInProgressRootRenderLanes(runtime) : NoLanes;
        const bool rootHasPendingCommit =
            root->cancelPendingCommit != nullptr || root->timeoutHandle != noTimeout;
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

} // namespace

void ensureRootIsScheduled(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root) {
  addRootToSchedule(runtime, root);
  getState(runtime).mightHavePendingSyncWork = true;
  ensureScheduleProcessing(runtime, jsRuntime);

  if (!disableLegacyMode && root.tag == RootTag::LegacyRoot) {
    try {
      facebook::jsi::Object internals = getReactSharedInternals(jsRuntime);
      if (hasReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kIsBatchingLegacy)) {
        const facebook::jsi::Value batching = getReactSharedInternalsProperty(
          jsRuntime,
          internals,
          ReactSharedInternalsKeys::kIsBatchingLegacy);
        if (batching.isBool() && batching.getBool()) {
          facebook::jsi::Value flagValue(true);
          setReactSharedInternalsProperty(
            jsRuntime,
            internals,
            ReactSharedInternalsKeys::kDidScheduleLegacyUpdate,
            std::move(flagValue));
        }
      }
    } catch (const std::exception&) {
      // Ignore if ReactSharedInternals are not available in the runtime.
    }
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
