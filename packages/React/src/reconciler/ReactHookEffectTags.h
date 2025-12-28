/**
 * React Hook Effect Tags
 * 
 * Hook 效果标志，用于标识不同类型的 Hook 效果及其生命周期阶段
 * 
 * @source reactjs/packages/react-reconciler/src/ReactHookEffectTags.js
 */

#pragma once

#include <cstdint>

namespace react::reconciler {

// =============================================================================
// Hook Flags 类型定义
// @source ReactHookEffectTags.js:10
// =============================================================================

using HookFlags = uint8_t;

// =============================================================================
// Hook Effect Tags 常量
// @source ReactHookEffectTags.js:12-20
// =============================================================================

// @source:12 - 无标志
constexpr HookFlags HookNoFlags = /*       */ 0b0000;

// @source:15 - 表示效果是否应该触发
constexpr HookFlags HookHasEffect = /*     */ 0b0001;

// @source:18 - Insertion 阶段效果 (useInsertionEffect)
constexpr HookFlags HookInsertion = /*     */ 0b0010;

// @source:19 - Layout 阶段效果 (useLayoutEffect)
constexpr HookFlags HookLayout = /*        */ 0b0100;

// @source:20 - Passive 阶段效果 (useEffect)
constexpr HookFlags HookPassive = /*       */ 0b1000;

// =============================================================================
// 工具函数
// =============================================================================

/**
 * 检查是否有指定的效果标志
 */
inline constexpr bool hasHookFlags(HookFlags flags, HookFlags check) {
  return (flags & check) == check;
}

/**
 * 添加效果标志
 */
inline constexpr HookFlags addHookFlags(HookFlags flags, HookFlags toAdd) {
  return flags | toAdd;
}

/**
 * 移除效果标志
 */
inline constexpr HookFlags removeHookFlags(HookFlags flags, HookFlags toRemove) {
  return flags & ~toRemove;
}

/**
 * 获取 Hook 效果的名称 (用于调试)
 */
inline const char* getHookEffectName(HookFlags flags) {
  if (flags & HookLayout) return "useLayoutEffect";
  if (flags & HookInsertion) return "useInsertionEffect";
  if (flags & HookPassive) return "useEffect";
  return "unknown";
}

} // namespace react::reconciler
