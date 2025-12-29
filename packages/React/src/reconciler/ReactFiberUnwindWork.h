#pragma once

#include <memory>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "ReactFiberSuspenseContext.h"
#include "ReactFiberSuspenseComponent.h"

namespace react::reconciler {

// 从 ReactFiberHostContext
void popHostContainer(const FiberRef& workInProgress);
void popHostContext(const FiberRef& workInProgress);

// 从 ReactFiberTreeContext
void popTreeContext(const FiberRef& workInProgress);

// 从 ReactFiberLegacyContext
void popLegacyContext(const FiberRef& workInProgress);
void popTopLevelLegacyContextObject(const FiberRef& workInProgress);

bool isLegacyContextProvider(const std::any& component);

// jsi::Value 重载版本
bool isLegacyContextProvider(const jsi::Value& component);

// 从 ReactFiberNewContext
void popProvider(const std::any& context, const FiberRef& workInProgress);

// jsi::Value 重载版本
void popProvider(const jsi::Value& context, const FiberRef& workInProgress);

// 从 ReactFiberCacheComponent
void popCacheProvider(const FiberRef& workInProgress, const std::any& cache);

// 从 ReactFiberHiddenContext
void popHiddenContext(const FiberRef& workInProgress);

// 从 ReactFiberTransition
void popTransition(const FiberRef& workInProgress, const FiberRef& current);
void popRootTransition(const FiberRef& workInProgress, const FiberRootRef& root, Lanes renderLanes);

// 从 ReactFiberHydrationContext
void resetHydrationState();

// 从 ReactProfilerTimer
void transferActualDuration(const FiberRef& workInProgress);

// 展开工作 - 当遇到错误或 Suspense 时向上遍历栈
FiberRef unwindWork(const FiberRef& current, const FiberRef& workInProgress, Lanes renderLanes);

// 展开被中断的工作
void unwindInterruptedWork(const FiberRef& current, const FiberRef& interruptedWork, Lanes renderLanes);

// 完整的错误展开流程
// 完成展开阶段

FiberRef completeUnitOfUnwind(const FiberRef& unitOfWork, const FiberRef& thrownValue, Lanes renderLanes);

} // namespace react::reconciler
