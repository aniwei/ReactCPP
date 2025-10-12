#include "ReactReconciler/ReactFiberClassUpdateQueue.h"
#include "ReactReconciler/ReactFiberAsyncAction.h"
#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactFiberHostRootState.h"
#include "ReactReconciler/ReactWorkTags.h"
#include "ReactReconciler/ReactFiberWorkLoop.h"
#include "ReactRuntime/ReactRuntime.h"

#include "jsi/jsi.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace react {

using facebook::jsi::Runtime;
using facebook::jsi::Value;

namespace {

void destroyUpdatePayload(Update& update) {
  switch (update.payloadType) {
    case UpdatePayloadType::HostRoot: {
      auto* payload = static_cast<HostRootUpdatePayload*>(update.payload);
      delete payload;
      break;
    }
    case UpdatePayloadType::None:
      break;
  }
  update.payload = nullptr;
  update.payloadType = UpdatePayloadType::None;
}

Update* getNext(Update* update) {
  return update != nullptr ? static_cast<Update*>(update->next) : nullptr;
}

Update* cloneUpdateNode(UpdateQueue& queue, const Update& source) {
  auto clone = std::make_unique<Update>();
  clone->lane = source.lane;
  clone->tag = source.tag;
  clone->payloadType = source.payloadType;
  if (source.payloadType == UpdatePayloadType::HostRoot && source.payload != nullptr) {
    auto* sourcePayload = static_cast<const HostRootUpdatePayload*>(source.payload);
    auto clonedPayload = std::make_unique<HostRootUpdatePayload>();
    if (sourcePayload->element != nullptr) {
      clonedPayload->element = std::make_unique<Value>(*sourcePayload->element);
    }
    clone->payload = clonedPayload.release();
  } else {
    clone->payload = source.payload;
  }
  clone->callback = source.callback;
  clone->next = nullptr;

  Update* clonePtr = clone.get();
  queue.ownedUpdates.push_back(std::move(clone));
  return clonePtr;
}

void pruneOwnedUpdates(UpdateQueue& queue) {
  std::vector<Update*> keep;
  for (Update* node = queue.firstBaseUpdate; node != nullptr; node = getNext(node)) {
    keep.push_back(node);
  }

  if (queue.shared != nullptr) {
    Update* const pending = static_cast<Update*>(queue.shared->pending);
    if (pending != nullptr) {
      Update* cursor = static_cast<Update*>(pending->next);
      if (cursor == nullptr) {
        keep.push_back(pending);
      } else {
        Update* start = cursor;
        do {
          keep.push_back(cursor);
          cursor = static_cast<Update*>(cursor->next);
        } while (cursor != nullptr && cursor != start);
      }
    }
  }

  auto matchesKeptNode = [&keep](const std::unique_ptr<Update>& ptr) {
    if (ptr == nullptr) {
      return false;
    }
    for (Update* candidate : keep) {
      if (candidate == ptr.get()) {
        return true;
      }
    }
    return false;
  };

  auto& owned = queue.ownedUpdates;
  auto write = owned.begin();
  for (auto read = owned.begin(); read != owned.end(); ++read) {
    if ((*read) != nullptr && matchesKeptNode(*read)) {
      if (write != read) {
        *write = std::move(*read);
      }
      ++write;
    }
  }
  owned.erase(write, owned.end());
}

UpdateQueue* cloneUpdateQueueInternal(const UpdateQueue& source) {
  auto* queue = new UpdateQueue();
  queue->baseState = source.baseState;
  queue->shared = source.shared;
  queue->callbacks.clear();

  Update* current = source.firstBaseUpdate;
  Update* previousClone = nullptr;
  while (current != nullptr) {
    auto clone = std::make_unique<Update>();
    clone->lane = current->lane;
    clone->tag = current->tag;
    clone->payloadType = current->payloadType;
    if (current->payloadType == UpdatePayloadType::HostRoot && current->payload != nullptr) {
      auto* sourcePayload = static_cast<const HostRootUpdatePayload*>(current->payload);
      auto clonedPayload = std::make_unique<HostRootUpdatePayload>();
      if (sourcePayload->element != nullptr) {
        clonedPayload->element = std::make_unique<Value>(*sourcePayload->element);
      }
      clone->payload = clonedPayload.release();
    } else {
      clone->payload = current->payload;
    }
    clone->callback = current->callback;
    clone->next = nullptr;

    Update* clonePtr = clone.get();
    queue->ownedUpdates.push_back(std::move(clone));

    if (previousClone == nullptr) {
      queue->firstBaseUpdate = clonePtr;
    } else {
      previousClone->next = clonePtr;
    }
    previousClone = clonePtr;

    current = getNext(current);
  }

  queue->lastBaseUpdate = previousClone;
  return queue;
}

UpdateQueue* createUpdateQueueInternal(FiberNode& fiber) {
  auto* queue = new UpdateQueue();
  queue->baseState = fiber.memoizedState;
  queue->firstBaseUpdate = nullptr;
  queue->lastBaseUpdate = nullptr;
  queue->shared = std::make_shared<SharedQueue>();
  return queue;
}

void appendUpdate(UpdateQueue& queue, Update* update) {
  if (queue.lastBaseUpdate == nullptr) {
    queue.firstBaseUpdate = update;
    queue.lastBaseUpdate = update;
  } else {
    queue.lastBaseUpdate->next = update;
    queue.lastBaseUpdate = update;
  }
}

} // namespace

