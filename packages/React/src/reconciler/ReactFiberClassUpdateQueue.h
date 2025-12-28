/**
 * React Fiber Class Update Queue
 * 
 * ClassUpdateQueue 是一个优先级更新的链表。
 * 
 * 与 Fiber 一样，更新队列成对出现：current 队列表示屏幕的可见状态，
 * work-in-progress 队列可以在提交前进行异步变更和处理——一种双缓冲形式。
 * 
 * 两个队列共享一个持久的单向链表结构。调度更新时，我们将其追加到两个队列的末尾。
 * 每个队列维护一个指向持久列表中第一个未处理更新的指针。
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberClassUpdateQueue.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>
#include <vector>
#include <variant>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactTypeOfMode.h"

namespace react::reconciler {

// 前向声明（避免循环依赖）
inline void markSkippedUpdateLanes(Lanes lanes);
inline Lanes getWorkInProgressRootRenderLanes();
inline bool isUnsafeClassRenderPhaseUpdate(FiberRef fiber);

// =============================================================================
// 更新标签 (Update Tags)
// @source ReactFiberClassUpdateQueue.js:155-158
// =============================================================================

enum class UpdateTag : uint8_t {
  UpdateState   = 0,   // setState 调用
  ReplaceState  = 1,   // replaceState 调用 (已废弃)
  ForceUpdate   = 2,   // forceUpdate 调用
  CaptureUpdate = 3  // 错误捕获更新
};

// 为了向后兼容的常量
inline constexpr uint8_t UpdateStateTag = 0;
inline constexpr uint8_t ReplaceStateTag = 1;
inline constexpr uint8_t ForceUpdateTag = 2;
inline constexpr uint8_t CaptureUpdateTag = 3;

// =============================================================================
// 更新类型 (ClassUpdate Type)
// @source ReactFiberClassUpdateQueue.js:127-138
// =============================================================================

/**
 * ClassUpdate 结构
 * 表示单个状态更新
 */
template<typename State>
struct ClassUpdate {
  Lane lane{NoLane};
  
  UpdateTag tag{UpdateTag::UpdateState};
  
  // payload 可以是新状态值或更新函数
  std::any payload;
  
  // 更新完成后的回调
  std::function<void()> callback;
  
  // 链表下一个节点
  std::shared_ptr<ClassUpdate<State>> next;
  
  ClassUpdate() = default;
  
  ClassUpdate(Lane l) : lane(l) {}
  
  ClassUpdate(Lane l, UpdateTag t) : lane(l), tag(t) {}
};

// =============================================================================
// 共享队列 (Shared Queue)
// @source ReactFiberClassUpdateQueue.js:140-144
// =============================================================================

/**
 * ClassSharedQueue - 在 current 和 work-in-progress 之间共享
 */
template<typename State>
struct ClassSharedQueue {
  // 待处理更新（循环链表）
  std::shared_ptr<ClassUpdate<State>> pending;
  
  // 队列中所有更新的 lanes
  Lanes lanes{NoLanes};
  
  // 隐藏组件的延迟回调
  std::vector<std::function<void()>> hiddenCallbacks;
};

// =============================================================================
// 更新队列 (Update Queue)
// @source ReactFiberClassUpdateQueue.js:146-152
// =============================================================================

/**
 * ClassUpdateQueue - 完整的更新队列结构
 */
template<typename State>
struct ClassUpdateQueue {
  // 应用第一个更新前的基础状态
  State baseState;
  
  // 基础更新链表的第一个
  std::shared_ptr<ClassUpdate<State>> firstBaseUpdate;
  
  // 基础更新链表的最后一个
  std::shared_ptr<ClassUpdate<State>> lastBaseUpdate;
  
  // current 和 work-in-progress 之间共享的队列
  std::shared_ptr<ClassSharedQueue<State>> shared;
  
  // 更新完成后需要执行的回调
  std::vector<std::function<void()>> callbacks;
  
  ClassUpdateQueue() : shared(std::make_shared<ClassSharedQueue<State>>()) {}
};

