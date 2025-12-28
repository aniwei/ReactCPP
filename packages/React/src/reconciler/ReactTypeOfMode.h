/**
 * React Type Of Mode
 * 
 * Fiber 节点的模式标志位
 * 
 * @source reactjs/packages/react-reconciler/src/ReactTypeOfMode.js
 */

#pragma once

#include <cstdint>

namespace react::reconciler {

// =============================================================================
// TypeOfMode
// @source reactjs/packages/react-reconciler/src/ReactTypeOfMode.js:11-21
// =============================================================================

using TypeOfMode = uint8_t;

// @source:13
constexpr TypeOfMode NoMode = /*              */ 0b0000000;

// @source:15 TODO: Remove ConcurrentMode by reading from the root tag instead
constexpr TypeOfMode ConcurrentMode = /*      */ 0b0000001;

// @source:16
constexpr TypeOfMode ProfileMode = /*         */ 0b0000010;

// DebugTracingMode was removed (0b0000100)

// @source:18
constexpr TypeOfMode StrictLegacyMode = /*    */ 0b0001000;

// @source:19
constexpr TypeOfMode StrictEffectsMode = /*   */ 0b0010000;

// @source:22 Keep track of if we're in a SuspenseyImages eligible subtree
constexpr TypeOfMode SuspenseyImagesMode = /* */ 0b0100000;

// =============================================================================
// 工具函数
// =============================================================================

/**
 * 获取模式名称
 */
inline const char* getModeName(TypeOfMode mode) {
    if (mode == NoMode) return "NoMode";
    
    // 返回主要模式
    if (mode & ConcurrentMode) return "ConcurrentMode";
    if (mode & ProfileMode) return "ProfileMode";
    if (mode & StrictLegacyMode) return "StrictLegacyMode";
    if (mode & StrictEffectsMode) return "StrictEffectsMode";
    if (mode & SuspenseyImagesMode) return "SuspenseyImagesMode";
    
    return "Unknown";
}

/**
 * 检查是否为并发模式
 */
inline constexpr bool isConcurrentMode(TypeOfMode mode) {
    return (mode & ConcurrentMode) != 0;
}

/**
 * 检查是否启用了 Profiler
 */
inline constexpr bool isProfileMode(TypeOfMode mode) {
    return (mode & ProfileMode) != 0;
}

/**
 * 检查是否为严格模式
 */
inline constexpr bool isStrictMode(TypeOfMode mode) {
    return (mode & (StrictLegacyMode | StrictEffectsMode)) != 0;
}

} // namespace react::reconciler
