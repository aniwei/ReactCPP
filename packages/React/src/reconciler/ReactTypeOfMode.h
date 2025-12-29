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

// TypeOfMode
using TypeOfMode = uint8_t;
constexpr TypeOfMode NoMode = /*              */ 0b0000000;
constexpr TypeOfMode ConcurrentMode = /*      */ 0b0000001;
constexpr TypeOfMode ProfileMode = /*         */ 0b0000010;

// DebugTracingMode was removed (0b0000100)
constexpr TypeOfMode StrictLegacyMode = /*    */ 0b0001000;
constexpr TypeOfMode StrictEffectsMode = /*   */ 0b0010000;
constexpr TypeOfMode SuspenseyImagesMode = /* */ 0b0100000;

// 工具函数
// 获取模式名称
const char* getModeName(TypeOfMode mode);

// 检查是否为并发模式
inline constexpr bool isConcurrentMode(TypeOfMode mode) {
  return (mode & ConcurrentMode) != 0;
}

// 检查是否启用了 Profiler
inline constexpr bool isProfileMode(TypeOfMode mode) {
  return (mode & ProfileMode) != 0;
}

// 检查是否为严格模式
inline constexpr bool isStrictMode(TypeOfMode mode) {
  return (mode & (StrictLegacyMode | StrictEffectsMode)) != 0;
}

} // namespace react::reconciler
