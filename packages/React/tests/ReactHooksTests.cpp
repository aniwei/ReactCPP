/**
 * React Fiber Hooks Tests
 * 
 * Phase 5: React Core Hooks 实现测试
 * 
 * 测试 useState, useReducer, useEffect, useMemo, useCallback, useRef 等
 */

#include <gtest/gtest.h>
#include <memory>
#include <functional>
#include <vector>
#include <string>

#include "../src/shared/objectIs.h"
#include "../src/reconciler/ReactFiberLane.h"
#include "../src/reconciler/ReactFiber.h"
#include "../src/reconciler/ReactFiberCommitEffects.h"
#include "../src/reconciler/ReactFiberHooks.h"
#include "../src/reconciler/ReactFiberNewContext.h"

using namespace react::shared;
using namespace react::reconciler;

// =============================================================================
// Object.is Tests
// =============================================================================

class ObjectIsTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ObjectIsTest, BasicEquality) {
    EXPECT_TRUE(objectIs(std::any(1), std::any(1)));
    EXPECT_TRUE(objectIs(std::any(true), std::any(true)));
    EXPECT_TRUE(objectIs(std::any(false), std::any(false)));
    EXPECT_TRUE(objectIs(std::any(std::string("hello")), std::any(std::string("hello"))));
}

TEST_F(ObjectIsTest, BasicInequality) {
    EXPECT_FALSE(objectIs(std::any(1), std::any(2)));
    EXPECT_FALSE(objectIs(std::any(true), std::any(false)));
    EXPECT_FALSE(objectIs(std::any(std::string("hello")), std::any(std::string("world"))));
}

TEST_F(ObjectIsTest, TypeMismatch) {
    EXPECT_FALSE(objectIs(std::any(1), std::any(1.0)));
    EXPECT_FALSE(objectIs(std::any(std::string("1")), std::any(1)));
}

TEST_F(ObjectIsTest, EmptyValues) {
    std::any empty1;
    std::any empty2;
    EXPECT_TRUE(objectIs(empty1, empty2));
}

TEST_F(ObjectIsTest, DoubleSpecialCases) {
    // NaN 与 NaN
    double nan1 = std::nan("1");
    double nan2 = std::nan("2");
    EXPECT_TRUE(objectIs(std::any(nan1), std::any(nan2)));
    
    // +0 与 -0
    double pos0 = 0.0;
    double neg0 = -0.0;
    EXPECT_FALSE(objectIs(std::any(pos0), std::any(neg0)));
}

TEST_F(ObjectIsTest, SharedPointerComparison) {
    auto ptr1 = std::make_shared<int>(42);
    auto ptr2 = ptr1;
    auto ptr3 = std::make_shared<int>(42);
    
    // 同一指针
    auto any1 = std::any(std::static_pointer_cast<void>(ptr1));
    auto any2 = std::any(std::static_pointer_cast<void>(ptr2));
    EXPECT_TRUE(objectIs(any1, any2));
    
    // 不同指针（即使值相同）
    auto any3 = std::any(std::static_pointer_cast<void>(ptr3));
    EXPECT_FALSE(objectIs(any1, any3));
}

// =============================================================================
// Hook Types Tests
// =============================================================================

class HookTypesTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(HookTypesTest, HookTypeEnumValues) {
    EXPECT_EQ(static_cast<int>(HookType::UseState), 0);
    EXPECT_NE(static_cast<int>(HookType::UseEffect), static_cast<int>(HookType::UseState));
    EXPECT_NE(static_cast<int>(HookType::UseMemo), static_cast<int>(HookType::UseCallback));
}

TEST_F(HookTypesTest, HookStructure) {
    Hook hook;
    EXPECT_FALSE(hook.memoizedState.has_value());
    EXPECT_FALSE(hook.baseState.has_value());
    EXPECT_EQ(hook.baseQueue, nullptr);
    EXPECT_FALSE(hook.queue.has_value());
    EXPECT_EQ(hook.next, nullptr);
}

TEST_F(HookTypesTest, HookLinkedList) {
    auto hook1 = std::make_shared<Hook>();
    auto hook2 = std::make_shared<Hook>();
    auto hook3 = std::make_shared<Hook>();
    
    hook1->memoizedState = std::any(1);
    hook2->memoizedState = std::any(2);
    hook3->memoizedState = std::any(3);
    
    hook1->next = hook2;
    hook2->next = hook3;
    
    EXPECT_EQ(std::any_cast<int>(hook1->memoizedState), 1);
    EXPECT_EQ(std::any_cast<int>(hook1->next->memoizedState), 2);
    EXPECT_EQ(std::any_cast<int>(hook1->next->next->memoizedState), 3);
    EXPECT_EQ(hook1->next->next->next, nullptr);
}

