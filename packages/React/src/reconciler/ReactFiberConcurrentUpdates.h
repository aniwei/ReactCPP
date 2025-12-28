/**
 * React Fiber Concurrent Updates
 * 
 * 并发更新队列管理，用于处理在渲染过程中接收到的更新
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberConcurrentUpdates.js
 */

#pragma once

#include <memory>
#include <vector>
#include <any>

#include "ReactFiber.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"

namespace react::reconciler {

// =============================================================================
// ConcurrentUpdate 类型
// @source:32-35 ConcurrentUpdate
// =============================================================================

struct ConcurrentUpdate {
  std::shared_ptr<ConcurrentUpdate> next = nullptr;
  Lane lane = NoLane;
};

using ConcurrentUpdateRef = std::shared_ptr<ConcurrentUpdate>;

// =============================================================================
// ConcurrentQueue 类型
// @source:37-39 ConcurrentQueue
// =============================================================================

struct ConcurrentQueue {
  ConcurrentUpdateRef pending = nullptr;
};

using ConcurrentQueueRef = std::shared_ptr<ConcurrentQueue>;

// =============================================================================
// 并发更新队列状态
// @source:44-48
// =============================================================================

// 存储并发更新的数组
inline std::vector<std::any> concurrentQueues;
inline size_t concurrentQueuesIndex = 0;
inline Lanes concurrentlyUpdatedLanes = NoLanes;

// =============================================================================
// 工具函数
// @source:50-80
// =============================================================================

/**
 * 获取并发更新的 Lanes
 * @source:82-84 getConcurrentlyUpdatedLanes
 */
inline Lanes getConcurrentlyUpdatedLanes() {
  return concurrentlyUpdatedLanes;
}

/**
 * 将更新添加到并发队列
 * @source:86-95 enqueueUpdate (internal)
 */
inline void enqueueConcurrentUpdate(
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

/**
 * 标记从 Fiber 到根的更新 Lane
 * @source:200-250 markUpdateLaneFromFiberToRoot
 */
inline FiberRootRef markUpdateLaneFromFiberToRoot(
  FiberRef sourceFiber,
  ConcurrentUpdateRef update,
  Lane lane
) {
  // 更新 Fiber 的 lanes
  sourceFiber->lanes = mergeLanes(sourceFiber->lanes, lane);
  
  auto alternate = sourceFiber->alternate.lock();
  if (alternate) {
    alternate->lanes = mergeLanes(alternate->lanes, lane);
  }
  
  // 向上遍历更新 childLanes
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
  
  // 到达根节点
  if (node->tag == HostRoot) {
    return std::any_cast<FiberRootRef>(node->stateNode);
  }
  
  return nullptr;
}

/**
 * 完成并发更新队列的处理
 * @source:50-80 finishQueueingConcurrentUpdates
 */
inline void finishQueueingConcurrentUpdates() {
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
        // 第一个更新，创建循环链表
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

/**
 * 检查是否正在渲染中
 * @source ReactFiberWorkLoop.js
 */
inline bool isRenderInProgress() {
  // 简化实现 - 实际需要从 work loop 获取状态
  return false;
}

/**
 * 排队并发 Hook 更新
 */
template<typename S, typename A>
inline FiberRootRef enqueueConcurrentHookUpdate(
  FiberRef fiber,
  std::shared_ptr<void> queue,
  std::shared_ptr<void> update,
  Lane lane
) {
  auto concurrentQueue = std::make_shared<ConcurrentQueue>();
  auto concurrentUpdate = std::make_shared<ConcurrentUpdate>();
  concurrentUpdate->lane = lane;
  
  enqueueConcurrentUpdate(fiber, concurrentQueue, concurrentUpdate, lane);
  
  return markUpdateLaneFromFiberToRoot(fiber, concurrentUpdate, lane);
}

/**
 * 排队并发 Class 更新
 */
inline FiberRootRef enqueueConcurrentClassUpdate(
  FiberRef fiber,
  std::shared_ptr<void> queue,
  std::shared_ptr<void> update,
  Lane lane
) {
  auto concurrentQueue = std::make_shared<ConcurrentQueue>();
  auto concurrentUpdate = std::make_shared<ConcurrentUpdate>();
  concurrentUpdate->lane = lane;
  
  enqueueConcurrentUpdate(fiber, concurrentQueue, concurrentUpdate, lane);
  
  return markUpdateLaneFromFiberToRoot(fiber, concurrentUpdate, lane);
}

/**
 * 不安全地标记更新 Lane 从 Fiber 到根
 * (用于遗留模式)
 */
inline FiberRootRef unsafe_markUpdateLaneFromFiberToRoot(
  FiberRef sourceFiber,
  Lane lane
) {
  return markUpdateLaneFromFiberToRoot(sourceFiber, nullptr, lane);
}

} // namespace react::reconciler
