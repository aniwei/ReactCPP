/**
 * React Fiber Lane 优先级系统
 * 
 * Lane 是 React Reconciler 使用的优先级模型
 * 使用位掩码来表示不同优先级的更新
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberLane.js
 */

#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace react::reconciler {

// 类型定义
using Lanes = uint32_t;
using Lane = uint32_t;

template<typename T>
using LaneMap = std::array<T, 31>;

// Lane 常量
constexpr int TotalLanes = 31;
constexpr Lanes NoLanes = /*                        */ 0b0000000000000000000000000000000;
constexpr Lane NoLane = /*                          */ 0b0000000000000000000000000000000;
constexpr Lane SyncHydrationLane = /*               */ 0b0000000000000000000000000000001;
constexpr Lane SyncLane = /*                        */ 0b0000000000000000000000000000010;
constexpr int SyncLaneIndex = 1;
constexpr Lane InputContinuousHydrationLane = /*    */ 0b0000000000000000000000000000100;
constexpr Lane InputContinuousLane = /*             */ 0b0000000000000000000000000001000;
constexpr Lane DefaultHydrationLane = /*            */ 0b0000000000000000000000000010000;
constexpr Lane DefaultLane = /*                     */ 0b0000000000000000000000000100000;
constexpr Lane SyncUpdateLanes = SyncLane | InputContinuousLane | DefaultLane;
constexpr Lane GestureLane = /*                     */ 0b0000000000000000000000001000000;
constexpr Lane TransitionHydrationLane = /*         */ 0b0000000000000000000000010000000;
constexpr Lanes TransitionLanes = /*                */ 0b0000000001111111111111100000000;
constexpr Lane TransitionLane1 = /*                 */ 0b0000000000000000000000100000000;
constexpr Lane TransitionLane2 = /*                 */ 0b0000000000000000000001000000000;
constexpr Lane TransitionLane3 = /*                 */ 0b0000000000000000000010000000000;
constexpr Lane TransitionLane4 = /*                 */ 0b0000000000000000000100000000000;
constexpr Lane TransitionLane5 = /*                 */ 0b0000000000000000001000000000000;
constexpr Lane TransitionLane6 = /*                 */ 0b0000000000000000010000000000000;
constexpr Lane TransitionLane7 = /*                 */ 0b0000000000000000100000000000000;
constexpr Lane TransitionLane8 = /*                 */ 0b0000000000000001000000000000000;
constexpr Lane TransitionLane9 = /*                 */ 0b0000000000000010000000000000000;
constexpr Lane TransitionLane10 = /*                */ 0b0000000000000100000000000000000;
constexpr Lane TransitionLane11 = /*                */ 0b0000000000001000000000000000000;
constexpr Lane TransitionLane12 = /*                */ 0b0000000000010000000000000000000;
constexpr Lane TransitionLane13 = /*                */ 0b0000000000100000000000000000000;
constexpr Lane TransitionLane14 = /*                */ 0b0000000001000000000000000000000;

constexpr Lanes RetryLanes = /*                     */ 0b0000011110000000000000000000000;
constexpr Lane RetryLane1 = /*                      */ 0b0000000010000000000000000000000;
constexpr Lane RetryLane2 = /*                      */ 0b0000000100000000000000000000000;
constexpr Lane RetryLane3 = /*                      */ 0b0000001000000000000000000000000;
constexpr Lane RetryLane4 = /*                      */ 0b0000010000000000000000000000000;
constexpr Lane SomeRetryLane = RetryLane1;
constexpr Lane SelectiveHydrationLane = /*          */ 0b0000100000000000000000000000000;
constexpr Lanes NonIdleLanes = /*                   */ 0b0000111111111111111111111111111;
constexpr Lane IdleHydrationLane = /*               */ 0b0001000000000000000000000000000;
constexpr Lane IdleLane = /*                        */ 0b0010000000000000000000000000000;
constexpr Lane OffscreenLane = /*                   */ 0b0100000000000000000000000000000;
constexpr Lane DeferredLane = /*                    */ 0b1000000000000000000000000000000;
constexpr Lanes UpdateLanes = SyncLane | InputContinuousLane | DefaultLane | TransitionLanes;

constexpr Lanes HydrationLanes = 
  SyncHydrationLane |
  InputContinuousHydrationLane |
  DefaultHydrationLane |
  TransitionHydrationLane |
  SelectiveHydrationLane |
  IdleHydrationLane;


// Lane 工具函数
const char* getLabelForLane(Lane lane);

// 合并两组 Lanes
inline constexpr Lanes mergeLanes(Lanes a, Lanes b) {
  return a | b;
}

// 检查是否包含指定的 Lanes
inline constexpr bool includesSomeLane(Lanes set, Lanes subset) {
  return (set & subset) != NoLanes;
}

// 检查 subset 是否为 set 的子集
inline constexpr bool isSubsetOfLanes(Lanes set, Lanes subset) {
  return (set & subset) == subset;
}

// 检查是否包含非空闲 Lanes
inline constexpr bool includesNonIdleWork(Lanes lanes) {
  return (lanes & NonIdleLanes) != NoLanes;
}

// 检查是否只包含重试 Lanes
inline constexpr bool includesOnlyRetries(Lanes lanes) {
  return (lanes & RetryLanes) == lanes;
}

/**
 * 检查是否包含 Transition Lanes
 */
inline constexpr bool includesTransitionLane(Lanes lanes) {
  return (lanes & TransitionLanes) != NoLanes;
}

/**
 * 检查是否阻塞
 */
inline constexpr bool isBlockingLane(Lane lane) {
  return (lane & SyncUpdateLanes) != NoLanes;
}

/**
 * 检查是否为 Transition Lane
 */
inline constexpr bool isTransitionLane(Lane lane) {
  return (lane & TransitionLanes) != NoLanes;
}

/**
 * 移除 Lanes
 */
inline constexpr Lanes removeLanes(Lanes set, Lanes subset) {
  return set & ~subset;
}

/**
 * 相交 Lanes
 */
inline constexpr Lanes intersectLanes(Lanes a, Lanes b) {
  return a & b;
}

/**
 * 获取最高优先级 Lane (最右边的 bit)
 */
inline constexpr Lane getHighestPriorityLane(Lanes lanes) {
  return lanes & (-static_cast<int32_t>(lanes));
}

/**
 * 获取最高优先级 Lanes (等于或高于给定 lane)
 */
inline constexpr Lanes getHighestPriorityLanes(Lanes lanes) {
  // 这是一个简化实现
  // 实际的 React 实现更复杂，考虑了 entanglement
  return getHighestPriorityLane(lanes);
}

/**
 * 计算前导零的数量
 * @source reactjs/packages/react-reconciler/src/clz32.js
 */
int clz32(uint32_t x);

/**
 * 获取 Lane 的索引
 */
int laneToIndex(Lane lane);

/**
 * 通过索引获取 Lane
 */
Lane indexToLane(int index);

/**
 * 获取下一个 Lanes
 */
Lanes getNextLanes(Lanes pendingLanes, Lanes suspendedLanes);

} // namespace react::reconciler