// =============================================================================
// Effect Advanced Tests
// =============================================================================

class EffectAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(EffectAdvancedTest, EffectStructure) {
  Effect effect;
  EXPECT_EQ(effect.tag, HookNoFlags);
  EXPECT_EQ(effect.create, nullptr);
  EXPECT_EQ(effect.inst.destroy, nullptr);
  EXPECT_TRUE(effect.deps.empty());
  EXPECT_EQ(effect.next, nullptr);
}

TEST_F(EffectAdvancedTest, EffectWithCreate) {
    auto effect = std::make_shared<Effect>();
    
    bool effectRan = false;
    bool cleanupRan = false;
    
    effect->create = [&effectRan, &cleanupRan]() {
        effectRan = true;
        return [&cleanupRan]() {
            cleanupRan = true;
        };
    };
    
    EXPECT_FALSE(effectRan);
    
    // 运行 effect
    auto cleanup = effect->create();
    EXPECT_TRUE(effectRan);
    EXPECT_FALSE(cleanupRan);
    
    // 运行 cleanup
    cleanup();
    EXPECT_TRUE(cleanupRan);
}

TEST_F(EffectAdvancedTest, EffectCircularList) {
    auto effect1 = std::make_shared<Effect>();
    auto effect2 = std::make_shared<Effect>();
    auto effect3 = std::make_shared<Effect>();
    
    effect1->tag = HookPassive;
    effect2->tag = HookLayout;
    effect3->tag = HookInsertion;
    
    // 创建循环链表
    effect1->next = effect2;
    effect2->next = effect3;
    effect3->next = effect1;
    
    // 验证循环
    EXPECT_EQ(effect1->next->next->next, effect1);
    
    // 遍历计数
    int count = 0;
    auto current = effect1;
    do {
        count++;
        current = current->next;
    } while (current != effect1);
    
    EXPECT_EQ(count, 3);
}

// =============================================================================
// Update Queue Tests
// =============================================================================

class UpdateQueueTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(UpdateQueueTest, HookUpdateStructure) {
    HookUpdate<int, int> update;
    EXPECT_EQ(update.lane, NoLane);
    EXPECT_EQ(update.revertLane, NoLane);
    EXPECT_FALSE(update.hasEagerState);
    EXPECT_FALSE(update.eagerState.has_value());
    EXPECT_EQ(update.next, nullptr);
}

TEST_F(UpdateQueueTest, HookUpdateQueueStructure) {
    HookUpdateQueue<int, int> queue;
    EXPECT_EQ(queue.pending, nullptr);
    EXPECT_EQ(queue.lanes, NoLanes);
    EXPECT_EQ(queue.dispatch, nullptr);
    EXPECT_EQ(queue.lastRenderedReducer, nullptr);
    EXPECT_FALSE(queue.lastRenderedState.has_value());
}

TEST_F(UpdateQueueTest, CircularUpdateQueue) {
    auto queue = std::make_shared<HookUpdateQueue<int, int>>();
    
    auto update1 = std::make_shared<HookUpdate<int, int>>();
    update1->action = 1;
    
    auto update2 = std::make_shared<HookUpdate<int, int>>();
    update2->action = 2;
    
    auto update3 = std::make_shared<HookUpdate<int, int>>();
    update3->action = 3;
    
    // 添加第一个更新
    update1->next = update1;
    queue->pending = update1;
    
    // 添加第二个更新
    update2->next = update1->next;
    update1->next = update2;
    queue->pending = update2;
    
    // 添加第三个更新
    update3->next = update2->next;
    update2->next = update3;
    queue->pending = update3;
    
    // 验证循环
    auto first = queue->pending->next;
    EXPECT_EQ(first->action, 1);
    EXPECT_EQ(first->next->action, 2);
    EXPECT_EQ(first->next->next->action, 3);
    EXPECT_EQ(first->next->next->next, first);
}

// =============================================================================
// Dispatcher Advanced Tests
// =============================================================================

class DispatcherAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(DispatcherAdvancedTest, DispatcherStructure) {
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
}

