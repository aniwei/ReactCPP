/**
 * ReactFiberClassUpdateQueue 测试
 * 
 * 测试更新队列的核心功能，包括：
 * - 更新队列初始化
 * - 更新创建和入队
 * - 更新处理和状态计算
 * - 队列克隆和捕获更新
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberClassUpdateQueue.js
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <functional>

#include "../ReactFiberClassUpdateQueue.h"
#include "../ReactFiber.h"
#include "../ReactFiberRoot.h"
#include "../ReactFiberLane.h"
#include "../ReactFiberFlags.h"
#include "../ReactWorkTags.h"

using namespace react::reconciler;

// =============================================================================
// 测试辅助函数
// =============================================================================

/**
 * 创建测试用的 Fiber 节点
 */
FiberRef createTestClassFiber(WorkTag tag = ClassComponent) {
    auto fiber = std::make_shared<Fiber>();
    fiber->tag = tag;
    fiber->lanes = NoLanes;
    fiber->childLanes = NoLanes;
    fiber->flags = NoFlags;
    fiber->memoizedState = std::any{};
    fiber->updateQueue = std::any{};
    return fiber;
}

/**
 * 创建测试用的 FiberRoot
 */
FiberRootRef createTestClassRoot() {
    auto root = std::make_shared<FiberRoot>();
    root->pendingLanes = NoLanes;
    root->suspendedLanes = NoLanes;
    root->pingedLanes = NoLanes;
    root->expiredLanes = NoLanes;
    return root;
}

// =============================================================================
// UpdateTag 测试
// =============================================================================

class ClassUpdateTagTest : public ::testing::Test {};

TEST_F(ClassUpdateTagTest, UpdateTagValues) {
    // 验证更新标签的值与 JS 对齐
    EXPECT_EQ(static_cast<uint8_t>(UpdateTag::UpdateState), 0);
    EXPECT_EQ(static_cast<uint8_t>(UpdateTag::ReplaceState), 1);
    EXPECT_EQ(static_cast<uint8_t>(UpdateTag::ForceUpdate), 2);
    EXPECT_EQ(static_cast<uint8_t>(UpdateTag::CaptureUpdate), 3);
}

TEST_F(ClassUpdateTagTest, ConstantCompatibility) {
    // 验证常量与枚举值一致
    EXPECT_EQ(UpdateStateTag, static_cast<uint8_t>(UpdateTag::UpdateState));
    EXPECT_EQ(ReplaceStateTag, static_cast<uint8_t>(UpdateTag::ReplaceState));
    EXPECT_EQ(ForceUpdateTag, static_cast<uint8_t>(UpdateTag::ForceUpdate));
    EXPECT_EQ(CaptureUpdateTag, static_cast<uint8_t>(UpdateTag::CaptureUpdate));
}

// =============================================================================
// ClassUpdate 结构测试
// =============================================================================

class ClassUpdateStructTest : public ::testing::Test {};

TEST_F(ClassUpdateStructTest, DefaultConstruction) {
    ClassUpdate<int> update;
    
    EXPECT_EQ(update.lane, NoLane);
    EXPECT_EQ(update.tag, UpdateTag::UpdateState);
    EXPECT_FALSE(update.payload.has_value());
    EXPECT_EQ(update.callback, nullptr);
    EXPECT_EQ(update.next, nullptr);
}

TEST_F(ClassUpdateStructTest, LaneConstruction) {
    ClassUpdate<std::string> update(SyncLane);
    
    EXPECT_EQ(update.lane, SyncLane);
    EXPECT_EQ(update.tag, UpdateTag::UpdateState);
}

TEST_F(ClassUpdateStructTest, LaneAndTagConstruction) {
    ClassUpdate<double> update(DefaultLane, UpdateTag::ForceUpdate);
    
    EXPECT_EQ(update.lane, DefaultLane);
    EXPECT_EQ(update.tag, UpdateTag::ForceUpdate);
}

TEST_F(ClassUpdateStructTest, PayloadStorage) {
    ClassUpdate<std::any> update;
    update.payload = std::make_any<int>(42);
    
    EXPECT_TRUE(update.payload.has_value());
    EXPECT_EQ(std::any_cast<int>(update.payload), 42);
}

