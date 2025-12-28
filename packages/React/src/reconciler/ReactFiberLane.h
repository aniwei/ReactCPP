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

// =============================================================================
// 类型定义
// @source reactjs/packages/react-reconciler/src/ReactFiberLane.js:16-18
// =============================================================================

using Lanes = uint32_t;
using Lane = uint32_t;

template<typename T>
using LaneMap = std::array<T, 31>;

// =============================================================================
// Lane 常量
// @source reactjs/packages/react-reconciler/src/ReactFiberLane.js:40-92
// =============================================================================

// @source:39
constexpr int TotalLanes = 31;

// @source:41
constexpr Lanes NoLanes = /*                        */ 0b0000000000000000000000000000000;
// @source:42
constexpr Lane NoLane = /*                          */ 0b0000000000000000000000000000000;

// @source:44
constexpr Lane SyncHydrationLane = /*               */ 0b0000000000000000000000000000001;
// @source:45
constexpr Lane SyncLane = /*                        */ 0b0000000000000000000000000000010;
// @source:46
constexpr int SyncLaneIndex = 1;

// @source:48
constexpr Lane InputContinuousHydrationLane = /*    */ 0b0000000000000000000000000000100;
// @source:49
constexpr Lane InputContinuousLane = /*             */ 0b0000000000000000000000000001000;

// @source:51
constexpr Lane DefaultHydrationLane = /*            */ 0b0000000000000000000000000010000;
// @source:52
constexpr Lane DefaultLane = /*                     */ 0b0000000000000000000000000100000;

// @source:54-55
constexpr Lane SyncUpdateLanes = SyncLane | InputContinuousLane | DefaultLane;

// @source:57
constexpr Lane GestureLane = /*                     */ 0b0000000000000000000000001000000;

// @source:59
constexpr Lane TransitionHydrationLane = /*         */ 0b0000000000000000000000010000000;
// @source:60
constexpr Lanes TransitionLanes = /*                */ 0b0000000001111111111111100000000;
// @source:61
constexpr Lane TransitionLane1 = /*                 */ 0b0000000000000000000000100000000;
// @source:62
constexpr Lane TransitionLane2 = /*                 */ 0b0000000000000000000001000000000;
// @source:63
constexpr Lane TransitionLane3 = /*                 */ 0b0000000000000000000010000000000;
// @source:64
constexpr Lane TransitionLane4 = /*                 */ 0b0000000000000000000100000000000;
// @source:65
constexpr Lane TransitionLane5 = /*                 */ 0b0000000000000000001000000000000;
// @source:66
constexpr Lane TransitionLane6 = /*                 */ 0b0000000000000000010000000000000;
// @source:67
constexpr Lane TransitionLane7 = /*                 */ 0b0000000000000000100000000000000;
// @source:68
constexpr Lane TransitionLane8 = /*                 */ 0b0000000000000001000000000000000;
// @source:69
constexpr Lane TransitionLane9 = /*                 */ 0b0000000000000010000000000000000;
// @source:70
constexpr Lane TransitionLane10 = /*                */ 0b0000000000000100000000000000000;
// @source:71
constexpr Lane TransitionLane11 = /*                */ 0b0000000000001000000000000000000;
// @source:72
constexpr Lane TransitionLane12 = /*                */ 0b0000000000010000000000000000000;
// @source:73
constexpr Lane TransitionLane13 = /*                */ 0b0000000000100000000000000000000;
// @source:74
constexpr Lane TransitionLane14 = /*                */ 0b0000000001000000000000000000000;

// @source:76
constexpr Lanes RetryLanes = /*                     */ 0b0000011110000000000000000000000;
// @source:77
constexpr Lane RetryLane1 = /*                      */ 0b0000000010000000000000000000000;
// @source:78
constexpr Lane RetryLane2 = /*                      */ 0b0000000100000000000000000000000;
// @source:79
constexpr Lane RetryLane3 = /*                      */ 0b0000001000000000000000000000000;
// @source:80
constexpr Lane RetryLane4 = /*                      */ 0b0000010000000000000000000000000;

// @source:82
constexpr Lane SomeRetryLane = RetryLane1;

// @source:84
constexpr Lane SelectiveHydrationLane = /*          */ 0b0000100000000000000000000000000;

// @source:86
constexpr Lanes NonIdleLanes = /*                   */ 0b0000111111111111111111111111111;

