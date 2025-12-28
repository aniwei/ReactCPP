/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberFlags.js
 * 
 * ReactFiberFlags - Fiber 节点效果标志位
 * 
 * 本文件与 ReactJS 的 ReactFiberFlags.js 完全 1:1 对应
 * 
 * 警告: 不要更改前几个值，它们被 React DevTools 使用！
 */

#pragma once

#include <cstdint>
#include "../shared/ReactFeatureFlags.h"

namespace react::reconciler {

// =============================================================================
// Flags 类型定义
// @source:15
// =============================================================================

using Flags = uint32_t;

// =============================================================================
// 核心标志位 - 不要更改这些值，它们被 React DevTools 使用！
// @source:17-22
// =============================================================================

// @source:18 - NoFlags
constexpr Flags NoFlags = /*                      */ 0b0000000000000000000000000000000;

// @source:19 - PerformedWork
constexpr Flags PerformedWork = /*                */ 0b0000000000000000000000000000001;

// @source:20 - Placement
constexpr Flags Placement = /*                    */ 0b0000000000000000000000000000010;

// @source:21 - DidCapture
constexpr Flags DidCapture = /*                   */ 0b0000000000000000000000010000000;

// @source:22 - Hydrating
constexpr Flags Hydrating = /*                    */ 0b0000000000000000001000000000000;

// =============================================================================
// 可变更的标志位
// @source:24-82
// =============================================================================

// @source:25 - Update
constexpr Flags Update = /*                       */ 0b0000000000000000000000000000100;

// @source:26 - Cloned
constexpr Flags Cloned = /*                       */ 0b0000000000000000000000000001000;

// @source:28 - ChildDeletion
constexpr Flags ChildDeletion = /*                */ 0b0000000000000000000000000010000;

// @source:29 - ContentReset
constexpr Flags ContentReset = /*                 */ 0b0000000000000000000000000100000;

// @source:30 - Callback
constexpr Flags Callback = /*                     */ 0b0000000000000000000000001000000;

// @source:33 - ForceClientRender
constexpr Flags ForceClientRender = /*            */ 0b0000000000000000000000100000000;

// @source:34 - Ref
constexpr Flags Ref = /*                          */ 0b0000000000000000000001000000000;

// @source:35 - Snapshot
constexpr Flags Snapshot = /*                     */ 0b0000000000000000000010000000000;

// @source:36 - Passive
constexpr Flags Passive = /*                      */ 0b0000000000000000000100000000000;

// @source:39 - Visibility
constexpr Flags Visibility = /*                   */ 0b0000000000000000010000000000000;

// @source:40 - StoreConsistency
constexpr Flags StoreConsistency = /*             */ 0b0000000000000000100000000000000;

// =============================================================================
// 复用的标志位（对于不同的 fiber 类型互斥）
// @source:42-49
// =============================================================================

// @source:44 - Hydrate
constexpr Flags Hydrate = Callback;

// @source:45 - ScheduleRetry
constexpr Flags ScheduleRetry = StoreConsistency;

// @source:46 - ShouldSuspendCommit
constexpr Flags ShouldSuspendCommit = Visibility;

// @source:47 - ViewTransitionNamedMount
constexpr Flags ViewTransitionNamedMount = ShouldSuspendCommit;

// @source:48 - DidDefer
constexpr Flags DidDefer = ContentReset;

// @source:49 - FormReset
constexpr Flags FormReset = Snapshot;

// @source:50 - AffectedParentLayout
constexpr Flags AffectedParentLayout = ContentReset;

// =============================================================================
// 组合标志位
// @source:52-55
// =============================================================================

// @source:52-53 - LifecycleEffectMask
constexpr Flags LifecycleEffectMask =
    Passive | Update | Callback | Ref | Snapshot | StoreConsistency;

// @source:56 - HostEffectMask
// Union of all commit flags (flags with the lifetime of a particular commit)
constexpr Flags HostEffectMask = /*               */ 0b0000000000000000111111111111111;

// =============================================================================
// 内部标志位（不是真正的副作用）
// @source:58-65
// =============================================================================

// @source:59 - Incomplete
constexpr Flags Incomplete = /*                   */ 0b0000000000000001000000000000000;

// @source:60 - ShouldCapture
constexpr Flags ShouldCapture = /*                */ 0b0000000000000010000000000000000;

// @source:61 - ForceUpdateForLegacySuspense
constexpr Flags ForceUpdateForLegacySuspense = /* */ 0b0000000000000100000000000000000;

// @source:62 - DidPropagateContext
constexpr Flags DidPropagateContext = /*          */ 0b0000000000001000000000000000000;

// @source:63 - NeedsPropagation
constexpr Flags NeedsPropagation = /*             */ 0b0000000000010000000000000000000;

// @source:64 - Forked
constexpr Flags Forked = /*                       */ 0b0000000000100000000000000000000;

// =============================================================================
// 静态标志位
// @source:66-82
// =============================================================================

// @source:71 - SnapshotStatic
constexpr Flags SnapshotStatic = /*               */ 0b0000000001000000000000000000000;

// @source:72 - LayoutStatic
constexpr Flags LayoutStatic = /*                 */ 0b0000000010000000000000000000000;

// @source:73 - RefStatic
constexpr Flags RefStatic = LayoutStatic;

// @source:74 - PassiveStatic
constexpr Flags PassiveStatic = /*                */ 0b0000000100000000000000000000000;

// @source:75 - MaySuspendCommit
constexpr Flags MaySuspendCommit = /*             */ 0b0000001000000000000000000000000;

// @source:79-80 - ViewTransitionNamedStatic
constexpr Flags ViewTransitionNamedStatic = SnapshotStatic | MaySuspendCommit;

// @source:83 - ViewTransitionStatic
constexpr Flags ViewTransitionStatic = /*         */ 0b0000010000000000000000000000000;

// =============================================================================
// DEV 标志位
// @source:86-88
// =============================================================================

// @source:86 - PlacementDEV
constexpr Flags PlacementDEV = /*                 */ 0b0000100000000000000000000000000;

// @source:87 - MountLayoutDev
constexpr Flags MountLayoutDev = /*               */ 0b0001000000000000000000000000000;

// @source:88 - MountPassiveDev
constexpr Flags MountPassiveDev = /*              */ 0b0010000000000000000000000000000;

// =============================================================================
// Commit Phase 标志位组合
// @source:90-143
// =============================================================================

// @source:92-109 - BeforeMutationMask
constexpr Flags BeforeMutationMask =
    Snapshot |
    (shared::enableCreateEventHandleAPI
        ? Update | ChildDeletion | Visibility
        : shared::enableUseEffectEventHook
            ? Update
            : 0);

// @source:112-113 - BeforeAndAfterMutationTransitionMask
constexpr Flags BeforeAndAfterMutationTransitionMask =
    Snapshot | Update | Placement | ChildDeletion | Visibility | ContentReset;

// @source:115-123 - MutationMask
constexpr Flags MutationMask =
    Placement |
    Update |
    ChildDeletion |
    ContentReset |
    Ref |
    Hydrating |
    Visibility |
    FormReset;

// @source:124 - LayoutMask
constexpr Flags LayoutMask = Update | Callback | Ref | Visibility;

// @source:127 - PassiveMask
constexpr Flags PassiveMask = Passive | Visibility | ChildDeletion;

// @source:131 - PassiveTransitionMask
constexpr Flags PassiveTransitionMask = PassiveMask | Update | Placement;

// @source:135-142 - StaticMask
// Union of tags that don't get reset on clones
constexpr Flags StaticMask =
    LayoutStatic |
    PassiveStatic |
    RefStatic |
    MaySuspendCommit |
    ViewTransitionStatic |
    ViewTransitionNamedStatic;

} // namespace react::reconciler