TEST_F(ClassUpdateStructTest, CallbackStorage) {
    ClassUpdate<int> update;
    bool callbackCalled = false;
    update.callback = [&callbackCalled]() { callbackCalled = true; };
    
    EXPECT_TRUE(update.callback != nullptr);
    update.callback();
    EXPECT_TRUE(callbackCalled);
}

TEST_F(ClassUpdateStructTest, LinkedListChaining) {
    auto update1 = std::make_shared<ClassUpdate<int>>(SyncLane);
    auto update2 = std::make_shared<ClassUpdate<int>>(DefaultLane);
    auto update3 = std::make_shared<ClassUpdate<int>>(InputContinuousLane);
    
    update1->next = update2;
    update2->next = update3;
    
    EXPECT_EQ(update1->next, update2);
    EXPECT_EQ(update2->next, update3);
    EXPECT_EQ(update3->next, nullptr);
}

// =============================================================================
// ClassSharedQueue 测试
// =============================================================================

class ClassSharedQueueTest : public ::testing::Test {};

TEST_F(ClassSharedQueueTest, DefaultConstruction) {
    ClassSharedQueue<int> sharedQueue;
    
    EXPECT_EQ(sharedQueue.pending, nullptr);
    EXPECT_EQ(sharedQueue.lanes, NoLanes);
    EXPECT_TRUE(sharedQueue.hiddenCallbacks.empty());
}

TEST_F(ClassSharedQueueTest, PendingUpdate) {
    ClassSharedQueue<std::any> sharedQueue;
    auto update = std::make_shared<ClassUpdate<std::any>>(SyncLane);
    sharedQueue.pending = update;
    
    EXPECT_EQ(sharedQueue.pending, update);
}

TEST_F(ClassSharedQueueTest, LanesTracking) {
    ClassSharedQueue<int> sharedQueue;
    sharedQueue.lanes = SyncLane;
    
    EXPECT_EQ(sharedQueue.lanes, SyncLane);
    
    sharedQueue.lanes = mergeLanes(sharedQueue.lanes, DefaultLane);
    EXPECT_TRUE(includesSomeLane(sharedQueue.lanes, SyncLane));
    EXPECT_TRUE(includesSomeLane(sharedQueue.lanes, DefaultLane));
}

TEST_F(ClassSharedQueueTest, HiddenCallbacks) {
    ClassSharedQueue<int> sharedQueue;
    bool cb1Called = false;
    bool cb2Called = false;
    
    sharedQueue.hiddenCallbacks.push_back([&cb1Called]() { cb1Called = true; });
    sharedQueue.hiddenCallbacks.push_back([&cb2Called]() { cb2Called = true; });
    
    EXPECT_EQ(sharedQueue.hiddenCallbacks.size(), 2);
    
    for (auto& cb : sharedQueue.hiddenCallbacks) {
        cb();
    }
    
    EXPECT_TRUE(cb1Called);
    EXPECT_TRUE(cb2Called);
}

// =============================================================================
// ClassUpdateQueue 测试
// =============================================================================

class ClassUpdateQueueTest : public ::testing::Test {};

TEST_F(ClassUpdateQueueTest, DefaultConstruction) {
    ClassUpdateQueue<int> queue;
    
    EXPECT_EQ(queue.firstBaseUpdate, nullptr);
    EXPECT_EQ(queue.lastBaseUpdate, nullptr);
    EXPECT_NE(queue.shared, nullptr);
    EXPECT_TRUE(queue.callbacks.empty());
}

TEST_F(ClassUpdateQueueTest, SharedQueueCreation) {
    ClassUpdateQueue<std::string> queue;
    
    EXPECT_NE(queue.shared, nullptr);
    EXPECT_EQ(queue.shared->pending, nullptr);
    EXPECT_EQ(queue.shared->lanes, NoLanes);
}