std::unique_ptr<Update> createUpdate(Lane lane) {
  auto update = std::make_unique<Update>();
  update->lane = lane;
  update->tag = UpdateTag::UpdateState;
  update->payloadType = UpdatePayloadType::None;
  update->payload = nullptr;
  update->callback = nullptr;
  update->next = nullptr;
  return update;
}

void initializeUpdateQueue(FiberNode& fiber) {
  if (fiber.updateQueue != nullptr) {
    return;
  }

  fiber.updateQueue = createUpdateQueueInternal(fiber);
}

void cloneUpdateQueue(FiberNode& current, FiberNode& workInProgress) {
  auto* const currentQueue = static_cast<UpdateQueue*>(current.updateQueue);
  if (currentQueue == nullptr) {
    workInProgress.updateQueue = nullptr;
    return;
  }

  auto* const existingQueue = static_cast<UpdateQueue*>(workInProgress.updateQueue);
  if (existingQueue == currentQueue || existingQueue == nullptr) {
    workInProgress.updateQueue = cloneUpdateQueueInternal(*currentQueue);
  }
}

UpdateQueue& ensureUpdateQueue(FiberNode& fiber) {
  auto* queue = static_cast<UpdateQueue*>(fiber.updateQueue);
  if (queue != nullptr) {
    return *queue;
  }

  if (FiberNode* const current = fiber.alternate) {
    if (auto* const currentQueue = static_cast<UpdateQueue*>(current->updateQueue)) {
      queue = cloneUpdateQueueInternal(*currentQueue);
      fiber.updateQueue = queue;
      return *queue;
    }
  }

  queue = createUpdateQueueInternal(fiber);
  fiber.updateQueue = queue;
  return *queue;
}

FiberRoot* enqueueUpdate(
    FiberNode& fiber,
    std::unique_ptr<Update> update,
    Lane lane) {
  UpdateQueue& queue = ensureUpdateQueue(fiber);
  Update* const updatePtr = update.get();
  updatePtr->lane = lane;
  updatePtr->next = nullptr;
  queue.ownedUpdates.push_back(std::move(update));

  if (queue.shared == nullptr) {
    queue.shared = std::make_shared<SharedQueue>();
  }

  auto shared = queue.shared;
  Update* pending = static_cast<Update*>(shared->pending);
  if (pending == nullptr) {
    updatePtr->next = updatePtr;
  } else {
    updatePtr->next = pending->next;
    pending->next = updatePtr;
  }
  shared->pending = updatePtr;
  shared->lanes = mergeLanes(shared->lanes, lane);

  appendUpdate(queue, updatePtr);

  if (lane == NoLane) {
    return nullptr;
  }
  return enqueueConcurrentClassUpdate(
      &fiber,
      shared.get(),
      updatePtr,
      lane);
}

