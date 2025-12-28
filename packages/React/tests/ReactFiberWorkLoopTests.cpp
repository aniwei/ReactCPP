/**
 * React Fiber Work Loop 测试
 * 
 * 测试 Reconciler 核心工作循环
 */

#include <gtest/gtest.h>
#include <memory>

#include "reconciler/ReactFiberWorkLoop.h"
#include "reconciler/ReactFiberBeginWork.h"
#include "reconciler/ReactFiberCompleteWork.h"
#include "reconciler/ReactFiberCommitWork.h"
#include "reconciler/ReactFiberCommitEffects.h"
#include "reconciler/ReactFiberHooks.h"
#include "reconciler/ReactChildFiber.h"
#include "reconciler/ReactFiber.h"
#include "reconciler/ReactFiberRoot.h"

using namespace react::reconciler;

// =============================================================================
// ExecutionContext 测试
// =============================================================================

TEST(ExecutionContextTest, BitwiseOperations) {
    // 测试无上下文
    EXPECT_EQ(static_cast<uint8_t>(ExecutionContext::NoContext), 0);
    
    // 测试 OR 操作
    auto combined = ExecutionContext::BatchedContext | ExecutionContext::RenderContext;
    EXPECT_TRUE(hasContext(combined, ExecutionContext::BatchedContext));
    EXPECT_TRUE(hasContext(combined, ExecutionContext::RenderContext));
    EXPECT_FALSE(hasContext(combined, ExecutionContext::CommitContext));
    
    // 测试 AND 操作
    auto masked = combined & ExecutionContext::RenderContext;
    EXPECT_EQ(masked, ExecutionContext::RenderContext);
    
    // 测试 NOT 操作
    auto negated = ~ExecutionContext::BatchedContext;
    EXPECT_FALSE(hasContext(negated, ExecutionContext::BatchedContext));
}

TEST(ExecutionContextTest, ContextFlags) {
    // 验证上下文标志值
    EXPECT_EQ(static_cast<uint8_t>(ExecutionContext::NoContext), 0b000);
    EXPECT_EQ(static_cast<uint8_t>(ExecutionContext::BatchedContext), 0b001);
    EXPECT_EQ(static_cast<uint8_t>(ExecutionContext::RenderContext), 0b010);
    EXPECT_EQ(static_cast<uint8_t>(ExecutionContext::CommitContext), 0b100);
}

// =============================================================================
// RootExitStatus 测试
// =============================================================================

TEST(RootExitStatusTest, Values) {
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::InProgress), 0);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::FatalErrored), 1);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::Errored), 2);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::Suspended), 3);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::SuspendedWithDelay), 4);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::Completed), 5);
    EXPECT_EQ(static_cast<uint8_t>(RootExitStatus::SuspendedAtTheShell), 6);
}

// =============================================================================
// SuspendedReason 测试
// =============================================================================

TEST(SuspendedReasonTest, Values) {
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::NotSuspended), 0);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnError), 1);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnData), 2);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnImmediate), 3);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnInstance), 4);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnInstanceAndReadyToContinue), 5);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnDeprecatedThrowPromise), 6);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedAndReadyToContinue), 7);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnHydration), 8);
    EXPECT_EQ(static_cast<uint8_t>(SuspendedReason::SuspendedOnAction), 9);
}

// =============================================================================
// WorkLoopState 测试
// =============================================================================

TEST(WorkLoopStateTest, DefaultValues) {
    WorkLoopState state;
    
    EXPECT_EQ(state.executionContext, ExecutionContext::NoContext);
    EXPECT_EQ(state.workInProgressRoot, nullptr);
    EXPECT_EQ(state.workInProgress, nullptr);
    EXPECT_EQ(state.workInProgressRootRenderLanes, NoLanes);
    EXPECT_EQ(state.workInProgressSuspendedReason, SuspendedReason::NotSuspended);
    EXPECT_FALSE(state.workInProgressRootDidSkipSuspendedSiblings);
    EXPECT_FALSE(state.workInProgressRootIsPrerendering);
    EXPECT_FALSE(state.workInProgressRootDidAttachPingListener);
    EXPECT_EQ(state.entangledRenderLanes, NoLanes);
    EXPECT_EQ(state.workInProgressRootExitStatus, RootExitStatus::InProgress);
    EXPECT_EQ(state.workInProgressRootSkippedLanes, NoLanes);
    EXPECT_FALSE(state.workInProgressRootDidIncludeRecursiveRenderUpdate);
    EXPECT_FALSE(state.didIncludeCommitPhaseUpdate);
}

