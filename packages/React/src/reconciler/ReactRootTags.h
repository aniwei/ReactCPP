/**
 * React Root Tags
 * 
 * 根节点类型标签
 * 
 * @source reactjs/packages/react-reconciler/src/ReactRootTags.js
 */

#pragma once

#include <cstdint>

namespace react::reconciler {

// =============================================================================
// RootTag
// @source reactjs/packages/react-reconciler/src/ReactRootTags.js:11-14
// =============================================================================

using RootTag = uint8_t;

// @source:13
constexpr RootTag LegacyRoot = 0;

// @source:14
constexpr RootTag ConcurrentRoot = 1;

// =============================================================================
// 工具函数
// =============================================================================

/**
 * 获取根类型名称
 */
inline const char* getRootTagName(RootTag tag) {
    switch (tag) {
        case LegacyRoot: return "LegacyRoot";
        case ConcurrentRoot: return "ConcurrentRoot";
        default: return "Unknown";
    }
}

/**
 * 检查是否为并发根
 */
inline constexpr bool isConcurrentRoot(RootTag tag) {
    return tag == ConcurrentRoot;
}

/**
 * 检查是否为遗留根
 */
inline constexpr bool isLegacyRoot(RootTag tag) {
    return tag == LegacyRoot;
}

} // namespace react::reconciler