std::unique_ptr<Update> createRootErrorUpdate(
    FiberRoot& root,
    const CapturedValue& errorInfo,
    Lane lane) {
  auto update = std::make_unique<Update>();
  update->lane = lane;
  update->tag = UpdateTag::CaptureUpdate;
  update->payloadType = UpdatePayloadType::None;
  update->payload = nullptr;
  update->callback = [&root, captured = errorInfo]() mutable {
    logUncaughtError(root, captured);
  };
  update->next = nullptr;
  return update;
}

std::unique_ptr<Update> createClassErrorUpdate(Lane lane) {
  auto update = std::make_unique<Update>();
  update->lane = lane;
  update->tag = UpdateTag::CaptureUpdate;
  update->payloadType = UpdatePayloadType::None;
  update->payload = nullptr;
  update->callback = nullptr;
  update->next = nullptr;
  return update;
}

void initializeClassErrorUpdate(
    Update& update,
    FiberRoot& root,
    FiberNode& fiber,
    const CapturedValue& errorInfo) {
  update.payload = errorInfo.value;

  void* const instance = fiber.stateNode;
  update.callback = [instance, &root, &fiber, source = errorInfo.source, value = errorInfo.value, stack = errorInfo.stack]() {
    if (instance != nullptr) {
      markLegacyErrorBoundaryAsFailed(instance);
    }

    CapturedValue wrapped{value, source, stack};
    logCaughtError(root, fiber, wrapped);
  };
}

void enqueueCapturedUpdate(FiberNode& fiber, std::unique_ptr<Update> update) {
  UpdateQueue& queue = ensureUpdateQueue(fiber);
  Update* const updatePtr = update.get();
  updatePtr->next = nullptr;

  FiberNode* const current = fiber.alternate;
  if (current != nullptr) {
    if (auto* const currentQueue = static_cast<UpdateQueue*>(current->updateQueue)) {
      if (queue.shared == currentQueue->shared) {
        Update* newFirst = nullptr;
        Update* newLast = nullptr;
        std::vector<std::unique_ptr<Update>> clonedUpdates;
        Update* base = queue.firstBaseUpdate;
        while (base != nullptr) {
          auto clone = std::make_unique<Update>();
          clone->lane = base->lane;
          clone->tag = base->tag;
          clone->payloadType = base->payloadType;
          if (base->payloadType == UpdatePayloadType::HostRoot && base->payload != nullptr) {
            auto* sourcePayload = static_cast<const HostRootUpdatePayload*>(base->payload);
            auto clonedPayload = std::make_unique<HostRootUpdatePayload>();
            if (sourcePayload->element != nullptr) {
              clonedPayload->element = std::make_unique<Value>(*sourcePayload->element);
            }
            clone->payload = clonedPayload.release();
          } else {
            clone->payload = base->payload;
          }
          clone->callback = nullptr;
          clone->next = nullptr;

          Update* clonePtr = clone.get();
          clonedUpdates.push_back(std::move(clone));

          if (newLast == nullptr) {
            newFirst = clonePtr;
          } else {
            newLast->next = clonePtr;
          }
          newLast = clonePtr;

          base = getNext(base);
        }

        if (newLast == nullptr) {
          newFirst = updatePtr;
        } else {
          newLast->next = updatePtr;
        }
        newLast = updatePtr;

        queue.baseState = currentQueue->baseState;
        queue.firstBaseUpdate = newFirst;
        queue.lastBaseUpdate = newLast;
        queue.shared = currentQueue->shared;
        queue.callbacks = currentQueue->callbacks;

        queue.ownedUpdates.clear();
        for (auto& cloned : clonedUpdates) {
          queue.ownedUpdates.push_back(std::move(cloned));
        }
        queue.ownedUpdates.push_back(std::move(update));
        return;
      }
    }
  }

  queue.ownedUpdates.push_back(std::move(update));
  appendUpdate(queue, updatePtr);
}

