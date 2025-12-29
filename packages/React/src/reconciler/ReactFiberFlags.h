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

using Flags = uint32_t;

constexpr Flags NoFlags = /*                      */ 0b0000000000000000000000000000000;
constexpr Flags PerformedWork = /*                */ 0b0000000000000000000000000000001;
constexpr Flags Placement = /*                    */ 0b0000000000000000000000000000010;
constexpr Flags DidCapture = /*                   */ 0b0000000000000000000000010000000;
constexpr Flags Hydrating = /*                    */ 0b0000000000000000001000000000000;

// 可变更的标志位
constexpr Flags Update = /*                       */ 0b0000000000000000000000000000100;
constexpr Flags Cloned = /*                       */ 0b0000000000000000000000000001000;
constexpr Flags ChildDeletion = /*                */ 0b0000000000000000000000000010000;
constexpr Flags ContentReset = /*                 */ 0b0000000000000000000000000100000;
constexpr Flags Callback = /*                     */ 0b0000000000000000000000001000000;
constexpr Flags ForceClientRender = /*            */ 0b0000000000000000000000100000000;
constexpr Flags Ref = /*                          */ 0b0000000000000000000001000000000;
constexpr Flags Snapshot = /*                     */ 0b0000000000000000000010000000000;
constexpr Flags Passive = /*                      */ 0b0000000000000000000100000000000;
constexpr Flags Visibility = /*                   */ 0b0000000000000000010000000000000;
constexpr Flags StoreConsistency = /*             */ 0b0000000000000000100000000000000;
constexpr Flags Hydrate = Callback;
constexpr Flags ScheduleRetry = StoreConsistency;
constexpr Flags ShouldSuspendCommit = Visibility;
constexpr Flags ViewTransitionNamedMount = ShouldSuspendCommit;
constexpr Flags DidDefer = ContentReset;
constexpr Flags FormReset = Snapshot;
constexpr Flags AffectedParentLayout = ContentReset;

// 组合标志位
constexpr Flags LifecycleEffectMask = Passive | Update | Callback | Ref | Snapshot | StoreConsistency;
constexpr Flags HostEffectMask = /*               */ 0b0000000000000000111111111111111;

// 内部标志位（不是真正的副作用）
constexpr Flags Incomplete = /*                   */ 0b0000000000000001000000000000000;
constexpr Flags ShouldCapture = /*                */ 0b0000000000000010000000000000000;
constexpr Flags ForceUpdateForLegacySuspense = /* */ 0b0000000000000100000000000000000;
constexpr Flags DidPropagateContext = /*          */ 0b0000000000001000000000000000000;
constexpr Flags NeedsPropagation = /*             */ 0b0000000000010000000000000000000;
constexpr Flags Forked = /*                       */ 0b0000000000100000000000000000000;

// 静态标志位
constexpr Flags SnapshotStatic = /*               */ 0b0000000001000000000000000000000;
constexpr Flags LayoutStatic = /*                 */ 0b0000000010000000000000000000000;
constexpr Flags RefStatic = LayoutStatic;
constexpr Flags PassiveStatic = /*                */ 0b0000000100000000000000000000000;
constexpr Flags MaySuspendCommit = /*             */ 0b0000001000000000000000000000000;
constexpr Flags ViewTransitionNamedStatic = SnapshotStatic | MaySuspendCommit;
constexpr Flags ViewTransitionStatic = /*         */ 0b0000010000000000000000000000000;

// DEV 标志位
constexpr Flags PlacementDEV = /*                 */ 0b0000100000000000000000000000000;
constexpr Flags MountLayoutDev = /*               */ 0b0001000000000000000000000000000;
constexpr Flags MountPassiveDev = /*              */ 0b0010000000000000000000000000000;

// Commit Phase 标志位组合
constexpr Flags BeforeMutationMask =
  Snapshot |
  (shared::enableCreateEventHandleAPI
    ? Update | ChildDeletion | Visibility
    : shared::enableUseEffectEventHook
      ? Update
      : 0);
constexpr Flags BeforeAndAfterMutationTransitionMask = Snapshot | Update | Placement | ChildDeletion | Visibility | ContentReset;
constexpr Flags MutationMask =
  Placement |
  Update |
  ChildDeletion |
  ContentReset |
  Ref |
  Hydrating |
  Visibility |
  FormReset;
constexpr Flags LayoutMask = Update | Callback | Ref | Visibility;
constexpr Flags PassiveMask = Passive | Visibility | ChildDeletion;
constexpr Flags PassiveTransitionMask = PassiveMask | Update | Placement;
constexpr Flags StaticMask =
  LayoutStatic |
  PassiveStatic |
  RefStatic |
  MaySuspendCommit |
  ViewTransitionStatic |
  ViewTransitionNamedStatic;

} // namespace react::reconciler
