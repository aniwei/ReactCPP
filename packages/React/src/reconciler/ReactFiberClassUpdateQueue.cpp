/**
 * React Fiber Class Update Queue
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberClassUpdateQueue.js
 */

#include "ReactFiberClassUpdateQueue.h"

#include <functional>

#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

void ClassUpdateQueueGlobals::reset() {
  hasForceUpdate = false;
  didReadFromEntangledAsyncAction = false;
}

void ClassUpdateQueueGlobals::resetCurrentlyProcessingQueue() {
  currentlyProcessingQueue = nullptr;
}

void initializeClassUpdateQueue(const FiberRef& fiber) {
  auto queue = std::make_shared<UpdateQueue>();

  // baseState 在 processClassUpdateQueue 内从 Fiber.memoizedState 初始化。
  queue->baseState = facebook::jsi::Value::undefined();

  queue->firstBaseUpdate = nullptr;
  queue->lastBaseUpdate = nullptr;
  queue->shared = std::make_shared<SharedQueue>();
  queue->callbacks.clear();

  fiber->updateQueue = queue;
}

void cloneClassUpdateQueue(
  facebook::jsi::Runtime& jsiRuntime,
  const FiberRef& current,
  const FiberRef& workInProgress
) {
  auto* queuePtr = std::get_if<std::shared_ptr<UpdateQueue>>(&workInProgress->updateQueue);
  auto* currentQueuePtr = std::get_if<std::shared_ptr<UpdateQueue>>(&current->updateQueue);
  if (!queuePtr || !currentQueuePtr) return;
  auto queue = *queuePtr;
  auto currentQueue = *currentQueuePtr;

  if (queue == currentQueue) {
    auto clone = std::make_shared<UpdateQueue>();
    clone->baseState = facebook::jsi::Value(jsiRuntime, currentQueue->baseState);
    clone->firstBaseUpdate = currentQueue->firstBaseUpdate;
    clone->lastBaseUpdate = currentQueue->lastBaseUpdate;
    clone->shared = currentQueue->shared;
    clone->callbacks.clear();

    workInProgress->updateQueue = clone;
  }
}

std::shared_ptr<ClassUpdate> createClassUpdate(Lane lane) {
  auto update = std::make_shared<ClassUpdate>();
  update->lane = lane;
  update->tag = UpdateTag::UpdateState;
  update->payload = facebook::jsi::Value::undefined();
  update->callback = nullptr;
  update->next = nullptr;
  return update;
}

std::shared_ptr<UpdateQueue> getClassUpdateQueue(const FiberRef& fiber) {
  auto* ptr = std::get_if<std::shared_ptr<UpdateQueue>>(&fiber->updateQueue);
  return ptr ? *ptr : nullptr;
}

FiberRootRef enqueueClassUpdate(
  FiberRef fiber,
  std::shared_ptr<ClassUpdate> update,
  Lane lane
) {
  auto updateQueue = getClassUpdateQueue(fiber);
  if (!updateQueue) {
    return nullptr;
  }

  auto sharedQueue = updateQueue->shared;

  auto pending = sharedQueue->pending;
  if (pending == nullptr) {
    update->next = update;
  } else {
    update->next = pending->next;
    pending->next = update;
  }
  sharedQueue->pending = update;

  sharedQueue->lanes = mergeLanes(sharedQueue->lanes, lane);

  return nullptr;
}

void entangleTransitionsForClassUpdate(
  FiberRootRef root,
  FiberRef fiber,
  Lane lane
) {
  auto updateQueue = getClassUpdateQueue(fiber);
  if (!updateQueue) {
    return;
  }

  auto sharedQueue = updateQueue->shared;

  if (isTransitionLane(lane)) {
    Lanes queueLanes = sharedQueue->lanes;
    queueLanes = intersectLanes(queueLanes, root->pendingLanes);
    Lanes newQueueLanes = mergeLanes(queueLanes, lane);
    sharedQueue->lanes = newQueueLanes;
  }
}

void enqueueClassCapturedUpdate(
  FiberRef workInProgress,
  std::shared_ptr<ClassUpdate> capturedUpdate
) {
  auto queue = getClassUpdateQueue(workInProgress);
  if (!queue) {
    return;
  }

  auto lastBaseUpdate = queue->lastBaseUpdate;
  if (lastBaseUpdate == nullptr) {
    queue->firstBaseUpdate = capturedUpdate;
  } else {
    lastBaseUpdate->next = capturedUpdate;
  }
  queue->lastBaseUpdate = capturedUpdate;
}