TEST_F(ClassUpdateQueueTest, BaseUpdateChaining) {
    ClassUpdateQueue<int> queue;
    auto update1 = std::make_shared<ClassUpdate<int>>(SyncLane);
    auto update2 = std::make_shared<ClassUpdate<int>>(DefaultLane);
    
    queue.firstBaseUpdate = update1;
    queue.lastBaseUpdate = update2;
    update1->next = update2;
    
    EXPECT_EQ(queue.firstBaseUpdate, update1);
    EXPECT_EQ(queue.lastBaseUpdate, update2);
    EXPECT_EQ(queue.firstBaseUpdate->next, update2);
}

TEST_F(ClassUpdateQueueTest, CallbacksStorage) {
    ClassUpdateQueue<int> queue;
    int callCount = 0;
    
    queue.callbacks.push_back([&callCount]() { callCount++; });
    queue.callbacks.push_back([&callCount]() { callCount++; });
    queue.callbacks.push_back([&callCount]() { callCount++; });
    
    EXPECT_EQ(queue.callbacks.size(), 3);
    
    for (auto& cb : queue.callbacks) {
        cb();
    }
    
    EXPECT_EQ(callCount, 3);
}

// =============================================================================
// ClassUpdateQueueGlobals 测试
// =============================================================================

class ClassUpdateQueueGlobalsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClassUpdateQueueGlobals::instance().reset();
    }
    
    void TearDown() override {
        ClassUpdateQueueGlobals::instance().reset();
    }
};

