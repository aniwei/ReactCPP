/**
 * React Fiber Concurrent Updates
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberConcurrentUpdates.js
 */

#include "ReactFiberConcurrentUpdates.h"

#include "ReactFiberWorkLoop.h"

namespace react::reconciler {
namespace {

// 存储并发更新的数组
std::vector<std::any> concurrentQueues;
size_t concurrentQueuesIndex = 0;
Lanes concurrentlyUpdatedLanes = NoLanes;

} // namespace

Lanes getConcurrentlyUpdatedLanes() {
  return concurrentlyUpdatedLanes;
}

void enqueueConcurrentUpdate(
  FiberRef fiber,
  ConcurrentQueueRef queue,
  ConcurrentUpdateRef update,
  Lane lane
) {
  concurrentQueues.push_back(fiber);
  concurrentQueues.push_back(queue);
  concurrentQueues.push_back(update);
  concurrentQueues.push_back(lane);
  concurrentQueuesIndex += 4;

  concurrentlyUpdatedLanes = mergeLanes(concurrentlyUpdatedLanes, lane);
}

FiberRootRef markUpdateLaneFromFiberToRoot(
  FiberRef sourceFiber,
  ConcurrentUpdateRef update,
  Lane lane
) {
  (void)update;

  sourceFiber->lanes = mergeLanes(sourceFiber->lanes, lane);

  auto alternate = sourceFiber->alternate.lock();
  if (alternate) {
    alternate->lanes = mergeLanes(alternate->lanes, lane);
  }

  FiberRef node = sourceFiber;
  FiberRef parent = node->return_.lock();

  while (parent) {
    parent->childLanes = mergeLanes(parent->childLanes, lane);
    alternate = parent->alternate.lock();
    if (alternate) {
      alternate->childLanes = mergeLanes(alternate->childLanes, lane);
    }
    node = parent;
    parent = node->return_.lock();
  }

  if (node->tag == HostRoot) {
      auto* rootPtr = std::get_if<FiberRootRef>(&node->stateNode);
    return rootPtr ? *rootPtr : nullptr;
  }

  return nullptr;
}

void finishQueueingConcurrentUpdates() {
  size_t endIndex = concurrentQueuesIndex;
  concurrentQueuesIndex = 0;
  concurrentlyUpdatedLanes = NoLanes;

  size_t i = 0;
  while (i < endIndex) {
    FiberRef fiber;
    ConcurrentQueueRef queue;
    ConcurrentUpdateRef update;
    Lane lane = NoLane;

    try {
      fiber = std::any_cast<FiberRef>(concurrentQueues[i]);
      concurrentQueues[i++] = std::any();

      queue = std::any_cast<ConcurrentQueueRef>(concurrentQueues[i]);
      concurrentQueues[i++] = std::any();

      update = std::any_cast<ConcurrentUpdateRef>(concurrentQueues[i]);
      concurrentQueues[i++] = std::any();

      lane = std::any_cast<Lane>(concurrentQueues[i]);
      concurrentQueues[i++] = std::any();
    } catch (...) {
      i += 4 - (i % 4);
      continue;
    }

    if (queue && update) {
      auto pending = queue->pending;
      if (!pending) {
        update->next = update;
      } else {
        update->next = pending->next;
        pending->next = update;
      }
      queue->pending = update;
    }

    if (lane != NoLane && fiber) {
      markUpdateLaneFromFiberToRoot(fiber, update, lane);
    }
  }

  concurrentQueues.clear();
}

bool isRenderInProgress(facebook::jsi::Runtime& jsiRuntime, react::ReactHostRuntime& hostRuntime) {
  try {
    return getWorkLoop(jsiRuntime, hostRuntime).isRendering();
  } catch (...) {
    return false;
  }
}

FiberRootRef enqueueConcurrentClassUpdate(
  FiberRef fiber,
  std::shared_ptr<void> queue,
  std::shared_ptr<void> update,
  Lane lane
) {
  (void)queue;
  (void)update;

  auto concurrentQueue = std::make_shared<ConcurrentQueue>();
  auto concurrentUpdate = std::make_shared<ConcurrentUpdate>();
  concurrentUpdate->lane = lane;

  enqueueConcurrentUpdate(fiber, concurrentQueue, concurrentUpdate, lane);
  return markUpdateLaneFromFiberToRoot(fiber, concurrentUpdate, lane);
}

FiberRootRef unsafe_markUpdateLaneFromFiberToRoot(FiberRef sourceFiber, Lane lane) {
  return markUpdateLaneFromFiberToRoot(sourceFiber, nullptr, lane);
}

} // namespace react::reconciler
