/**
 * React Event Priorities
 * 
 * 事件优先级系统，将用户交互事件映射到对应的 Lane 优先级
 * 
 * @source reactjs/packages/react-reconciler/src/ReactEventPriorities.js
 */

#pragma once

#include "ReactFiberLane.h"

namespace react::reconciler {

// =============================================================================
// Event Priority 类型定义
// @source ReactEventPriorities.js:21
// =============================================================================

using EventPriority = Lane;

// =============================================================================
// Event Priority 常量
// @source ReactEventPriorities.js:23-27
// =============================================================================

// @source:23
constexpr EventPriority NoEventPriority = NoLane;

// @source:24 - 离散事件优先级 (如点击、按键)
constexpr EventPriority DiscreteEventPriority = SyncLane;

// @source:25 - 连续事件优先级 (如滚动、拖拽)
constexpr EventPriority ContinuousEventPriority = InputContinuousLane;

// @source:26 - 默认事件优先级
constexpr EventPriority DefaultEventPriority = DefaultLane;

// @source:27 - 空闲事件优先级
constexpr EventPriority IdleEventPriority = IdleLane;

// =============================================================================
// Event Priority 工具函数
// @source ReactEventPriorities.js:29-61
// =============================================================================

/**
 * 返回更高的事件优先级
 * @source:29-32 higherEventPriority
 */
inline constexpr EventPriority higherEventPriority(
  EventPriority a,
  EventPriority b
) {
  // 数值越小优先级越高
  return (a != 0 && a < b) ? a : b;
}

/**
 * 返回更低的事件优先级
 * @source:34-37 lowerEventPriority
 */
inline constexpr EventPriority lowerEventPriority(
  EventPriority a,
  EventPriority b
) {
  return (a == 0 || a > b) ? a : b;
}

/**
 * 检查 a 是否比 b 更高优先级
 * @source:39-42 isHigherEventPriority
 */
inline constexpr bool isHigherEventPriority(
  EventPriority a,
  EventPriority b
) {
  return a != 0 && a < b;
}

/**
 * 将事件优先级转换为 Lane
 * @source:44-46 eventPriorityToLane
 */
inline constexpr Lane eventPriorityToLane(EventPriority updatePriority) {
  return updatePriority;
}

/**
 * 将 Lanes 转换为事件优先级
 * @source:48-59 lanesToEventPriority
 */
inline EventPriority lanesToEventPriority(Lanes lanes) {
  Lane lane = getHighestPriorityLane(lanes);
  
  if (!isHigherEventPriority(DiscreteEventPriority, lane)) {
    return DiscreteEventPriority;
  }
  if (!isHigherEventPriority(ContinuousEventPriority, lane)) {
    return ContinuousEventPriority;
  }
  if (includesNonIdleWork(lane)) {
    return DefaultEventPriority;
  }
  return IdleEventPriority;
}

} // namespace react::reconciler