TEST_F(ClassUpdateQueueGlobalsTest, SingletonInstance) {
    auto& instance1 = ClassUpdateQueueGlobals::instance();
    auto& instance2 = ClassUpdateQueueGlobals::instance();
    
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(ClassUpdateQueueGlobalsTest, InitialState) {
    auto& globals = ClassUpdateQueueGlobals::instance();
    
    EXPECT_FALSE(globals.hasForceUpdate);
    EXPECT_FALSE(globals.didReadFromEntangledAsyncAction);
    EXPECT_EQ(globals.currentlyProcessingQueue, nullptr);
}

TEST_F(ClassUpdateQueueGlobalsTest, ForceUpdateFlag) {
    auto& globals = ClassUpdateQueueGlobals::instance();
    
    globals.hasForceUpdate = true;
    EXPECT_TRUE(globals.hasForceUpdate);
    
    globals.reset();
    EXPECT_FALSE(globals.hasForceUpdate);
}

TEST_F(ClassUpdateQueueGlobalsTest, ResetCurrentlyProcessingQueue) {
    auto& globals = ClassUpdateQueueGlobals::instance();
    
    globals.currentlyProcessingQueue = std::make_shared<AnyClassSharedQueue>();
    EXPECT_NE(globals.currentlyProcessingQueue, nullptr);
    
    globals.resetCurrentlyProcessingQueue();
    EXPECT_EQ(globals.currentlyProcessingQueue, nullptr);
}

// =============================================================================
// initializeClassUpdateQueue 测试
// =============================================================================

class InitializeClassUpdateQueueTest : public ::testing::Test {};

TEST_F(InitializeClassUpdateQueueTest, InitializeFiber) {
    auto fiber = createTestClassFiber();
    
    initializeClassUpdateQueue(fiber);
    
    EXPECT_TRUE(fiber->updateQueue.has_value());
}

TEST_F(InitializeClassUpdateQueueTest, QueueStructure) {
    auto fiber = createTestClassFiber();
    fiber->memoizedState = std::make_any<int>(42);
    
    initializeClassUpdateQueue(fiber);
    
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_NE(queue, nullptr);
    EXPECT_EQ(queue->firstBaseUpdate, nullptr);
    EXPECT_EQ(queue->lastBaseUpdate, nullptr);
    EXPECT_NE(queue->shared, nullptr);
    EXPECT_TRUE(queue->callbacks.empty());
}

TEST_F(InitializeClassUpdateQueueTest, SharedQueueInitialized) {
    auto fiber = createTestClassFiber();
    
    initializeClassUpdateQueue(fiber);
    
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_NE(queue->shared, nullptr);
    EXPECT_EQ(queue->shared->pending, nullptr);
    EXPECT_EQ(queue->shared->lanes, NoLanes);
}

// =============================================================================
// createClassUpdate 测试
// =============================================================================

class CreateClassUpdateTest : public ::testing::Test {};

TEST_F(CreateClassUpdateTest, CreateWithLane) {
    auto update = createClassUpdate(SyncLane);
    
    EXPECT_NE(update, nullptr);
    EXPECT_EQ(update->lane, SyncLane);
    EXPECT_EQ(update->tag, UpdateTag::UpdateState);
    EXPECT_EQ(update->next, nullptr);
}

TEST_F(CreateClassUpdateTest, CreateWithDifferentLanes) {
    auto syncUpdate = createClassUpdate(SyncLane);
    auto defaultUpdate = createClassUpdate(DefaultLane);
    auto inputUpdate = createClassUpdate(InputContinuousLane);
    
    EXPECT_EQ(syncUpdate->lane, SyncLane);
    EXPECT_EQ(defaultUpdate->lane, DefaultLane);
    EXPECT_EQ(inputUpdate->lane, InputContinuousLane);
}

TEST_F(CreateClassUpdateTest, PayloadIsEmpty) {
    auto update = createClassUpdate(SyncLane);
    
    EXPECT_FALSE(update->payload.has_value());
}

TEST_F(CreateClassUpdateTest, CallbackIsNull) {
    auto update = createClassUpdate(SyncLane);
    
    EXPECT_EQ(update->callback, nullptr);
}

// =============================================================================
// cloneClassUpdateQueue 测试
// =============================================================================

class CloneClassUpdateQueueTest : public ::testing::Test {};

TEST_F(CloneClassUpdateQueueTest, CloneWhenSameQueue) {
    auto current = createTestClassFiber();
    auto workInProgress = createTestClassFiber();
    
    initializeClassUpdateQueue(current);
    
    // 让 work-in-progress 共享同一个队列
    workInProgress->updateQueue = current->updateQueue;
    
    // 克隆应该创建新队列
    cloneClassUpdateQueue(current, workInProgress);
    
    // 验证 updateQueue 不为空
    EXPECT_TRUE(workInProgress->updateQueue.has_value());
}

TEST_F(CloneClassUpdateQueueTest, SharedQueueStaysShared) {
    auto current = createTestClassFiber();
    auto workInProgress = createTestClassFiber();
    
    initializeClassUpdateQueue(current);
    auto currentQueue = getClassUpdateQueue(current);
    auto originalShared = currentQueue->shared;
    
    workInProgress->updateQueue = current->updateQueue;
    cloneClassUpdateQueue(current, workInProgress);
    
    auto wipQueue = getClassUpdateQueue(workInProgress);
    // 共享部分应该保持共享
    EXPECT_EQ(wipQueue->shared, originalShared);
}

// =============================================================================
// enqueueClassUpdate 测试
// =============================================================================

class EnqueueClassUpdateTest : public ::testing::Test {};

TEST_F(EnqueueClassUpdateTest, EnqueueToFiberWithoutQueue) {
    auto fiber = createTestClassFiber();
    // 不初始化 updateQueue
    fiber->updateQueue = std::any{};
    
    auto update = createClassUpdate(SyncLane);
    auto root = enqueueClassUpdate(fiber, update, SyncLane);
    
    // 没有队列时应返回 nullptr
    EXPECT_EQ(root, nullptr);
}

TEST_F(EnqueueClassUpdateTest, EnqueueToInitializedFiber) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto update = createClassUpdate(SyncLane);
    update->payload = std::make_any<int>(42);
    
    // 入队操作
    auto root = enqueueClassUpdate(fiber, update, SyncLane);
    
    // 验证队列存在
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_NE(queue, nullptr);
    EXPECT_NE(queue->shared->pending, nullptr);
}

TEST_F(EnqueueClassUpdateTest, EnqueueMultipleUpdates) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto update1 = createClassUpdate(SyncLane);
    auto update2 = createClassUpdate(DefaultLane);
    
    enqueueClassUpdate(fiber, update1, SyncLane);
    enqueueClassUpdate(fiber, update2, DefaultLane);
    
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_NE(queue->shared->pending, nullptr);
    
    // 验证 lanes 合并
    EXPECT_TRUE(includesSomeLane(queue->shared->lanes, SyncLane));
    EXPECT_TRUE(includesSomeLane(queue->shared->lanes, DefaultLane));
}