// 使用 std::any 作为泛型状态的默认类型
using AnyClassUpdate = ClassUpdate<std::any>;
using AnyClassSharedQueue = ClassSharedQueue<std::any>;
using AnyClassUpdateQueue = ClassUpdateQueue<std::any>;

// =============================================================================
// 全局状态
// @source ReactFiberClassUpdateQueue.js:160-162
// =============================================================================

/**
 * 全局状态 - 在 processUpdateQueue 开始时重置
 */
class ClassUpdateQueueGlobals {
public:
  static ClassUpdateQueueGlobals& instance() {
    static ClassUpdateQueueGlobals inst;
    return inst;
  }
  
  bool hasForceUpdate{false};
  
  // DEV 模式下的当前处理队列
  std::shared_ptr<AnyClassSharedQueue> currentlyProcessingQueue;
  
  // 是否读取了纠缠的异步 action
  bool didReadFromEntangledAsyncAction{false};
  
  void reset() {
    hasForceUpdate = false;
    didReadFromEntangledAsyncAction = false;
  }
  
  void resetCurrentlyProcessingQueue() {
    currentlyProcessingQueue = nullptr;
  }

private:
  ClassUpdateQueueGlobals() = default;
};

// =============================================================================
// 初始化更新队列
// @source ReactFiberClassUpdateQueue.js:175-188
// =============================================================================

/**
 * 初始化 Fiber 的更新队列
 */
inline void initializeClassUpdateQueue(FiberRef fiber) {
  auto queue = std::make_shared<AnyClassUpdateQueue>();
  queue->baseState = fiber->memoizedState;
  queue->firstBaseUpdate = nullptr;
  queue->lastBaseUpdate = nullptr;
  queue->shared = std::make_shared<AnyClassSharedQueue>();
  queue->callbacks.clear();
  
  fiber->updateQueue = std::make_any<std::shared_ptr<AnyClassUpdateQueue>>(queue);
}

// =============================================================================
// 克隆更新队列
// @source ReactFiberClassUpdateQueue.js:190-209
// =============================================================================

/**
 * 从 current 克隆更新队列到 work-in-progress
 * 除非它已经是一个克隆
 */
inline void cloneClassUpdateQueue(FiberRef current, FiberRef workInProgress) {
  if (!workInProgress->updateQueue.has_value() || !current->updateQueue.has_value()) {
    return;
  }
  
  std::shared_ptr<AnyClassUpdateQueue> queue;
  std::shared_ptr<AnyClassUpdateQueue> currentQueue;
  
  try {
    queue = std::any_cast<std::shared_ptr<AnyClassUpdateQueue>>(workInProgress->updateQueue);
    currentQueue = std::any_cast<std::shared_ptr<AnyClassUpdateQueue>>(current->updateQueue);
  } catch (const std::bad_any_cast&) {
    return;
  }
  
  if (queue == currentQueue) {
    // 需要克隆
    auto clone = std::make_shared<AnyClassUpdateQueue>();
    clone->baseState = currentQueue->baseState;
    clone->firstBaseUpdate = currentQueue->firstBaseUpdate;
    clone->lastBaseUpdate = currentQueue->lastBaseUpdate;
    clone->shared = currentQueue->shared;  // 共享部分不克隆
    clone->callbacks.clear();
    
    workInProgress->updateQueue = std::make_any<std::shared_ptr<AnyClassUpdateQueue>>(clone);
  }
}

// =============================================================================
// 创建更新
// @source ReactFiberClassUpdateQueue.js:211-224
// =============================================================================

/**
 * 创建新的更新对象
 */
inline std::shared_ptr<AnyClassUpdate> createClassUpdate(Lane lane) {
  auto update = std::make_shared<AnyClassUpdate>();
  update->lane = lane;
  update->tag = UpdateTag::UpdateState;
  update->payload = std::any{};
  update->callback = nullptr;
  update->next = nullptr;
  return update;
}

// =============================================================================
// 获取更新队列
// =============================================================================

/**
 * 获取 Fiber 的更新队列
 */