TEST_F(DispatcherAdvancedTest, UseStateDispatcher) {
    Dispatcher dispatcher;
    
    int callCount = 0;
    dispatcher.useState = [&callCount](std::any initialState) 
        -> std::pair<std::any, std::function<void(std::any)>> {
        callCount++;
        return {initialState, [](std::any) {}};
    };
    
    auto [state, setState] = dispatcher.useState(std::any(42));
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(std::any_cast<int>(state), 42);
}

TEST_F(DispatcherAdvancedTest, UseReducerDispatcher) {
    Dispatcher dispatcher;
    
    dispatcher.useReducer = [](
        std::function<std::any(std::any, std::any)> reducer,
        std::any initialArg,
        std::optional<std::function<std::any(std::any)>> init
    ) -> std::pair<std::any, std::function<void(std::any)>> {
        std::any initialState;
        if (init.has_value()) {
            initialState = init.value()(initialArg);
        } else {
            initialState = initialArg;
        }
        
        (void)reducer;  // Suppress unused warning
        auto dispatch = [](std::any) {};
        return {initialState, dispatch};
    };
    
    auto countReducer = [](std::any state, std::any action) -> std::any {
        int count = std::any_cast<int>(state);
        std::string type = std::any_cast<std::string>(action);
        if (type == "increment") {
            return count + 1;
        } else if (type == "decrement") {
            return count - 1;
        }
        return count;
    };
    
    auto [state, dispatch] = dispatcher.useReducer(countReducer, std::any(0), std::nullopt);
    EXPECT_EQ(std::any_cast<int>(state), 0);
}

// =============================================================================
// HooksContext Tests
// =============================================================================

class HooksContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(HooksContextTest, DefaultState) {
    HooksContext context;
    EXPECT_EQ(context.currentlyRenderingFiber, nullptr);
    EXPECT_EQ(context.currentHook, nullptr);
    EXPECT_EQ(context.workInProgressHook, nullptr);
    EXPECT_FALSE(context.didScheduleRenderPhaseUpdate);
    EXPECT_FALSE(context.didScheduleRenderPhaseUpdateDuringThisPass);
    EXPECT_EQ(context.renderPhaseUpdateCount, 0);
}

TEST_F(HooksContextTest, Reset) {
    HooksContext context;
    
    // 设置一些值
    context.currentlyRenderingFiber = std::make_shared<Fiber>(
        FunctionComponent, ConcurrentMode
    );
    context.didScheduleRenderPhaseUpdate = true;
    context.renderPhaseUpdateCount = 5;
    
    // 重置
    context.reset();
    
    EXPECT_EQ(context.currentlyRenderingFiber, nullptr);
    EXPECT_FALSE(context.didScheduleRenderPhaseUpdate);
    EXPECT_EQ(context.renderPhaseUpdateCount, 0);
}

// =============================================================================
// Context Tests
// =============================================================================

class ContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ContextTest, CreateContext) {
    auto context = createContext<int>(42);
    EXPECT_EQ(context->_defaultValue, 42);
    EXPECT_EQ(context->_currentValue, 42);
}

TEST_F(ContextTest, CreateContextWithString) {
    auto context = createContext<std::string>("default");
    EXPECT_EQ(context->_defaultValue, "default");
    EXPECT_EQ(context->_currentValue, "default");
}

TEST_F(ContextTest, ContextDependencyStructure) {
    ContextDependency dep;
    EXPECT_FALSE(dep.context.has_value());
    EXPECT_FALSE(dep.memoizedValue.has_value());
    EXPECT_EQ(dep.next, nullptr);
}

TEST_F(ContextTest, ContextDependencyChain) {
    auto dep1 = std::make_shared<ContextDependency>();
    auto dep2 = std::make_shared<ContextDependency>();
    auto dep3 = std::make_shared<ContextDependency>();
    
    dep1->memoizedValue = std::any(1);
    dep2->memoizedValue = std::any(2);
    dep3->memoizedValue = std::any(3);
    
    dep1->next = dep2;
    dep2->next = dep3;
    
    EXPECT_EQ(std::any_cast<int>(dep1->memoizedValue), 1);
    EXPECT_EQ(std::any_cast<int>(dep1->next->memoizedValue), 2);
    EXPECT_EQ(std::any_cast<int>(dep1->next->next->memoizedValue), 3);
}

// =============================================================================
// FunctionComponentUpdateQueue Advanced Tests
// =============================================================================

class FunctionComponentUpdateQueueAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(FunctionComponentUpdateQueueAdvancedTest, DefaultState) {
    FunctionComponentUpdateQueue queue;
    EXPECT_EQ(queue.lastEffect, nullptr);
    EXPECT_FALSE(queue.events.has_value());
    EXPECT_FALSE(queue.stores.has_value());
    EXPECT_FALSE(queue.memoCache.has_value());
}

TEST_F(FunctionComponentUpdateQueueAdvancedTest, AddEffects) {
    auto queue = std::make_shared<FunctionComponentUpdateQueue>();
    
    auto effect1 = std::make_shared<Effect>();
    effect1->tag = HookPassive;
    
    auto effect2 = std::make_shared<Effect>();
    effect2->tag = HookLayout;
    
    // 添加第一个 effect
    effect1->next = effect1;
    queue->lastEffect = effect1;
    
    // 添加第二个 effect
    effect2->next = effect1->next;  // point to first
    effect1->next = effect2;
    queue->lastEffect = effect2;
    
    // 验证
    EXPECT_EQ(queue->lastEffect->tag, HookLayout);
    EXPECT_EQ(queue->lastEffect->next->tag, HookPassive);
    EXPECT_EQ(queue->lastEffect->next->next, queue->lastEffect);
}

// =============================================================================
// Basic State Reducer Tests
// =============================================================================

class BasicStateReducerTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(BasicStateReducerTest, DirectValue) {
    auto result = basicStateReducer<int>(10, std::any(20));
    EXPECT_EQ(result, 20);
}

TEST_F(BasicStateReducerTest, FunctionValue) {
    std::function<int(int)> incrementFn = [](int prev) { return prev + 1; };
    auto result = basicStateReducer<int>(10, std::any(incrementFn));
    EXPECT_EQ(result, 11);
}

TEST_F(BasicStateReducerTest, MultipleFunctionCalls) {
    std::function<int(int)> doubleFn = [](int prev) { return prev * 2; };
    
    int state = 1;
    state = basicStateReducer<int>(state, std::any(doubleFn));
    EXPECT_EQ(state, 2);
    
    state = basicStateReducer<int>(state, std::any(doubleFn));
    EXPECT_EQ(state, 4);
    
    state = basicStateReducer<int>(state, std::any(doubleFn));
    EXPECT_EQ(state, 8);
}

// =============================================================================
// Hook Flags Advanced Tests
// =============================================================================

class HookFlagsAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(HookFlagsAdvancedTest, FlagValues) {
    EXPECT_EQ(HookNoFlags, 0);
    EXPECT_EQ(HookHasEffect, 1);
    EXPECT_EQ(HookInsertion, 2);
    EXPECT_EQ(HookLayout, 4);
    EXPECT_EQ(HookPassive, 8);
}

TEST_F(HookFlagsAdvancedTest, FlagCombination) {
    HookFlags flags = HookHasEffect | HookPassive;
    EXPECT_TRUE(flags & HookHasEffect);
    EXPECT_TRUE(flags & HookPassive);
    EXPECT_FALSE(flags & HookLayout);
}

TEST_F(HookFlagsAdvancedTest, EffectTagging) {
    auto passiveEffect = std::make_shared<Effect>();
    passiveEffect->tag = static_cast<HookFlags>(HookHasEffect | HookPassive);
    
    auto layoutEffect = std::make_shared<Effect>();
    layoutEffect->tag = static_cast<HookFlags>(HookHasEffect | HookLayout);
    
    EXPECT_TRUE(passiveEffect->tag & HookPassive);
    EXPECT_FALSE(passiveEffect->tag & HookLayout);
    
    EXPECT_TRUE(layoutEffect->tag & HookLayout);
    EXPECT_FALSE(layoutEffect->tag & HookPassive);
}

// =============================================================================
// Integration Tests - Simulated Hook Flow
// =============================================================================

class HookFlowTest : public ::testing::Test {
protected:
    HooksContext context;
    FiberRef fiber;
    
    void SetUp() override {
        fiber = std::make_shared<Fiber>(FunctionComponent, ConcurrentMode);
        context.currentlyRenderingFiber = fiber;
    }
};

TEST_F(HookFlowTest, SimulatedMountState) {
    // 模拟 mountState
    auto hook = std::make_shared<Hook>();
    int initialState = 0;
    
    hook->memoizedState = initialState;
    hook->baseState = initialState;
    
    auto queue = std::make_shared<HookUpdateQueue<int, int>>();
    queue->pending = nullptr;
    queue->lastRenderedState = initialState;
    
    hook->queue = queue;
    
    // 验证状态
    EXPECT_EQ(std::any_cast<int>(hook->memoizedState), 0);
    
    // 设置到 fiber
    fiber->memoizedState = hook;
    context.workInProgressHook = hook;
    
    EXPECT_EQ(context.workInProgressHook->memoizedState.type(), typeid(int));
}