// =============================================================================
// getStateFromClassUpdate 测试
// =============================================================================

class GetStateFromClassUpdateTest : public ::testing::Test {};

TEST_F(GetStateFromClassUpdateTest, UpdateStateWithPayload) {
    auto fiber = createTestClassFiber();
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    auto update = std::make_shared<AnyClassUpdate>(SyncLane);
    
    update->tag = UpdateTag::UpdateState;
    update->payload = std::make_any<int>(100);
    
    std::any prevState = std::make_any<int>(50);
    std::any nextProps;
    std::any instance;
    
    auto newState = getStateFromClassUpdate(
        fiber, queue, update, prevState, nextProps, instance
    );
    
    // 新状态应该是 payload
    EXPECT_TRUE(newState.has_value());
    EXPECT_EQ(std::any_cast<int>(newState), 100);
}

TEST_F(GetStateFromClassUpdateTest, ForceUpdateDoesNotChangeState) {
    auto fiber = createTestClassFiber();
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    auto update = std::make_shared<AnyClassUpdate>(SyncLane);
    
    update->tag = UpdateTag::ForceUpdate;
    
    std::any prevState = std::make_any<int>(42);
    std::any nextProps;
    std::any instance;
    
    ClassUpdateQueueGlobals::instance().reset();
    
    auto newState = getStateFromClassUpdate(
        fiber, queue, update, prevState, nextProps, instance
    );
    
    // 状态应该不变
    EXPECT_TRUE(newState.has_value());
    EXPECT_EQ(std::any_cast<int>(newState), 42);
    
    // hasForceUpdate 应该被设置
    EXPECT_TRUE(ClassUpdateQueueGlobals::instance().hasForceUpdate);
    
    ClassUpdateQueueGlobals::instance().reset();
}

TEST_F(GetStateFromClassUpdateTest, ReplaceStateWithPayload) {
    auto fiber = createTestClassFiber();
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    auto update = std::make_shared<AnyClassUpdate>(SyncLane);
    
    update->tag = UpdateTag::ReplaceState;
    update->payload = std::make_any<std::string>("new state");
    
    std::any prevState = std::make_any<std::string>("old state");
    std::any nextProps;
    std::any instance;
    
    auto newState = getStateFromClassUpdate(
        fiber, queue, update, prevState, nextProps, instance
    );
    
    EXPECT_TRUE(newState.has_value());
    EXPECT_EQ(std::any_cast<std::string>(newState), "new state");
}

// =============================================================================
// ForceUpdate 相关函数测试
// =============================================================================

class ClassForceUpdateTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClassUpdateQueueGlobals::instance().reset();
    }
    
    void TearDown() override {
        ClassUpdateQueueGlobals::instance().reset();
    }
};

TEST_F(ClassForceUpdateTest, ResetHasForceUpdate) {
    ClassUpdateQueueGlobals::instance().hasForceUpdate = true;
    
    resetHasForceUpdateBeforeProcessing();
    
    EXPECT_FALSE(ClassUpdateQueueGlobals::instance().hasForceUpdate);
}

TEST_F(ClassForceUpdateTest, CheckHasForceUpdate) {
    EXPECT_FALSE(checkHasForceUpdateAfterProcessing());
    
    ClassUpdateQueueGlobals::instance().hasForceUpdate = true;
    
    EXPECT_TRUE(checkHasForceUpdateAfterProcessing());
}

// =============================================================================
// 回调函数测试
// =============================================================================

class ClassCallbackTest : public ::testing::Test {};

TEST_F(ClassCallbackTest, CallCallbackExecutes) {
    bool called = false;
    std::function<void()> callback = [&called]() { called = true; };
    
    callClassUpdateCallback(callback, std::any{});
    
    EXPECT_TRUE(called);
}

TEST_F(ClassCallbackTest, NullCallbackSafe) {
    std::function<void()> nullCallback = nullptr;
    
    // 不应崩溃
    callClassUpdateCallback(nullCallback, std::any{});
}