namespace {

void* getStateFromUpdate(
    Runtime& /*jsRuntime*/,
    FiberNode& workInProgress,
    Update& update,
    void* prevState,
    const Value& /*props*/,
    const Value& /*instanceValue*/,
    AsyncActionState::UpdateQueueFlags& queueFlags) {
  if (update.payloadType == UpdatePayloadType::HostRoot) {
    auto* hostState = prevState != nullptr
        ? static_cast<HostRootMemoizedState*>(prevState)
        : new HostRootMemoizedState();
    auto* payload = static_cast<HostRootUpdatePayload*>(update.payload);
    if (payload != nullptr) {
      if (payload->element != nullptr) {
        hostState->element = std::make_unique<Value>(*payload->element);
      } else {
        hostState->element.reset();
      }
      destroyUpdatePayload(update);
    }
    return hostState;
  }

  switch (update.tag) {
    case UpdateTag::ReplaceState: {
      return update.payload != nullptr ? update.payload : nullptr;
    }
    case UpdateTag::CaptureUpdate: {
      workInProgress.flags = static_cast<FiberFlags>((workInProgress.flags & ~ShouldCapture) | DidCapture);
      [[fallthrough]];
    }
    case UpdateTag::UpdateState: {
      if (update.payload != nullptr) {
        return update.payload;
      }
      return prevState;
    }
    case UpdateTag::ForceUpdate: {
      queueFlags.hasForceUpdate = true;
      return prevState;
    }
  }
  return prevState;
}

} // namespace