inline std::shared_ptr<AnyClassUpdateQueue> getClassUpdateQueue(FiberRef fiber) {
  if (!fiber->updateQueue.has_value()) {
    return nullptr;
  }
  
  try {
    return std::any_cast<std::shared_ptr<AnyClassUpdateQueue>>(fiber->updateQueue);
  } catch (const std::bad_any_cast&) {
    return nullptr;
  }
}

// =============================================================================
// 入队更新
// @source ReactFiberClassUpdateQueue.js:226-275
// =============================================================================

/**
 * 将更新加入队列
 * @returns FiberRootRef 或 nullptr
 */
inline FiberRootRef enqueueClassUpdate(
  FiberRef fiber,
  std::shared_ptr<AnyClassUpdate> update,
  Lane lane
) {
  auto updateQueue = getClassUpdateQueue(fiber);
  if (!updateQueue) {
    return nullptr;
  }
  
  auto sharedQueue = updateQueue->shared;
  
  // 简化实现：直接添加到 pending 循环链表
  auto pending = sharedQueue->pending;
  if (pending == nullptr) {
    // 第一个更新，创建循环链表
    update->next = update;
  } else {
    update->next = pending->next;
    pending->next = update;
  }
  sharedQueue->pending = update;
  
  // 更新 lanes
  sharedQueue->lanes = mergeLanes(sharedQueue->lanes, lane);
  
  // 简化：返回 nullptr（完整实现需要遍历到 root）
  return nullptr;
}

// =============================================================================
// Transition 纠缠
// @source ReactFiberClassUpdateQueue.js:277-301
// =============================================================================

/**
 * 纠缠 Transition lanes
 */
inline void entangleTransitionsForClassUpdate(
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
    
    // 如果任何纠缠的 lanes 不再在 root 上待处理，它们必须已完成
    queueLanes = intersectLanes(queueLanes, root->pendingLanes);
    
    // 将新的 transition lane 与其他 transition lanes 纠缠
    Lanes newQueueLanes = mergeLanes(queueLanes, lane);
    sharedQueue->lanes = newQueueLanes;
  }
}

// =============================================================================
// 入队捕获的更新
// @source ReactFiberClassUpdateQueue.js:303-377
// =============================================================================

/**
 * 入队捕获的更新（错误边界使用）
 * 捕获的更新是在渲染阶段由子组件抛出的更新
 */
inline void enqueueClassCapturedUpdate(
  FiberRef workInProgress,
  std::shared_ptr<AnyClassUpdate> capturedUpdate
) {
  auto queue = getClassUpdateQueue(workInProgress);
  if (!queue) {
    return;
  }
  
  // 追加到列表末尾
  auto lastBaseUpdate = queue->lastBaseUpdate;
  if (lastBaseUpdate == nullptr) {
    queue->firstBaseUpdate = capturedUpdate;
  } else {
    lastBaseUpdate->next = capturedUpdate;
  }
  queue->lastBaseUpdate = capturedUpdate;
}

// =============================================================================
// 从更新获取状态
// @source ReactFiberClassUpdateQueue.js:379-461
// =============================================================================

/**
 * 从更新计算新状态
 */
inline std::any getStateFromClassUpdate(
  FiberRef workInProgress,
  std::shared_ptr<AnyClassUpdateQueue> queue,
  std::shared_ptr<AnyClassUpdate> update,
  std::any prevState,
  std::any nextProps,
  std::any instance
) {
  auto& globals = ClassUpdateQueueGlobals::instance();
  
  switch (update->tag) {
    case UpdateTag::ReplaceState: {
      // payload 是新状态
      if (update->payload.has_value()) {
        return update->payload;
      }
      return prevState;
    }
    
    case UpdateTag::CaptureUpdate: {
      // 设置 DidCapture flag
      workInProgress->flags = 
        (workInProgress->flags & ~ShouldCapture) | DidCapture;
      // 故意穿透到 UpdateState
      [[fallthrough]];
    }
    
    case UpdateTag::UpdateState: {
      if (!update->payload.has_value()) {
        return prevState;
      }
      return update->payload;
    }
    
    case UpdateTag::ForceUpdate: {
      globals.hasForceUpdate = true;
      return prevState;
    }
  }
  
  return prevState;
}

