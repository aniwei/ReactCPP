/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/scheduler/src/SchedulerPriorities.js
 * 
 * SchedulerPriorities - 调度器优先级定义
 * 
 * 本文件与 ReactJS 的 SchedulerPriorities.js 完全 1:1 对应
 */

#pragma once

#include <cstdint>

namespace react::scheduler {

// =============================================================================
// PriorityLevel 类型定义
// @source:10
// =============================================================================

using PriorityLevel = uint8_t;

// =============================================================================
// Priority 常量
// @source:12-18
// =============================================================================

// @source:13 - NoPriority = 0
constexpr PriorityLevel NoPriority = 0;

// @source:14 - ImmediatePriority = 1
constexpr PriorityLevel ImmediatePriority = 1;

// @source:15 - UserBlockingPriority = 2
constexpr PriorityLevel UserBlockingPriority = 2;

// @source:16 - NormalPriority = 3
constexpr PriorityLevel NormalPriority = 3;

// @source:17 - LowPriority = 4
constexpr PriorityLevel LowPriority = 4;

// @source:18 - IdlePriority = 5
constexpr PriorityLevel IdlePriority = 5;

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * 获取优先级的字符串名称（用于调试输出）
 */
inline const char* getPriorityName(PriorityLevel priority) {
    switch (priority) {
        case NoPriority: return "NoPriority";
        case ImmediatePriority: return "ImmediatePriority";
        case UserBlockingPriority: return "UserBlockingPriority";
        case NormalPriority: return "NormalPriority";
        case LowPriority: return "LowPriority";
        case IdlePriority: return "IdlePriority";
        default: return "Unknown";
    }
}

/**
 * 判断优先级是否有效
 */
constexpr bool isValidPriority(PriorityLevel priority) {
    return priority >= NoPriority && priority <= IdlePriority;
}

/**
 * 比较两个优先级（数值越小优先级越高）
 * 返回值: 
 *   < 0: a 优先级更高
 *   = 0: 优先级相同
 *   > 0: b 优先级更高
 */
constexpr int comparePriority(PriorityLevel a, PriorityLevel b) {
    return static_cast<int>(a) - static_cast<int>(b);
}

} // namespace react::scheduler
