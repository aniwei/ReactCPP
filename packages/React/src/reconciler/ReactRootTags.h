#pragma once

#include <cstdint>

namespace react::reconciler {

using RootTag = uint8_t;

constexpr RootTag LegacyRoot = 0;
constexpr RootTag ConcurrentRoot = 1;

// 工具函数
// 获取根类型名称
const char* getRootTagName(RootTag tag);
 
// 检查是否为并发根
inline constexpr bool isConcurrentRoot(RootTag tag) {
  return tag == ConcurrentRoot;
}

// 检查是否为遗留根
inline constexpr bool isLegacyRoot(RootTag tag) {
  return tag == LegacyRoot;
}

} // namespace react::reconciler