// =============================================================================
// 处理更新队列
// @source ReactFiberClassUpdateQueue.js:481-599
// =============================================================================

/**
 * 处理更新队列，计算最终状态
 */
inline void processClassUpdateQueue(
  FiberRef workInProgress,
  std::any props,
  std::any instance,
  Lanes renderLanes
) {
  auto& globals = ClassUpdateQueueGlobals::instance();
  globals.didReadFromEntangledAsyncAction = false;
  
  auto queue = getClassUpdateQueue(workInProgress);
  if (!queue) {
    return;
  }
  
  globals.hasForceUpdate = false;
  
  auto firstBaseUpdate = queue->firstBaseUpdate;
  auto lastBaseUpdate = queue->lastBaseUpdate;
  
  // 检查是否有待处理更新，如果有，转移到基础队列
  auto pendingQueue = queue->shared->pending;
  if (pendingQueue != nullptr) {
    queue->shared->pending = nullptr;
    
    // 待处理队列是循环的。断开 first 和 last 之间的连接
    auto lastPendingUpdate = pendingQueue;
    auto firstPendingUpdate = lastPendingUpdate->next;
    lastPendingUpdate->next = nullptr;
    
    // 追加待处理更新到基础队列
    if (lastBaseUpdate == nullptr) {
      firstBaseUpdate = firstPendingUpdate;
    } else {
      lastBaseUpdate->next = firstPendingUpdate;
    }
    lastBaseUpdate = lastPendingUpdate;
    
    // 更新 alternate 的队列
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
  
  // 处理更新
  if (firstBaseUpdate != nullptr) {
    std::any newState = queue->baseState;
    Lanes newLanes = NoLanes;
    
    std::any newBaseState{};
    bool hasNewBaseState = false;
    std::shared_ptr<AnyClassUpdate> newFirstBaseUpdate = nullptr;
    std::shared_ptr<AnyClassUpdate> newLastBaseUpdate = nullptr;
    
    auto update = firstBaseUpdate;
    do {
      Lane updateLane = removeLanes(update->lane, OffscreenLane);
      
      // 检查是否应跳过此更新
      bool shouldSkipUpdate = !isSubsetOfLanes(renderLanes, updateLane);
      
      if (shouldSkipUpdate) {
        // 优先级不足，跳过此更新
        auto clone = std::make_shared<AnyClassUpdate>();
        clone->lane = updateLane;
        clone->tag = update->tag;
        clone->payload = update->payload;
        clone->callback = update->callback;
        clone->next = nullptr;
        
        if (newLastBaseUpdate == nullptr) {
          newFirstBaseUpdate = newLastBaseUpdate = clone;
          newBaseState = newState;
          hasNewBaseState = true;
        } else {
          newLastBaseUpdate->next = clone;
          newLastBaseUpdate = clone;
        }
        
        // 更新剩余优先级
        newLanes = mergeLanes(newLanes, updateLane);
      } else {
        // 此更新有足够的优先级
        
        if (newLastBaseUpdate != nullptr) {
          // 克隆更新以保持基础队列完整
          auto clone = std::make_shared<AnyClassUpdate>();
          clone->lane = NoLane;
          clone->tag = update->tag;
          clone->payload = update->payload;
          clone->callback = nullptr;
          clone->next = nullptr;
          
          newLastBaseUpdate->next = clone;
          newLastBaseUpdate = clone;
        }
        
        // 处理此更新
        newState = getStateFromClassUpdate(
          workInProgress,
          queue,
          update,
          newState,
          props,
          instance
        );
        
        // 处理回调
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
        } else {
          // 在 reducer 内部调度了更新，追加并继续处理
          auto lastPending = pendingQueue;
          auto firstPending = lastPending->next;
          lastPending->next = nullptr;
          update = firstPending;
          queue->lastBaseUpdate = lastPending;
          queue->shared->pending = nullptr;
        }
      }
    } while (true);
    
    if (newLastBaseUpdate == nullptr) {
      newBaseState = newState;
      hasNewBaseState = true;
    }
    
    if (hasNewBaseState) {
      queue->baseState = newBaseState;
    }
    queue->firstBaseUpdate = newFirstBaseUpdate;
    queue->lastBaseUpdate = newLastBaseUpdate;
    
    if (firstBaseUpdate == nullptr) {
      // 队列为空，重置 lanes
      queue->shared->lanes = NoLanes;
    }
    
    // 更新 fiber 状态
    workInProgress->lanes = newLanes;
    workInProgress->memoizedState = newState;
  }
}