TEST_F(HookFlowTest, SimulatedMultipleHooks) {
    // 模拟多个 hooks
    auto hook1 = std::make_shared<Hook>();
    hook1->memoizedState = 0;  // useState(0)
    
    auto hook2 = std::make_shared<Hook>();
    hook2->memoizedState = std::string("hello");  // useState("hello")
    
    auto hook3 = std::make_shared<Hook>();
    hook3->memoizedState = std::vector<std::any>{1, 2, 3};  // useMemo result
    
    // 链接 hooks
    hook1->next = hook2;
    hook2->next = hook3;
    
    // 设置到 fiber
    fiber->memoizedState = hook1;
    
    // 验证 hook 链
    auto firstHook = std::any_cast<HookRef>(fiber->memoizedState);
    EXPECT_EQ(std::any_cast<int>(firstHook->memoizedState), 0);
    EXPECT_EQ(std::any_cast<std::string>(firstHook->next->memoizedState), "hello");
}

TEST_F(HookFlowTest, SimulatedEffectChain) {
    auto updateQueue = std::make_shared<FunctionComponentUpdateQueue>();
    
    // 创建 passive effect
    auto effect1 = std::make_shared<Effect>();
    effect1->tag = static_cast<HookFlags>(HookHasEffect | HookPassive);
    effect1->deps = {std::any(1), std::any(2)};
    
    // 创建 layout effect
    auto effect2 = std::make_shared<Effect>();
    effect2->tag = static_cast<HookFlags>(HookHasEffect | HookLayout);
    effect2->deps = {};
    
    // 建立循环链表
    effect1->next = effect1;
    updateQueue->lastEffect = effect1;
    
    // 添加第二个
    effect2->next = effect1->next;
    effect1->next = effect2;
    updateQueue->lastEffect = effect2;
    
    // 验证
    fiber->updateQueue = updateQueue;
    
    auto queue = std::any_cast<FunctionComponentUpdateQueueRef>(fiber->updateQueue);
    EXPECT_EQ(queue->lastEffect->tag, static_cast<HookFlags>(HookHasEffect | HookLayout));
}

// =============================================================================
// Dependencies Comparison Tests
// =============================================================================

class DepsComparisonTest : public ::testing::Test {
protected:
    void SetUp() override {}
    
    bool areHookInputsEqual(
        const std::vector<std::any>& nextDeps,
        const std::vector<std::any>& prevDeps
    ) {
        if (prevDeps.empty()) return false;
        if (nextDeps.size() != prevDeps.size()) return false;
        
        for (size_t i = 0; i < prevDeps.size(); i++) {
            if (!objectIs(nextDeps[i], prevDeps[i])) {
                return false;
            }
        }
        return true;
    }
};

TEST_F(DepsComparisonTest, EmptyDeps) {
    std::vector<std::any> empty;
    EXPECT_FALSE(areHookInputsEqual(empty, empty));
}

TEST_F(DepsComparisonTest, SameDeps) {
    std::vector<std::any> deps1 = {std::any(1), std::any(2), std::any(3)};
    std::vector<std::any> deps2 = {std::any(1), std::any(2), std::any(3)};
    EXPECT_TRUE(areHookInputsEqual(deps1, deps2));
}

TEST_F(DepsComparisonTest, DifferentDeps) {
    std::vector<std::any> deps1 = {std::any(1), std::any(2), std::any(3)};
    std::vector<std::any> deps2 = {std::any(1), std::any(2), std::any(4)};
    EXPECT_FALSE(areHookInputsEqual(deps1, deps2));
}

TEST_F(DepsComparisonTest, DifferentLength) {
    std::vector<std::any> deps1 = {std::any(1), std::any(2)};
    std::vector<std::any> deps2 = {std::any(1), std::any(2), std::any(3)};
    EXPECT_FALSE(areHookInputsEqual(deps1, deps2));
}

TEST_F(DepsComparisonTest, MixedTypes) {
    std::vector<std::any> deps1 = {std::any(1), std::any(std::string("test")), std::any(true)};
    std::vector<std::any> deps2 = {std::any(1), std::any(std::string("test")), std::any(true)};
    EXPECT_TRUE(areHookInputsEqual(deps1, deps2));
}
