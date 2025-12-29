#pragma once

#include <cstdint>

namespace react::reconciler {

// WorkTag 类型定义
using WorkTag = uint8_t;

// Work Tag 常量
constexpr WorkTag FunctionComponent = 0;
constexpr WorkTag ClassComponent = 1;
constexpr WorkTag HostRoot = 3;
constexpr WorkTag HostPortal = 4;
constexpr WorkTag HostComponent = 5;
constexpr WorkTag HostText = 6;
constexpr WorkTag Fragment = 7;
constexpr WorkTag Mode = 8;
constexpr WorkTag ContextConsumer = 9;
constexpr WorkTag ContextProvider = 10;
constexpr WorkTag ForwardRef = 11;
constexpr WorkTag Profiler = 12;
constexpr WorkTag SuspenseComponent = 13;
constexpr WorkTag MemoComponent = 14;
constexpr WorkTag SimpleMemoComponent = 15;
constexpr WorkTag LazyComponent = 16;
constexpr WorkTag IncompleteClassComponent = 17;
constexpr WorkTag DehydratedFragment = 18;
constexpr WorkTag SuspenseListComponent = 19;
constexpr WorkTag ScopeComponent = 21;
constexpr WorkTag OffscreenComponent = 22;
constexpr WorkTag LegacyHiddenComponent = 23;
constexpr WorkTag CacheComponent = 24;
constexpr WorkTag TracingMarkerComponent = 25;
constexpr WorkTag HostHoistable = 26;
constexpr WorkTag HostSingleton = 27;
constexpr WorkTag IncompleteFunctionComponent = 28;
constexpr WorkTag Throw = 29;
constexpr WorkTag ViewTransitionComponent = 30;
constexpr WorkTag ActivityComponent = 31;

// 辅助函数
const char* getWorkTagName(WorkTag tag);

} // namespace react::reconciler