facebook::jsi::Value getStateFromClassUpdate(
  FiberRef workInProgress,
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  std::shared_ptr<UpdateQueue> /*queue*/,
  std::shared_ptr<ClassUpdate> update,
  const facebook::jsi::Value& prevState,
  const facebook::jsi::Value& nextProps,
  const facebook::jsi::Value& /*instance*/
) {
  (void)nextProps;
  auto& globals = hostRuntime.getClassUpdateQueueGlobals();

  if (!update) {
    return facebook::jsi::Value(jsiRuntime, prevState);
  }

  switch (update->tag) {
    case UpdateTag::ReplaceState: {
      if (update->payload.isUndefined()) {
        return facebook::jsi::Value(jsiRuntime, prevState);
      }
      return facebook::jsi::Value(jsiRuntime, update->payload);
    }

    case UpdateTag::CaptureUpdate: {
      workInProgress->flags = (workInProgress->flags & ~ShouldCapture) | DidCapture;
      [[fallthrough]];
    }

    case UpdateTag::UpdateState: {
      if (update->payload.isUndefined()) {
        return facebook::jsi::Value(jsiRuntime, prevState);
      }

      if (update->payload.isObject() && update->payload.asObject(jsiRuntime).isFunction(jsiRuntime)) {
        auto fn = update->payload.asObject(jsiRuntime).asFunction(jsiRuntime);
        auto result = fn.call(jsiRuntime, prevState, nextProps);
        return facebook::jsi::Value(jsiRuntime, result);
      }

      // 简化：不做 partialState merge，直接把 payload 当作 nextState。
      return facebook::jsi::Value(jsiRuntime, update->payload);
    }

    case UpdateTag::ForceUpdate: {
      globals.hasForceUpdate = true;
      return facebook::jsi::Value(jsiRuntime, prevState);
    }
  }

  return facebook::jsi::Value(jsiRuntime, prevState);
}

void processClassUpdateQueue(
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  FiberRef workInProgress,
  const facebook::jsi::Value& props,
  const facebook::jsi::Value& instance,
  Lanes renderLanes
) {
  auto& globals = hostRuntime.getClassUpdateQueueGlobals();
  globals.didReadFromEntangledAsyncAction = false;

  auto queue = getClassUpdateQueue(workInProgress);
  if (!queue) {
    return;
  }

  globals.hasForceUpdate = false;

  auto firstBaseUpdate = queue->firstBaseUpdate;
  auto lastBaseUpdate = queue->lastBaseUpdate;

  auto pendingQueue = queue->shared->pending;
  if (pendingQueue != nullptr) {
    queue->shared->pending = nullptr;

    auto lastPendingUpdate = pendingQueue;
    auto firstPendingUpdate = lastPendingUpdate->next;
    lastPendingUpdate->next = nullptr;

    if (lastBaseUpdate == nullptr) {
      firstBaseUpdate = firstPendingUpdate;
    } else {
      lastBaseUpdate->next = firstPendingUpdate;
    }
    lastBaseUpdate = lastPendingUpdate;

    FiberRef current = workInProgress->getAlternate();
    if (current != nullptr) {
      auto currentQueue = getClassUpdateQueue(current);
      if (currentQueue) {
        auto currentLastBaseUpdate = currentQueue->lastBaseUpdate;
        if (currentLastBaseUpdate != lastBaseUpdate) {
          if (currentLastBaseUpdate == nullptr) {
            currentQueue->firstBaseUpdate = firstPendingUpdate;
          } else {
            currentLastBaseUpdate->next = firstPendingUpdate;
          }
          currentQueue->lastBaseUpdate = lastPendingUpdate;
        }
      }
    }
  }

  if (firstBaseUpdate != nullptr) {
    facebook::jsi::Value newState = queue->baseState.isUndefined()
      ? facebook::jsi::Value::undefined()
      : facebook::jsi::Value(jsiRuntime, queue->baseState);
    Lanes newLanes = NoLanes;

    facebook::jsi::Value newBaseState = facebook::jsi::Value::undefined();
    bool hasNewBaseState = false;
    std::shared_ptr<ClassUpdate> newFirstBaseUpdate = nullptr;
    std::shared_ptr<ClassUpdate> newLastBaseUpdate = nullptr;

    auto update = firstBaseUpdate;
    do {
      Lane updateLane = removeLanes(update->lane, OffscreenLane);

      bool shouldSkipUpdate = !isSubsetOfLanes(renderLanes, updateLane);

      if (shouldSkipUpdate) {
        auto clone = std::make_shared<ClassUpdate>();
        clone->lane = updateLane;
        clone->tag = update->tag;
        clone->payload = facebook::jsi::Value(jsiRuntime, update->payload);
        clone->callback = update->callback;
        clone->next = nullptr;

        if (newLastBaseUpdate == nullptr) {
          newFirstBaseUpdate = newLastBaseUpdate = clone;
          newBaseState = facebook::jsi::Value(jsiRuntime, newState);
          hasNewBaseState = true;
        } else {
          newLastBaseUpdate->next = clone;
          newLastBaseUpdate = clone;
        }

        newLanes = mergeLanes(newLanes, updateLane);
      } else {
        if (newLastBaseUpdate != nullptr) {
          auto clone = std::make_shared<ClassUpdate>();
          clone->lane = NoLane;
          clone->tag = update->tag;
          clone->payload = facebook::jsi::Value(jsiRuntime, update->payload);
          clone->callback = nullptr;
          clone->next = nullptr;

          newLastBaseUpdate->next = clone;
          newLastBaseUpdate = clone;
        }

        newState = getStateFromClassUpdate(
          workInProgress,
          jsiRuntime,
          hostRuntime,
          queue,
          update,
          newState,
          props,
          instance
        );

        if (update->callback) {
          workInProgress->flags = workInProgress->flags | Callback;
          queue->callbacks.push_back(update->callback);
        }
      }

      update = update->next;
      if (update == nullptr) {
        pendingQueue = queue->shared->pending;
        if (pendingQueue == nullptr) {
          break;
        }

        auto lastPending = pendingQueue;
        auto firstPending = lastPending->next;
        lastPending->next = nullptr;
        update = firstPending;
        queue->lastBaseUpdate = lastPending;
        queue->shared->pending = nullptr;
      }
    } while (true);

    if (newLastBaseUpdate == nullptr) {
      newBaseState = facebook::jsi::Value(jsiRuntime, newState);
      hasNewBaseState = true;
    }

    if (hasNewBaseState) {
      queue->baseState = std::move(newBaseState);
    }
    queue->firstBaseUpdate = newFirstBaseUpdate;
    queue->lastBaseUpdate = newLastBaseUpdate;

    if (firstBaseUpdate == nullptr) {
      queue->shared->lanes = NoLanes;
    }

    workInProgress->lanes = newLanes;
    workInProgress->memoizedState = std::any{};
  }
}