// =============================================================================
// 回调辅助函数
// @source ReactFiberClassUpdateQueue.js:701-711
// =============================================================================

/**
 * 调用回调
 */
inline void callClassUpdateCallback(std::function<void()> callback, std::any context) {
  if (callback) {
    callback();
  }
}

// =============================================================================
// ForceUpdate 相关
// @source ReactFiberClassUpdateQueue.js:713-720
// =============================================================================

/**
 * 重置 hasForceUpdate 标志
 */
inline void resetHasForceUpdateBeforeProcessing() {
  ClassUpdateQueueGlobals::instance().hasForceUpdate = false;
}

/**
 * 检查处理后是否有 force update
 */
inline bool checkHasForceUpdateAfterProcessing() {
  return ClassUpdateQueueGlobals::instance().hasForceUpdate;
}

// =============================================================================
// 隐藏回调处理
// @source ReactFiberClassUpdateQueue.js:722-738
// =============================================================================

/**
 * 延迟隐藏组件的回调
 */
inline void deferHiddenClassCallbacks(std::shared_ptr<AnyClassUpdateQueue> updateQueue) {
  if (updateQueue->callbacks.empty()) {
    return;
  }
  
  // 将回调移动到共享队列的隐藏回调中
  auto& hiddenCallbacks = updateQueue->shared->hiddenCallbacks;
  hiddenCallbacks.insert(
    hiddenCallbacks.end(),
    updateQueue->callbacks.begin(),
    updateQueue->callbacks.end()
  );
  updateQueue->callbacks.clear();
}

/**
 * 提交隐藏的回调
 */
inline void commitHiddenClassCallbacks(
  std::shared_ptr<AnyClassUpdateQueue> updateQueue,
  std::any context
) {
  auto& hiddenCallbacks = updateQueue->shared->hiddenCallbacks;
  if (hiddenCallbacks.empty()) {
    return;
  }
  
  for (auto& callback : hiddenCallbacks) {
    callClassUpdateCallback(callback, context);
  }
  hiddenCallbacks.clear();
}

/**
 * 提交回调
 */
inline void commitClassCallbacks(
  std::shared_ptr<AnyClassUpdateQueue> updateQueue,
  std::any context
) {
  if (updateQueue->callbacks.empty()) {
    return;
  }
  
  for (auto& callback : updateQueue->callbacks) {
    callClassUpdateCallback(callback, context);
  }
  updateQueue->callbacks.clear();
}

// =============================================================================
// 便捷函数
// =============================================================================

/**
 * 检查是否有待处理更新
 */
inline bool hasClassPendingUpdates(FiberRef fiber) {
  auto queue = getClassUpdateQueue(fiber);
  if (!queue) {
    return false;
  }
  return queue->shared->pending != nullptr || queue->firstBaseUpdate != nullptr;
}

/**
 * 创建并入队状态更新
 */
inline FiberRootRef scheduleClassUpdateOnFiber(
  FiberRef fiber,
  std::any payload,
  Lane lane
) {
  auto update = createClassUpdate(lane);
  update->payload = payload;
  return enqueueClassUpdate(fiber, update, lane);
}

/**
 * 创建并入队 force update
 */
inline FiberRootRef scheduleClassForceUpdateOnFiber(FiberRef fiber, Lane lane) {
  auto update = createClassUpdate(lane);
  update->tag = UpdateTag::ForceUpdate;
  return enqueueClassUpdate(fiber, update, lane);
}

} // namespace react::reconciler