// @source:88
constexpr Lane IdleHydrationLane = /*               */ 0b0001000000000000000000000000000;
// @source:89
constexpr Lane IdleLane = /*                        */ 0b0010000000000000000000000000000;

// @source:91
constexpr Lane OffscreenLane = /*                   */ 0b0100000000000000000000000000000;
// @source:92
constexpr Lane DeferredLane = /*                    */ 0b1000000000000000000000000000000;

// @source:95-96
constexpr Lanes UpdateLanes = SyncLane | InputContinuousLane | DefaultLane | TransitionLanes;

// @source:98-104
constexpr Lanes HydrationLanes = 
    SyncHydrationLane |
    InputContinuousHydrationLane |
    DefaultHydrationLane |
    TransitionHydrationLane |
    SelectiveHydrationLane |
    IdleHydrationLane;

// =============================================================================
// Lane 工具函数
// =============================================================================

/**
 * 获取 Lane 的标签名称
 * @source reactjs/packages/react-reconciler/src/ReactFiberLane.js:107-150
 */
inline const char* getLabelForLane(Lane lane) {
    if (lane & SyncHydrationLane) return "SyncHydrationLane";
    if (lane & SyncLane) return "Sync";
    if (lane & InputContinuousHydrationLane) return "InputContinuousHydration";
    if (lane & InputContinuousLane) return "InputContinuous";
    if (lane & DefaultHydrationLane) return "DefaultHydration";
    if (lane & DefaultLane) return "Default";
    if (lane & TransitionHydrationLane) return "TransitionHydration";
    if (lane & TransitionLanes) return "Transition";
    if (lane & RetryLanes) return "Retry";
    if (lane & SelectiveHydrationLane) return "SelectiveHydration";
    if (lane & IdleHydrationLane) return "IdleHydration";
    if (lane & IdleLane) return "Idle";
    if (lane & OffscreenLane) return "Offscreen";
    if (lane & DeferredLane) return "Deferred";
    return "Unknown";
}

/**
 * 合并两组 Lanes
 */
inline constexpr Lanes mergeLanes(Lanes a, Lanes b) {
    return a | b;
}

/**
 * 检查是否包含指定的 Lanes
 */
inline constexpr bool includesSomeLane(Lanes set, Lanes subset) {
    return (set & subset) != NoLanes;
}
/**
 * 检查 subset 是否为 set 的子集
 * @source reactjs/packages/react-reconciler/src/ReactFiberLane.js:194-196
 */
inline constexpr bool isSubsetOfLanes(Lanes set, Lanes subset) {
    return (set & subset) == subset;
}
/**
 * 检查是否包含非空闲 Lanes
 */
inline constexpr bool includesNonIdleWork(Lanes lanes) {
    return (lanes & NonIdleLanes) != NoLanes;
}

/**
 * 检查是否只包含重试 Lanes
 */
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
inline int clz32(uint32_t x) {
    if (x == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(x);
#else
    // 软件实现
    int n = 0;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFF) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFF) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFF) { n += 1; }
    return n;
#endif
}

/**
 * 获取 Lane 的索引
 */
inline int laneToIndex(Lane lane) {
    return 31 - clz32(lane);
}

/**
 * 通过索引获取 Lane
 */
inline Lane indexToLane(int index) {
    return static_cast<Lane>(1 << index);
}

/**
 * 获取下一个 Lanes
 */
inline Lanes getNextLanes(Lanes pendingLanes, Lanes suspendedLanes) {
    if (pendingLanes == NoLanes) {
        return NoLanes;
    }
    
    Lanes nextLanes = NoLanes;
    
    Lanes nonIdlePendingLanes = pendingLanes & NonIdleLanes;
    if (nonIdlePendingLanes != NoLanes) {
        Lanes nonIdleUnblockedLanes = nonIdlePendingLanes & ~suspendedLanes;
        if (nonIdleUnblockedLanes != NoLanes) {
            nextLanes = getHighestPriorityLanes(nonIdleUnblockedLanes);
        } else {
            nextLanes = getHighestPriorityLanes(nonIdlePendingLanes);
        }
    } else {
        // 只有空闲工作
        Lanes unblockedLanes = pendingLanes & ~suspendedLanes;
        if (unblockedLanes != NoLanes) {
            nextLanes = getHighestPriorityLanes(unblockedLanes);
        } else {
            nextLanes = getHighestPriorityLanes(pendingLanes);
        }
    }
    
    return nextLanes;
}

} // namespace react::reconciler