TEST(WorkLoopStateTest, Reset) {
    WorkLoopState state;
    
    // 修改状态
    state.executionContext = ExecutionContext::RenderContext;
    state.workInProgressRootRenderLanes = SyncLane;
    state.workInProgressSuspendedReason = SuspendedReason::SuspendedOnData;
    state.workInProgressRootExitStatus = RootExitStatus::Completed;
    
    // 重置
    state.reset();
    
    // 验证重置后的值
    EXPECT_EQ(state.executionContext, ExecutionContext::NoContext);
    EXPECT_EQ(state.workInProgressRootRenderLanes, NoLanes);
    EXPECT_EQ(state.workInProgressSuspendedReason, SuspendedReason::NotSuspended);
    EXPECT_EQ(state.workInProgressRootExitStatus, RootExitStatus::InProgress);
}

TEST(WorkLoopStateTest, Constants) {
    EXPECT_EQ(WorkLoopState::FALLBACK_THROTTLE_MS, 300.0);
    EXPECT_EQ(WorkLoopState::RENDER_TIMEOUT_MS, 500.0);
}

// =============================================================================
// CapturedValue 测试（简单版）
// =============================================================================

TEST(SimpleCapturedValueTest, Creation) {
    // 使用简单结构体来测试基本捕获值
    struct SimpleCapturedValue {
      std::any value;
      std::optional<std::string> stack = std::nullopt;
    };
    
    SimpleCapturedValue cv;
    cv.value = std::string("test error");
    cv.stack = "at Component (file.js:10)";
    
    EXPECT_EQ(std::any_cast<std::string>(cv.value), "test error");
    EXPECT_TRUE(cv.stack.has_value());
    EXPECT_EQ(cv.stack.value(), "at Component (file.js:10)");
}

// =============================================================================
// SchedulerInterface 测试
// =============================================================================

TEST(SchedulerInterfaceTest, DefaultValues) {
    SchedulerInterface scheduler;
    
    EXPECT_EQ(scheduler.scheduleCallback, nullptr);
    EXPECT_EQ(scheduler.cancelCallback, nullptr);
    EXPECT_EQ(scheduler.shouldYield, nullptr);
    EXPECT_EQ(scheduler.now, nullptr);
    EXPECT_EQ(scheduler.requestPaint, nullptr);
}

TEST(SchedulerInterfaceTest, WithCallbacks) {
    SchedulerInterface scheduler;
    
    bool yieldCalled = false;
    double currentTime = 100.0;
    
    scheduler.shouldYield = [&yieldCalled]() {
        yieldCalled = true;
        return false;
    };
    
    scheduler.now = [currentTime]() {
        return currentTime;
    };
    
    EXPECT_FALSE(scheduler.shouldYield());
    EXPECT_TRUE(yieldCalled);
    EXPECT_EQ(scheduler.now(), 100.0);
}

// =============================================================================
// HookFlags 测试
// =============================================================================

TEST(HookFlagsTest, Values) {
    EXPECT_EQ(HookNoFlags, 0b0000000);
    EXPECT_EQ(HookHasEffect, 0b0000001);
    EXPECT_EQ(HookInsertion, 0b0000010);
    EXPECT_EQ(HookLayout, 0b0000100);
    EXPECT_EQ(HookPassive, 0b0001000);
}