TEST_F(ClassCallbackTest, DeferHiddenCallbacks) {
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    
    bool cb1Called = false;
    bool cb2Called = false;
    queue->callbacks.push_back([&cb1Called]() { cb1Called = true; });
    queue->callbacks.push_back([&cb2Called]() { cb2Called = true; });
    
    deferHiddenClassCallbacks(queue);
    
    // 回调应该移动到 hiddenCallbacks
    EXPECT_TRUE(queue->callbacks.empty());
    EXPECT_EQ(queue->shared->hiddenCallbacks.size(), 2);
    
    // 执行隐藏回调
    for (auto& cb : queue->shared->hiddenCallbacks) {
        cb();
    }
    
    EXPECT_TRUE(cb1Called);
    EXPECT_TRUE(cb2Called);
}

TEST_F(ClassCallbackTest, CommitHiddenCallbacks) {
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    
    bool cb1Called = false;
    bool cb2Called = false;
    queue->shared->hiddenCallbacks.push_back([&cb1Called]() { cb1Called = true; });
    queue->shared->hiddenCallbacks.push_back([&cb2Called]() { cb2Called = true; });
    
    commitHiddenClassCallbacks(queue, std::any{});
    
    EXPECT_TRUE(cb1Called);
    EXPECT_TRUE(cb2Called);
    EXPECT_TRUE(queue->shared->hiddenCallbacks.empty());
}

TEST_F(ClassCallbackTest, CommitCallbacks) {
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    
    int callCount = 0;
    queue->callbacks.push_back([&callCount]() { callCount++; });
    queue->callbacks.push_back([&callCount]() { callCount++; });
    queue->callbacks.push_back([&callCount]() { callCount++; });
    
    commitClassCallbacks(queue, std::any{});
    
    EXPECT_EQ(callCount, 3);
    EXPECT_TRUE(queue->callbacks.empty());
}

// =============================================================================
// 便捷函数测试
// =============================================================================

class ClassHelperFunctionsTest : public ::testing::Test {};

TEST_F(ClassHelperFunctionsTest, GetClassUpdateQueueFromFiber) {
    auto fiber = createTestClassFiber();
    
    // 未初始化时返回 nullptr
    EXPECT_EQ(getClassUpdateQueue(fiber), nullptr);
    
    // 初始化后返回队列
    initializeClassUpdateQueue(fiber);
    EXPECT_NE(getClassUpdateQueue(fiber), nullptr);
}

TEST_F(ClassHelperFunctionsTest, HasClassPendingUpdatesEmpty) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    EXPECT_FALSE(hasClassPendingUpdates(fiber));
}

TEST_F(ClassHelperFunctionsTest, HasClassPendingUpdatesWithPending) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto queue = getClassUpdateQueue(fiber);
    auto update = std::make_shared<AnyClassUpdate>(SyncLane);
    queue->shared->pending = update;
    
    EXPECT_TRUE(hasClassPendingUpdates(fiber));
}

TEST_F(ClassHelperFunctionsTest, HasClassPendingUpdatesWithBaseUpdate) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto queue = getClassUpdateQueue(fiber);
    auto update = std::make_shared<AnyClassUpdate>(SyncLane);
    queue->firstBaseUpdate = update;
    
    EXPECT_TRUE(hasClassPendingUpdates(fiber));
}

// =============================================================================
// enqueueClassCapturedUpdate 测试
// =============================================================================

class EnqueueClassCapturedUpdateTest : public ::testing::Test {};

TEST_F(EnqueueClassCapturedUpdateTest, EnqueueToEmptyQueue) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto capturedUpdate = std::make_shared<AnyClassUpdate>(SyncLane);
    capturedUpdate->tag = UpdateTag::CaptureUpdate;
    capturedUpdate->payload = std::make_any<std::string>("error");
    
    enqueueClassCapturedUpdate(fiber, capturedUpdate);
    
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_EQ(queue->firstBaseUpdate, capturedUpdate);
    EXPECT_EQ(queue->lastBaseUpdate, capturedUpdate);
}