void callClassUpdateCallback(std::function<void()> callback) {
  if (callback) {
    callback();
  }
}

void resetHasForceUpdateBeforeProcessing(ReactHostRuntime& hostRuntime) {
  hostRuntime.getClassUpdateQueueGlobals().hasForceUpdate = false;
}

bool checkHasForceUpdateAfterProcessing(ReactHostRuntime& hostRuntime) {
  return hostRuntime.getClassUpdateQueueGlobals().hasForceUpdate;
}

void deferHiddenClassCallbacks(std::shared_ptr<UpdateQueue> updateQueue) {
  if (updateQueue->callbacks.empty()) {
    return;
  }

  auto& hiddenCallbacks = updateQueue->shared->hiddenCallbacks;
  hiddenCallbacks.insert(
    hiddenCallbacks.end(),
    updateQueue->callbacks.begin(),
    updateQueue->callbacks.end()
  );
  updateQueue->callbacks.clear();
}

void commitHiddenClassCallbacks(
  std::shared_ptr<UpdateQueue> updateQueue
) {
  auto& hiddenCallbacks = updateQueue->shared->hiddenCallbacks;
  if (hiddenCallbacks.empty()) {
    return;
  }

  for (auto& callback : hiddenCallbacks) {
    callClassUpdateCallback(callback);
  }
  hiddenCallbacks.clear();
}

void commitClassCallbacks(
  std::shared_ptr<UpdateQueue> updateQueue
) {
  if (updateQueue->callbacks.empty()) {
    return;
  }

  for (auto& callback : updateQueue->callbacks) {
    callClassUpdateCallback(callback);
  }
  updateQueue->callbacks.clear();
}

bool hasClassPendingUpdates(FiberRef fiber) {
  auto queue = getClassUpdateQueue(fiber);
  if (!queue) {
    return false;
  }
  return queue->shared->pending != nullptr || queue->firstBaseUpdate != nullptr;
}

FiberRootRef scheduleClassUpdateOnFiber(
  facebook::jsi::Runtime& jsiRuntime,
  FiberRef fiber,
  const facebook::jsi::Value& payload,
  Lane lane
) {
  auto update = createClassUpdate(lane);
  update->payload = facebook::jsi::Value(jsiRuntime, payload);
  return enqueueClassUpdate(fiber, update, lane);
}

FiberRootRef scheduleClassForceUpdateOnFiber(FiberRef fiber, Lane lane) {
  auto update = createClassUpdate(lane);
  update->tag = UpdateTag::ForceUpdate;
  return enqueueClassUpdate(fiber, update, lane);
}

} // namespace react::reconciler
