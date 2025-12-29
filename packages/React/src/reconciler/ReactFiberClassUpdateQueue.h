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
 */

#pragma once

#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <variant>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactTypeOfMode.h"

namespace facebook::jsi {
class Runtime;
class Value;
} // namespace facebook::jsi

namespace react {
class ReactHostRuntime;
} // namespace react

namespace react::reconciler {

// 前向声明（避免循环依赖）
void markSkippedUpdateLanes(
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  Lanes lanes);

Lanes getWorkInProgressRootRenderLanes(
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime);

bool isUnsafeClassRenderPhaseUpdate(
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  const FiberRef& fiber);

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

// 更新类型 (ClassUpdate Type)
struct ClassUpdate {
  Lane lane{NoLane};
  UpdateTag tag{UpdateTag::UpdateState};
  facebook::jsi::Value payload = facebook::jsi::Value::undefined();
  std::function<void()> callback;
  std::shared_ptr<ClassUpdate> next;

  ClassUpdate() = default;
  explicit ClassUpdate(Lane l) : lane(l) {}
  ClassUpdate(Lane l, UpdateTag t) : lane(l), tag(t) {}
};

// 共享队列 (Shared Queue)
// @source ReactFiberClassUpdateQueue.js:140-144
struct SharedQueue {
  // 待处理更新（循环链表）
  std::shared_ptr<ClassUpdate> pending;
  
  // 队列中所有更新的 lanes
  Lanes lanes{NoLanes};
  
  // 隐藏组件的延迟回调
  std::vector<std::function<void()>> hiddenCallbacks;
};


// 更新队列 (Update Queue)
// ReactFiber.h 中前向声明为 struct UpdateQueue，这里给出完整定义。
struct UpdateQueue {
  // 应用第一个更新前的基础状态
  facebook::jsi::Value baseState = facebook::jsi::Value::undefined();
  
  // 基础更新链表的第一个
  std::shared_ptr<ClassUpdate> firstBaseUpdate;
  
  // 基础更新链表的最后一个
  std::shared_ptr<ClassUpdate> lastBaseUpdate;
  
  // current 和 work-in-progress 之间共享的队列
  std::shared_ptr<SharedQueue> shared;
  
  // 更新完成后需要执行的回调
  std::vector<std::function<void()>> callbacks;
  
  UpdateQueue() : shared(std::make_shared<SharedQueue>()) {}
};

// 全局状态
// 全局状态 - 在 processUpdateQueue 开始时重置
class ClassUpdateQueueGlobals {
public:
  bool hasForceUpdate{false};
  
  // DEV 模式下的当前处理队列
  std::shared_ptr<SharedQueue> currentlyProcessingQueue;
  
  // 是否读取了纠缠的异步 action
  bool didReadFromEntangledAsyncAction{false};
  
  void reset();
  void resetCurrentlyProcessingQueue();
};


// 初始化更新队列
// 初始化 Fiber 的更新队列
void initializeClassUpdateQueue(const FiberRef& fiber);


// 克隆更新队列
// @source ReactFiberClassUpdateQueue.js:190-209

// 从 current 克隆更新队列到 work-in-progress
// jsi::Value 是 move-only；克隆队列时需要 Runtime 以便复制/克隆 Value。
void cloneClassUpdateQueue(
  facebook::jsi::Runtime& jsiRuntime,
  const FiberRef& current,
  const FiberRef& workInProgress);


// 创建更新
/**
 * 创建新的更新对象
 */
std::shared_ptr<ClassUpdate> createClassUpdate(Lane lane);

// 获取更新队列
// 获取 Fiber 的更新队列
std::shared_ptr<UpdateQueue> getClassUpdateQueue(const FiberRef& fiber);

// 入队更新
// @source ReactFiberClassUpdateQueue.js:226-275


/**
 * 将更新加入队列
 * @returns FiberRootRef 或 nullptr
 */
FiberRootRef enqueueClassUpdate(
  FiberRef fiber,
  std::shared_ptr<ClassUpdate> update,
  Lane lane);


// Transition 纠缠
// 纠缠 Transition lanes
void entangleTransitionsForClassUpdate(
  const FiberRootRef& root,
  const FiberRef& fiber,
  Lane lane);


// 入队捕获的更新
// 入队捕获的更新（错误边界使用）
// 捕获的更新是在渲染阶段由子组件抛出的更新
void enqueueClassCapturedUpdate(
  const FiberRef& workInProgress,
  std::shared_ptr<ClassUpdate> capturedUpdate
);


// 从更新获取状态
// @source ReactFiberClassUpdateQueue.js:379-461


/**
 * 从更新计算新状态
 */
facebook::jsi::Value getStateFromClassUpdate(
  FiberRef workInProgress,
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  std::shared_ptr<UpdateQueue> queue,
  std::shared_ptr<ClassUpdate> update,
  const facebook::jsi::Value& prevState,
  const facebook::jsi::Value& nextProps,
  const facebook::jsi::Value& instance
);


// 处理更新队列
// @source ReactFiberClassUpdateQueue.js:481-599


/**
 * 处理更新队列，计算最终状态
 */
void processClassUpdateQueue(
  facebook::jsi::Runtime& jsiRuntime,
  ReactHostRuntime& hostRuntime,
  FiberRef workInProgress,
  const facebook::jsi::Value& props,
  const facebook::jsi::Value& instance,
  Lanes renderLanes
);


// 回调辅助函数
// @source ReactFiberClassUpdateQueue.js:701-711


/**
 * 调用回调
 */
void callClassUpdateCallback(std::function<void()> callback);


// ForceUpdate 相关
// @source ReactFiberClassUpdateQueue.js:713-720


/**
 * 重置 hasForceUpdate 标志
 */
void resetHasForceUpdateBeforeProcessing(ReactHostRuntime& hostRuntime);

/**
 * 检查处理后是否有 force update
 */
bool checkHasForceUpdateAfterProcessing(ReactHostRuntime& hostRuntime);


// 隐藏回调处理
// @source ReactFiberClassUpdateQueue.js:722-738


/**
 * 延迟隐藏组件的回调
 */
void deferHiddenClassCallbacks(std::shared_ptr<UpdateQueue> updateQueue);

/**
 * 提交隐藏的回调
 */
void commitHiddenClassCallbacks(
  std::shared_ptr<UpdateQueue> updateQueue
);

/**
 * 提交回调
 */
void commitClassCallbacks(
  std::shared_ptr<UpdateQueue> updateQueue
);


// 便捷函数


/**
 * 检查是否有待处理更新
 */
bool hasClassPendingUpdates(FiberRef fiber);

/**
 * 创建并入队状态更新
 */
FiberRootRef scheduleClassUpdateOnFiber(
  facebook::jsi::Runtime& jsiRuntime,
  FiberRef fiber,
  const facebook::jsi::Value& payload,
  Lane lane
);

/**
 * 创建并入队 force update
 */
FiberRootRef scheduleClassForceUpdateOnFiber(FiberRef fiber, Lane lane);

} // namespace react::reconciler