void processUpdateQueue(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& workInProgress,
    const Value& props,
    const Value& instanceValue,
    Lanes renderLanes) {
  (void)instanceValue;

  auto* queue = static_cast<UpdateQueue*>(workInProgress.updateQueue);
  if (queue == nullptr) {
    return;
  }

  auto& queueFlags = runtime.asyncActionState().classUpdateQueueFlags;
  queueFlags.didReadFromEntangledAsyncAction = false;
  queueFlags.hasForceUpdate = false;
  queue->callbacks.clear();

  Update* firstBaseUpdate = queue->firstBaseUpdate;
  Update* lastBaseUpdate = queue->lastBaseUpdate;

  const auto shared = queue->shared;
  Update* pendingQueue = shared != nullptr ? static_cast<Update*>(shared->pending) : nullptr;
  if (pendingQueue != nullptr) {
    shared->pending = nullptr;

    Update* const lastPendingUpdate = pendingQueue;
    Update* firstPendingUpdate = static_cast<Update*>(lastPendingUpdate->next);
    if (firstPendingUpdate == nullptr) {
      firstPendingUpdate = lastPendingUpdate;
    }
    lastPendingUpdate->next = nullptr;

    if (lastBaseUpdate == nullptr) {
      firstBaseUpdate = firstPendingUpdate;
    } else {
      lastBaseUpdate->next = firstPendingUpdate;
    }
    lastBaseUpdate = lastPendingUpdate;

    if (FiberNode* const current = workInProgress.alternate) {
      if (auto* const currentQueue = static_cast<UpdateQueue*>(current->updateQueue)) {
        Update* const currentLastBaseUpdate = currentQueue->lastBaseUpdate;
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

  if (firstBaseUpdate == nullptr) {
    if (shared != nullptr) {
      shared->lanes = NoLanes;
    }
    workInProgress.lanes = NoLanes;
    workInProgress.memoizedState = queue->baseState;
    pruneOwnedUpdates(*queue);
    return;
  }

  void* newState = queue->baseState;
  Lanes newLanes = NoLanes;

  void* newBaseState = nullptr;
  Update* newFirstBaseUpdate = nullptr;
  Update* newLastBaseUpdate = nullptr;

  Update* update = firstBaseUpdate;
  while (update != nullptr) {
    const Lane strippedLane = removeLanes(update->lane, OffscreenLane);
    const bool isHiddenUpdate = strippedLane != update->lane;

    const bool shouldSkipUpdate = strippedLane != NoLane && (
        isHiddenUpdate
            ? !isSubsetOfLanes(getWorkInProgressRootRenderLanes(runtime), strippedLane)
            : !isSubsetOfLanes(renderLanes, strippedLane));

    if (shouldSkipUpdate) {
      Update* const clone = cloneUpdateNode(*queue, *update);
      clone->lane = strippedLane;
      clone->next = nullptr;

      if (newLastBaseUpdate == nullptr) {
        newFirstBaseUpdate = clone;
        newLastBaseUpdate = clone;
        newBaseState = newState;
      } else {
        newLastBaseUpdate->next = clone;
        newLastBaseUpdate = clone;
      }

      newLanes = mergeLanes(newLanes, strippedLane);
    } else {
      if (strippedLane != NoLane && strippedLane == peekEntangledActionLane(runtime)) {
        queueFlags.didReadFromEntangledAsyncAction = true;
      }

      if (newLastBaseUpdate != nullptr) {
        Update* const clone = cloneUpdateNode(*queue, *update);
        clone->lane = NoLane;
        clone->callback = nullptr;
        newLastBaseUpdate->next = clone;
        newLastBaseUpdate = clone;
      }

      newState = getStateFromUpdate(jsRuntime, workInProgress, *update, newState, props, instanceValue, queueFlags);

      if (update->callback) {
        queue->callbacks.push_back(update->callback);
        workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Callback);
        if (isHiddenUpdate) {
          workInProgress.flags = static_cast<FiberFlags>(workInProgress.flags | Visibility);
        }
      }
    }

    update = getNext(update);
    if (update == nullptr) {
      Update* const pending = shared != nullptr ? static_cast<Update*>(shared->pending) : nullptr;
      if (pending == nullptr) {
        break;
      }

      shared->pending = nullptr;
      Update* const lastPendingUpdate = pending;
      Update* firstPendingUpdate = static_cast<Update*>(lastPendingUpdate->next);
      if (firstPendingUpdate == nullptr) {
        firstPendingUpdate = lastPendingUpdate;
      }
      lastPendingUpdate->next = nullptr;
      update = firstPendingUpdate;
      queue->lastBaseUpdate = lastPendingUpdate;
    }
  }

  if (newLastBaseUpdate == nullptr) {
    newBaseState = newState;
  }

  queue->baseState = newBaseState;
  queue->firstBaseUpdate = newFirstBaseUpdate;
  queue->lastBaseUpdate = newLastBaseUpdate;

  if (shared != nullptr && newFirstBaseUpdate == nullptr) {
    shared->lanes = NoLanes;
  }

  markSkippedUpdateLanes(runtime, newLanes);
  workInProgress.lanes = newLanes;
  workInProgress.memoizedState = newState;

  pruneOwnedUpdates(*queue);
}

void suspendIfUpdateReadFromEntangledAsyncAction(ReactRuntime& runtime) {
  const auto& queueFlags = runtime.asyncActionState().classUpdateQueueFlags;
  if (!queueFlags.didReadFromEntangledAsyncAction) {
    return;
  }

  if (auto thenable = peekEntangledActionThenable(runtime)) {
    throw thenable;
  }
}

void resetHasForceUpdateBeforeProcessing(ReactRuntime& runtime) {
  runtime.asyncActionState().classUpdateQueueFlags.hasForceUpdate = false;
}

bool checkHasForceUpdateAfterProcessing(ReactRuntime& runtime) {
  return runtime.asyncActionState().classUpdateQueueFlags.hasForceUpdate;
}

namespace {

void callCallback(const std::function<void()>& callback) {
  if (!callback) {
    throw std::invalid_argument("Invalid callback provided to UpdateQueue. Expected callable.");
  }
  callback();
}

} // namespace

void deferHiddenCallbacks(UpdateQueue& queue) {
  if (queue.callbacks.empty()) {
    return;
  }

  if (queue.shared == nullptr) {
    queue.callbacks.clear();
    return;
  }

  auto& hiddenCallbacks = queue.shared->hiddenCallbacks;
  hiddenCallbacks.reserve(hiddenCallbacks.size() + queue.callbacks.size());
  for (auto& callback : queue.callbacks) {
    hiddenCallbacks.push_back(std::move(callback));
  }
  queue.callbacks.clear();
}

void commitHiddenCallbacks(UpdateQueue& queue) {
  if (queue.shared == nullptr || queue.shared->hiddenCallbacks.empty()) {
    return;
  }

  auto hiddenCallbacks = std::move(queue.shared->hiddenCallbacks);
  queue.shared->hiddenCallbacks.clear();
  for (auto& callback : hiddenCallbacks) {
    callCallback(callback);
  }
}

void commitCallbacks(UpdateQueue& queue) {
  if (queue.callbacks.empty()) {
    return;
  }

  auto callbacks = std::move(queue.callbacks);
  queue.callbacks.clear();
  for (auto& callback : callbacks) {
    callCallback(callback);
  }
}

Update::~Update() {
  destroyUpdatePayload(*this);
}

} // namespace react

