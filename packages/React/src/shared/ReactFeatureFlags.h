/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/shared/ReactFeatureFlags.js
 * 
 * ReactFeatureFlags - 编译期和运行期特性开关
 * 
 * 本文件与 ReactJS 的 ReactFeatureFlags.js 完全 1:1 对应
 */

#pragma once

#include <cstdint>

namespace react::shared {

// =============================================================================
// 编译期配置宏
// =============================================================================

// 定义构建模式
#ifndef REACT_PROFILE
#define REACT_PROFILE 0
#endif

#ifndef REACT_EXPERIMENTAL
#define REACT_EXPERIMENTAL 0
#endif

#ifndef REACT_DEV
#define REACT_DEV 0
#endif

// 条件编译辅助宏
#define __PROFILE__ REACT_PROFILE
#define __EXPERIMENTAL__ REACT_EXPERIMENTAL
#define __DEV__ REACT_DEV

// =============================================================================
// Killswitch - 可回滚的开关
// =============================================================================

// @source:24 - enableHydrationLaneScheduling
constexpr bool enableHydrationLaneScheduling = true;

// =============================================================================
// Land or remove (moderate effort)
// =============================================================================

// @source:33 - disableSchedulerTimeoutInWorkLoop
constexpr bool disableSchedulerTimeoutInWorkLoop = false;

// =============================================================================
// Slated for removal in the future
// =============================================================================

// @source:48 - enableSuspenseCallback
constexpr bool enableSuspenseCallback = false;

// @source:51 - enableScopeAPI
constexpr bool enableScopeAPI = false;

// @source:54 - enableCreateEventHandleAPI
constexpr bool enableCreateEventHandleAPI = false;

// @source:57 - enableLegacyFBSupport
constexpr bool enableLegacyFBSupport = false;

// =============================================================================
// Ongoing experiments
// =============================================================================

// @source:66 - enableYieldingBeforePassive
constexpr bool enableYieldingBeforePassive = false;

// @source:69 - enableThrottledScheduling
constexpr bool enableThrottledScheduling = false;

// @source:71 - enableLegacyCache
constexpr bool enableLegacyCache = __EXPERIMENTAL__;

// @source:73 - enableAsyncIterableChildren
constexpr bool enableAsyncIterableChildren = __EXPERIMENTAL__;

// @source:75 - enableTaint
constexpr bool enableTaint = __EXPERIMENTAL__;

// @source:77 - enablePostpone
constexpr bool enablePostpone = __EXPERIMENTAL__;

// @source:79 - enableHalt
constexpr bool enableHalt = __EXPERIMENTAL__;

// @source:81 - enableViewTransition
constexpr bool enableViewTransition = __EXPERIMENTAL__;

// @source:83 - enableGestureTransition
constexpr bool enableGestureTransition = __EXPERIMENTAL__;

// @source:85 - enableScrollEndPolyfill
constexpr bool enableScrollEndPolyfill = __EXPERIMENTAL__;

// @source:87 - enableSuspenseyImages
constexpr bool enableSuspenseyImages = false;

// @source:89 - enableFizzBlockingRender
constexpr bool enableFizzBlockingRender = __EXPERIMENTAL__;

// @source:91 - enableSrcObject
constexpr bool enableSrcObject = __EXPERIMENTAL__;

// @source:93 - enableHydrationChangeEvent
constexpr bool enableHydrationChangeEvent = __EXPERIMENTAL__;

// @source:95 - enableDefaultTransitionIndicator
constexpr bool enableDefaultTransitionIndicator = __EXPERIMENTAL__;

// @source:100 - enableObjectFiber
constexpr bool enableObjectFiber = false;

// @source:102 - enableTransitionTracing
constexpr bool enableTransitionTracing = false;

// @source:105 - enableLegacyHidden
constexpr bool enableLegacyHidden = false;

// @source:108 - enableSuspenseAvoidThisFallback
constexpr bool enableSuspenseAvoidThisFallback = false;

// @source:110 - enableCPUSuspense
constexpr bool enableCPUSuspense = __EXPERIMENTAL__;

// @source:113 - enableNoCloningMemoCache
constexpr bool enableNoCloningMemoCache = false;

// @source:115 - enableUseEffectEventHook
constexpr bool enableUseEffectEventHook = __EXPERIMENTAL__;

// @source:120 - enableFizzExternalRuntime
constexpr bool enableFizzExternalRuntime = __EXPERIMENTAL__;

// @source:122 - alwaysThrottleRetries
constexpr bool alwaysThrottleRetries = true;

// @source:124 - passChildrenWhenCloningPersistedNodes
constexpr bool passChildrenWhenCloningPersistedNodes = false;

// @source:130 - enablePersistedModeClonedFlag
constexpr bool enablePersistedModeClonedFlag = false;

// @source:132 - enableEagerAlternateStateNodeCleanup
constexpr bool enableEagerAlternateStateNodeCleanup = true;

// @source:137 - enableRetryLaneExpiration
constexpr bool enableRetryLaneExpiration = false;

// @source:138 - retryLaneExpirationMs
constexpr int32_t retryLaneExpirationMs = 5000;

// @source:139 - syncLaneExpirationMs
constexpr int32_t syncLaneExpirationMs = 250;

// @source:140 - transitionLaneExpirationMs
constexpr int32_t transitionLaneExpirationMs = 5000;

// @source:146 - enableInfiniteRenderLoopDetection
constexpr bool enableInfiniteRenderLoopDetection = false;

// @source:148 - enableFragmentRefs
constexpr bool enableFragmentRefs = __EXPERIMENTAL__;

// =============================================================================
// Ready for next major
// =============================================================================

// @source:161 - renameElementSymbol
constexpr bool renameElementSymbol = true;

// @source:166 - enableHiddenSubtreeInsertionEffectCleanup
constexpr bool enableHiddenSubtreeInsertionEffectCleanup = false;

// @source:171 - disableLegacyContext
constexpr bool disableLegacyContext = true;

// @source:175 - disableLegacyContextForFunctionComponents
constexpr bool disableLegacyContextForFunctionComponents = true;

// @source:178 - enableMoveBefore
constexpr bool enableMoveBefore = false;

// @source:181 - disableClientCache
constexpr bool disableClientCache = true;

// @source:184 - enableReactTestRendererWarning
constexpr bool enableReactTestRendererWarning = true;

// @source:189 - disableLegacyMode
constexpr bool disableLegacyMode = true;

// =============================================================================
// React DOM Chopping Block
// =============================================================================

// @source:211 - disableCommentsAsDOMContainers
constexpr bool disableCommentsAsDOMContainers = true;

// @source:213 - enableTrustedTypesIntegration
constexpr bool enableTrustedTypesIntegration = false;

// @source:218 - disableInputAttributeSyncing
constexpr bool disableInputAttributeSyncing = false;

// @source:221 - disableTextareaChildren
constexpr bool disableTextareaChildren = false;

// =============================================================================
// Debugging and DevTools
// =============================================================================

// @source:228 - enableProfilerTimer
constexpr bool enableProfilerTimer = __PROFILE__;

// @source:233 - enableComponentPerformanceTrack
constexpr bool enableComponentPerformanceTrack = __EXPERIMENTAL__;

// @source:238-239 - enableSchedulingProfiler
constexpr bool enableSchedulingProfiler = !enableComponentPerformanceTrack && __PROFILE__;

// @source:242 - enableProfilerCommitHooks
constexpr bool enableProfilerCommitHooks = __PROFILE__;

// @source:245 - enableProfilerNestedUpdatePhase
constexpr bool enableProfilerNestedUpdatePhase = __PROFILE__;

// @source:247 - enableAsyncDebugInfo
constexpr bool enableAsyncDebugInfo = true;

// @source:250 - enableUpdaterTracking
constexpr bool enableUpdaterTracking = __PROFILE__;

// @source:252 - ownerStackLimit
constexpr int32_t ownerStackLimit = 10000;

} // namespace react::shared