TEST_F(EnqueueClassCapturedUpdateTest, EnqueueToExistingQueue) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    // 先添加一个更新
    auto existingUpdate = std::make_shared<AnyClassUpdate>(DefaultLane);
    auto queue = getClassUpdateQueue(fiber);
    queue->firstBaseUpdate = existingUpdate;
    queue->lastBaseUpdate = existingUpdate;
    
    // 添加捕获的更新
    auto capturedUpdate = std::make_shared<AnyClassUpdate>(SyncLane);
    capturedUpdate->tag = UpdateTag::CaptureUpdate;
    
    enqueueClassCapturedUpdate(fiber, capturedUpdate);
    
    // 捕获的更新应该追加到末尾
    EXPECT_EQ(queue->firstBaseUpdate, existingUpdate);
    EXPECT_EQ(queue->lastBaseUpdate, capturedUpdate);
    EXPECT_EQ(existingUpdate->next, capturedUpdate);
}

// =============================================================================
// 类型别名测试
// =============================================================================

class ClassTypeAliasTest : public ::testing::Test {};

TEST_F(ClassTypeAliasTest, AnyClassUpdateIsCorrectType) {
    AnyClassUpdate update;
    EXPECT_EQ(update.lane, NoLane);
}

TEST_F(ClassTypeAliasTest, AnyClassSharedQueueIsCorrectType) {
    AnyClassSharedQueue sharedQueue;
    EXPECT_EQ(sharedQueue.pending, nullptr);
}

TEST_F(ClassTypeAliasTest, AnyClassUpdateQueueIsCorrectType) {
    AnyClassUpdateQueue queue;
    EXPECT_NE(queue.shared, nullptr);
}

// =============================================================================
// 边界情况测试
// =============================================================================

class ClassEdgeCaseTest : public ::testing::Test {};

TEST_F(ClassEdgeCaseTest, CircularPendingQueue) {
    auto queue = std::make_shared<AnyClassUpdateQueue>();
    
    // 创建循环链表
    auto update1 = std::make_shared<AnyClassUpdate>(SyncLane);
    auto update2 = std::make_shared<AnyClassUpdate>(DefaultLane);
    auto update3 = std::make_shared<AnyClassUpdate>(InputContinuousLane);
    
    update1->next = update2;
    update2->next = update3;
    update3->next = update1;  // 循环
    
    queue->shared->pending = update3;  // 指向最后一个
    
    // 验证可以正确访问
    EXPECT_EQ(queue->shared->pending, update3);
    EXPECT_EQ(queue->shared->pending->next, update1);
}

TEST_F(ClassEdgeCaseTest, EmptyPayloadHandling) {
    auto update = createClassUpdate(SyncLane);
    
    EXPECT_FALSE(update->payload.has_value());
}

TEST_F(ClassEdgeCaseTest, MultipleQueuesIndependent) {
    auto fiber1 = createTestClassFiber();
    auto fiber2 = createTestClassFiber();
    
    initializeClassUpdateQueue(fiber1);
    initializeClassUpdateQueue(fiber2);
    
    auto queue1 = getClassUpdateQueue(fiber1);
    auto queue2 = getClassUpdateQueue(fiber2);
    
    EXPECT_NE(queue1, queue2);
    EXPECT_NE(queue1->shared, queue2->shared);
}

// =============================================================================
// 调度更新便捷函数测试
// =============================================================================

class ClassScheduleUpdateTest : public ::testing::Test {};

TEST_F(ClassScheduleUpdateTest, ScheduleStateUpdate) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto root = scheduleClassUpdateOnFiber(
        fiber, 
        std::make_any<int>(42), 
        SyncLane
    );
    
    // 验证队列有待处理更新
    EXPECT_TRUE(hasClassPendingUpdates(fiber));
}

TEST_F(ClassScheduleUpdateTest, ScheduleForceUpdate) {
    auto fiber = createTestClassFiber();
    initializeClassUpdateQueue(fiber);
    
    auto root = scheduleClassForceUpdateOnFiber(fiber, SyncLane);
    
    // 验证队列有待处理更新
    EXPECT_TRUE(hasClassPendingUpdates(fiber));
    
    // 验证是 ForceUpdate
    auto queue = getClassUpdateQueue(fiber);
    EXPECT_EQ(queue->shared->pending->tag, UpdateTag::ForceUpdate);
}
