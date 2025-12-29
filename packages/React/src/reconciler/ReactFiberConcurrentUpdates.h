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

namespace facebook::jsi {
class Runtime;
} // namespace facebook::jsi

namespace react {
class ReactHostRuntime;
} // namespace react

namespace react::reconciler {


// ConcurrentUpdate 类型
// @source:32-35 ConcurrentUpdate


struct ConcurrentUpdate {
  std::shared_ptr<ConcurrentUpdate> next = nullptr;
  Lane lane = NoLane;
};

using ConcurrentUpdateRef = std::shared_ptr<ConcurrentUpdate>;


// ConcurrentQueue 类型
// @source:37-39 ConcurrentQueue


struct ConcurrentQueue {
  ConcurrentUpdateRef pending = nullptr;
};

using ConcurrentQueueRef = std::shared_ptr<ConcurrentQueue>;


/**
 * 获取并发更新的 Lanes
 * @source:82-84 getConcurrentlyUpdatedLanes
 */
Lanes getConcurrentlyUpdatedLanes();

/**
 * 将更新添加到并发队列
 * @source:86-95 enqueueUpdate (internal)
 */
void enqueueConcurrentUpdate(
  FiberRef fiber,
  ConcurrentQueueRef queue,
  ConcurrentUpdateRef update,
  Lane lane
);

/**
 * 标记从 Fiber 到根的更新 Lane
 * @source:200-250 markUpdateLaneFromFiberToRoot
 */
FiberRootRef markUpdateLaneFromFiberToRoot(
  FiberRef sourceFiber,
  ConcurrentUpdateRef update,
  Lane lane
);

/**
 * 完成并发更新队列的处理
 * @source:50-80 finishQueueingConcurrentUpdates
 */
void finishQueueingConcurrentUpdates();

/**
 * 检查是否正在渲染中
 * @source ReactFiberWorkLoop.js
 */
bool isRenderInProgress(facebook::jsi::Runtime& jsiRuntime, react::ReactHostRuntime& hostRuntime);

/**
 * 排队并发 Hook 更新
 */
template<typename S, typename A>
inline FiberRootRef enqueueConcurrentHookUpdate(
  FiberRef fiber,
  std::shared_ptr<void> /*queue*/, 
  std::shared_ptr<void> /*update*/,
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
FiberRootRef enqueueConcurrentClassUpdate(
  FiberRef fiber,
  std::shared_ptr<void> queue,
  std::shared_ptr<void> update,
  Lane lane
);

/**
 * 不安全地标记更新 Lane 从 Fiber 到根
 * (用于遗留模式)
 */
FiberRootRef unsafe_markUpdateLaneFromFiberToRoot(
  FiberRef sourceFiber,
  Lane lane
);

} // namespace react::reconciler