TEST(HookFlagsTest, Combinations) {
    HookFlags layoutWithEffect = HookLayout | HookHasEffect;
    EXPECT_EQ(layoutWithEffect, 0b0000101);
    
    HookFlags passiveWithEffect = HookPassive | HookHasEffect;
    EXPECT_EQ(passiveWithEffect, 0b0001001);
    
    // 检查是否有效果
    EXPECT_TRUE((layoutWithEffect & HookHasEffect) != 0);
    EXPECT_TRUE((layoutWithEffect & HookLayout) != 0);
    EXPECT_FALSE((layoutWithEffect & HookPassive) != 0);
}

// =============================================================================
// CommitPhase 测试
// =============================================================================

TEST(CommitPhaseTest, Values) {
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::BeforeMutation), 0);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::Mutation), 1);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::LayoutPhase), 2);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::PassivePhase), 3);
}

// =============================================================================
// Effect 结构测试
// =============================================================================

TEST(EffectTest, Creation) {
    Effect effect;
    effect.tag = HookPassive | HookHasEffect;
    effect.deps = {std::any(1), std::any(std::string("test"))};
    
    EXPECT_EQ(effect.tag, HookPassive | HookHasEffect);
    EXPECT_EQ(effect.deps.size(), 2);
    EXPECT_EQ(effect.next, nullptr);
}

TEST(EffectTest, LinkedList) {
    auto effect1 = std::make_shared<Effect>();
    auto effect2 = std::make_shared<Effect>();
    auto effect3 = std::make_shared<Effect>();
    
    effect1->tag = HookLayout | HookHasEffect;
    effect2->tag = HookPassive | HookHasEffect;
    effect3->tag = HookInsertion | HookHasEffect;
    
    effect1->next = effect2;
    effect2->next = effect3;
    effect3->next = effect1;  // 循环链表
    
    // 遍历链表
    int count = 0;
    auto current = effect1;
    do {
        count++;
        current = current->next;
    } while (current != effect1);
    
    EXPECT_EQ(count, 3);
}

// =============================================================================
// Hook 结构测试
// =============================================================================

TEST(HookTest, Creation) {
    Hook hook;
    hook.memoizedState = 42;
    hook.baseState = 42;
    
    EXPECT_EQ(std::any_cast<int>(hook.memoizedState), 42);
    EXPECT_EQ(std::any_cast<int>(hook.baseState), 42);
    EXPECT_EQ(hook.baseQueue, nullptr);
    EXPECT_EQ(hook.next, nullptr);
}

TEST(HookTest, LinkedList) {
    auto hook1 = std::make_shared<Hook>();
    auto hook2 = std::make_shared<Hook>();
    auto hook3 = std::make_shared<Hook>();
    
    hook1->memoizedState = std::string("state1");
    hook2->memoizedState = std::string("state2");
    hook3->memoizedState = std::string("state3");
    
    hook1->next = hook2;
    hook2->next = hook3;
    
    // 遍历链表
    std::vector<std::string> states;
    auto current = hook1;
    while (current) {
        states.push_back(std::any_cast<std::string>(current->memoizedState));
        current = current->next;
    }
    
    EXPECT_EQ(states.size(), 3);
    EXPECT_EQ(states[0], "state1");
    EXPECT_EQ(states[1], "state2");
    EXPECT_EQ(states[2], "state3");
}

// =============================================================================
// BeginWorkContext 测试
// =============================================================================

TEST(BeginWorkContextTest, DefaultValues) {
    BeginWorkContext context;
    EXPECT_FALSE(context.didReceiveUpdate);
}

// =============================================================================
// HostContext 测试
// =============================================================================

TEST(HostContextTest, DefaultValues) {
    HostContext context;
    EXPECT_FALSE(context.rootInstance.has_value());
    EXPECT_FALSE(context.hostContext.has_value());
    EXPECT_FALSE(context.namespace_.has_value());
}

// =============================================================================
// ChildReconciler 接口测试
// =============================================================================

TEST(ChildReconcilerTest, DefaultValues) {
    ChildReconciler reconciler;
    EXPECT_EQ(reconciler.reconcileChildFibers, nullptr);
    EXPECT_EQ(reconciler.mountChildFibers, nullptr);
    EXPECT_EQ(reconciler.cloneChildFibers, nullptr);
}

