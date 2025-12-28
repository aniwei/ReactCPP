/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/react-reconciler/src/ReactWorkTags.js
 * 
 * ReactWorkTags - Fiber 节点类型标签
 * 
 * 本文件与 ReactJS 的 ReactWorkTags.js 完全 1:1 对应
 */

#pragma once

#include <cstdint>

namespace react::reconciler {

// =============================================================================
// WorkTag 类型定义
// @source:10-44
// =============================================================================

using WorkTag = uint8_t;

// =============================================================================
// Work Tag 常量
// @source:45-68
// =============================================================================

// @source:45 - FunctionComponent = 0
constexpr WorkTag FunctionComponent = 0;

// @source:46 - ClassComponent = 1
constexpr WorkTag ClassComponent = 1;

// Note: 2 is reserved (IndeterminateComponent was removed)

// @source:47 - HostRoot = 3
// Root of a host tree. Could be nested inside another node.
constexpr WorkTag HostRoot = 3;

// @source:48 - HostPortal = 4
// A subtree. Could be an entry point to a different renderer.
constexpr WorkTag HostPortal = 4;

// @source:49 - HostComponent = 5
constexpr WorkTag HostComponent = 5;

// @source:50 - HostText = 6
constexpr WorkTag HostText = 6;

// @source:51 - Fragment = 7
constexpr WorkTag Fragment = 7;

// @source:52 - Mode = 8
constexpr WorkTag Mode = 8;

// @source:53 - ContextConsumer = 9
constexpr WorkTag ContextConsumer = 9;

// @source:54 - ContextProvider = 10
constexpr WorkTag ContextProvider = 10;

// @source:55 - ForwardRef = 11
constexpr WorkTag ForwardRef = 11;

// @source:56 - Profiler = 12
constexpr WorkTag Profiler = 12;

// @source:57 - SuspenseComponent = 13
constexpr WorkTag SuspenseComponent = 13;

// @source:58 - MemoComponent = 14
constexpr WorkTag MemoComponent = 14;

// @source:59 - SimpleMemoComponent = 15
constexpr WorkTag SimpleMemoComponent = 15;

// @source:60 - LazyComponent = 16
constexpr WorkTag LazyComponent = 16;

// @source:61 - IncompleteClassComponent = 17
constexpr WorkTag IncompleteClassComponent = 17;

// @source:62 - DehydratedFragment = 18
constexpr WorkTag DehydratedFragment = 18;

// @source:63 - SuspenseListComponent = 19
constexpr WorkTag SuspenseListComponent = 19;

// Note: 20 is reserved

// @source:64 - ScopeComponent = 21
constexpr WorkTag ScopeComponent = 21;

// @source:65 - OffscreenComponent = 22
constexpr WorkTag OffscreenComponent = 22;

// @source:66 - LegacyHiddenComponent = 23
constexpr WorkTag LegacyHiddenComponent = 23;

// @source:67 - CacheComponent = 24
constexpr WorkTag CacheComponent = 24;

// @source:68 - TracingMarkerComponent = 25
constexpr WorkTag TracingMarkerComponent = 25;

// @source:69 - HostHoistable = 26
constexpr WorkTag HostHoistable = 26;

// @source:70 - HostSingleton = 27
constexpr WorkTag HostSingleton = 27;

// @source:71 - IncompleteFunctionComponent = 28
constexpr WorkTag IncompleteFunctionComponent = 28;

// @source:72 - Throw = 29
constexpr WorkTag Throw = 29;

// @source:73 - ViewTransitionComponent = 30
constexpr WorkTag ViewTransitionComponent = 30;

// @source:74 - ActivityComponent = 31
constexpr WorkTag ActivityComponent = 31;

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * 获取 WorkTag 的字符串名称（用于调试输出）
 */
inline const char* getWorkTagName(WorkTag tag) {
    switch (tag) {
        case FunctionComponent: return "FunctionComponent";
        case ClassComponent: return "ClassComponent";
        case HostRoot: return "HostRoot";
        case HostPortal: return "HostPortal";
        case HostComponent: return "HostComponent";
        case HostText: return "HostText";
        case Fragment: return "Fragment";
        case Mode: return "Mode";
        case ContextConsumer: return "ContextConsumer";
        case ContextProvider: return "ContextProvider";
        case ForwardRef: return "ForwardRef";
        case Profiler: return "Profiler";
        case SuspenseComponent: return "SuspenseComponent";
        case MemoComponent: return "MemoComponent";
        case SimpleMemoComponent: return "SimpleMemoComponent";
        case LazyComponent: return "LazyComponent";
        case IncompleteClassComponent: return "IncompleteClassComponent";
        case DehydratedFragment: return "DehydratedFragment";
        case SuspenseListComponent: return "SuspenseListComponent";
        case ScopeComponent: return "ScopeComponent";
        case OffscreenComponent: return "OffscreenComponent";
        case LegacyHiddenComponent: return "LegacyHiddenComponent";
        case CacheComponent: return "CacheComponent";
        case TracingMarkerComponent: return "TracingMarkerComponent";
        case HostHoistable: return "HostHoistable";
        case HostSingleton: return "HostSingleton";
        case IncompleteFunctionComponent: return "IncompleteFunctionComponent";
        case Throw: return "Throw";
        case ViewTransitionComponent: return "ViewTransitionComponent";
        case ActivityComponent: return "ActivityComponent";
        default: return "Unknown";
    }
}

} // namespace react::reconciler