// =============================================================================
// ReactChildFiberReconciler 测试
// =============================================================================

TEST(ReactChildFiberReconcilerTest, ShouldTrackSideEffects) {
    ReactChildFiberReconciler mountReconciler(false);
    ReactChildFiberReconciler updateReconciler(true);
    
    EXPECT_FALSE(mountReconciler.shouldTrackSideEffects());
    EXPECT_TRUE(updateReconciler.shouldTrackSideEffects());
}

// =============================================================================
// Dispatcher 接口测试
// =============================================================================

TEST(DispatcherTest, DefaultValues) {
    Dispatcher dispatcher;
    
    EXPECT_EQ(dispatcher.useState, nullptr);
    EXPECT_EQ(dispatcher.useReducer, nullptr);
    EXPECT_EQ(dispatcher.useEffect, nullptr);
    EXPECT_EQ(dispatcher.useLayoutEffect, nullptr);
    EXPECT_EQ(dispatcher.useMemo, nullptr);
    EXPECT_EQ(dispatcher.useCallback, nullptr);
    EXPECT_EQ(dispatcher.useRef, nullptr);
    EXPECT_EQ(dispatcher.useContext, nullptr);
    EXPECT_EQ(dispatcher.useTransition, nullptr);
    EXPECT_EQ(dispatcher.useDeferredValue, nullptr);
    EXPECT_EQ(dispatcher.useId, nullptr);
}

// =============================================================================
// HookType 枚举测试
// =============================================================================

TEST(HookTypeTest, Values) {
    EXPECT_EQ(static_cast<int>(HookType::UseState), 0);
    EXPECT_EQ(static_cast<int>(HookType::UseReducer), 1);
    EXPECT_EQ(static_cast<int>(HookType::UseEffect), 2);
    EXPECT_EQ(static_cast<int>(HookType::UseLayoutEffect), 3);
    EXPECT_EQ(static_cast<int>(HookType::UseInsertionEffect), 4);
    EXPECT_EQ(static_cast<int>(HookType::UseMemo), 5);
    EXPECT_EQ(static_cast<int>(HookType::UseCallback), 6);
    EXPECT_EQ(static_cast<int>(HookType::UseRef), 7);
    EXPECT_EQ(static_cast<int>(HookType::UseContext), 8);
}

// =============================================================================
// FunctionComponentUpdateQueue 测试
// =============================================================================

TEST(FunctionComponentUpdateQueueTest, DefaultValues) {
    FunctionComponentUpdateQueue queue;
    
    EXPECT_EQ(queue.lastEffect, nullptr);
    EXPECT_FALSE(queue.events.has_value());
    EXPECT_FALSE(queue.stores.has_value());
    EXPECT_FALSE(queue.memoCache.has_value());
}

// =============================================================================
// placeSingleChild 测试
// =============================================================================

TEST(PlaceSingleChildTest, WithoutTracking) {
    auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
    fiber->flags = NoFlags;
    // alternate 默认是 expired weak_ptr
    
    placeSingleChild(fiber, false);
    
    // 不跟踪副作用时不应添加 Placement 标志
    EXPECT_EQ(fiber->flags & Placement, 0);
}

TEST(PlaceSingleChildTest, WithTracking) {
    auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
    fiber->flags = NoFlags;
    // alternate 默认是 expired weak_ptr
    
    placeSingleChild(fiber, true);
    
    // 跟踪副作用且没有 alternate 时应添加 Placement 标志
    EXPECT_EQ(fiber->flags & Placement, Placement);
}

TEST(PlaceSingleChildTest, WithAlternate) {
    auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
    auto alternate = std::make_shared<Fiber>(FunctionComponent, NoMode);
    fiber->flags = NoFlags;
    fiber->alternate = alternate;  // 设置 alternate
    
    placeSingleChild(fiber, true);
    
    // 有 alternate 时不应添加 Placement 标志
    EXPECT_EQ(fiber->flags & Placement, 0);
}

// =============================================================================
// 主函数
// =============================================================================

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
