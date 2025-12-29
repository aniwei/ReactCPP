/**
 * React Fiber 数据结构单元测试
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiber.js
 * @source reactjs/packages/react-reconciler/src/ReactFiberLane.js
 * @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js
 */

#include <gtest/gtest.h>
#include "reconciler/ReactFiberLane.h"
#include "reconciler/ReactTypeOfMode.h"
#include "reconciler/ReactRootTags.h"
#include "reconciler/ReactFiber.h"
#include "reconciler/ReactFiberRoot.h"

namespace react::reconciler::tests {


// ReactFiberLane 测试


TEST(ReactFiberLaneTest, LaneConstants) {
    // @source:41-42
    EXPECT_EQ(NoLanes, 0);
    EXPECT_EQ(NoLane, 0);
    
    // @source:44-45
    EXPECT_EQ(SyncHydrationLane, 0b0000000000000000000000000000001);
    EXPECT_EQ(SyncLane, 0b0000000000000000000000000000010);
    EXPECT_EQ(SyncLaneIndex, 1);
    
    // @source:48-49
    EXPECT_EQ(InputContinuousHydrationLane, 0b0000000000000000000000000000100);
    EXPECT_EQ(InputContinuousLane, 0b0000000000000000000000000001000);
    
    // @source:51-52
    EXPECT_EQ(DefaultHydrationLane, 0b0000000000000000000000000010000);
    EXPECT_EQ(DefaultLane, 0b0000000000000000000000000100000);
}

TEST(ReactFiberLaneTest, TransitionLanes) {
    // @source:60
    EXPECT_EQ(TransitionLanes, 0b0000000001111111111111100000000);
    
    // 验证 TransitionLanes 包含所有 TransitionLane
    EXPECT_TRUE((TransitionLanes & TransitionLane1) != 0);
    EXPECT_TRUE((TransitionLanes & TransitionLane14) != 0);
}

TEST(ReactFiberLaneTest, RetryLanes) {
    // @source:76
    EXPECT_EQ(RetryLanes, 0b0000011110000000000000000000000);
    EXPECT_EQ(SomeRetryLane, RetryLane1);
}

TEST(ReactFiberLaneTest, SpecialLanes) {
    // @source:84-92
    EXPECT_EQ(SelectiveHydrationLane, 0b0000100000000000000000000000000);
    EXPECT_EQ(IdleHydrationLane, 0b0001000000000000000000000000000);
    EXPECT_EQ(IdleLane, 0b0010000000000000000000000000000);
    EXPECT_EQ(OffscreenLane, 0b0100000000000000000000000000000);
    EXPECT_EQ(DeferredLane, 0b1000000000000000000000000000000);
}

TEST(ReactFiberLaneTest, MergeLanes) {
    Lanes a = SyncLane;
    Lanes b = DefaultLane;
    
    EXPECT_EQ(mergeLanes(a, b), SyncLane | DefaultLane);
    EXPECT_EQ(mergeLanes(NoLanes, SyncLane), SyncLane);
    EXPECT_EQ(mergeLanes(NoLanes, NoLanes), NoLanes);
}

TEST(ReactFiberLaneTest, IncludesSomeLane) {
    EXPECT_TRUE(includesSomeLane(SyncUpdateLanes, SyncLane));
    EXPECT_TRUE(includesSomeLane(SyncUpdateLanes, DefaultLane));
    EXPECT_FALSE(includesSomeLane(SyncUpdateLanes, IdleLane));
    EXPECT_FALSE(includesSomeLane(NoLanes, SyncLane));
}

TEST(ReactFiberLaneTest, IncludesNonIdleWork) {
    EXPECT_TRUE(includesNonIdleWork(SyncLane));
    EXPECT_TRUE(includesNonIdleWork(DefaultLane));
    EXPECT_TRUE(includesNonIdleWork(TransitionLane1));
    EXPECT_FALSE(includesNonIdleWork(IdleLane));
    EXPECT_FALSE(includesNonIdleWork(OffscreenLane));
}

TEST(ReactFiberLaneTest, IncludesTransitionLane) {
    EXPECT_TRUE(includesTransitionLane(TransitionLane1));
    EXPECT_TRUE(includesTransitionLane(TransitionLanes));
    EXPECT_FALSE(includesTransitionLane(SyncLane));
    EXPECT_FALSE(includesTransitionLane(IdleLane));
}

TEST(ReactFiberLaneTest, RemoveLanes) {
    Lanes lanes = SyncLane | DefaultLane | IdleLane;
    
    EXPECT_EQ(removeLanes(lanes, SyncLane), DefaultLane | IdleLane);
    EXPECT_EQ(removeLanes(lanes, DefaultLane | IdleLane), SyncLane);
    EXPECT_EQ(removeLanes(lanes, NoLanes), lanes);
}

TEST(ReactFiberLaneTest, GetHighestPriorityLane) {
    // 最右边的位是最高优先级
    EXPECT_EQ(getHighestPriorityLane(SyncLane | DefaultLane), SyncLane);
    EXPECT_EQ(getHighestPriorityLane(DefaultLane | IdleLane), DefaultLane);
    EXPECT_EQ(getHighestPriorityLane(TransitionLanes), TransitionLane1);
}

TEST(ReactFiberLaneTest, LaneToIndex) {
    EXPECT_EQ(laneToIndex(SyncHydrationLane), 0);
    EXPECT_EQ(laneToIndex(SyncLane), 1);
    EXPECT_EQ(laneToIndex(DefaultLane), 5);
}

TEST(ReactFiberLaneTest, GetLabelForLane) {
    EXPECT_STREQ(getLabelForLane(SyncLane), "Sync");
    EXPECT_STREQ(getLabelForLane(DefaultLane), "Default");
    EXPECT_STREQ(getLabelForLane(TransitionLane1), "Transition");
    EXPECT_STREQ(getLabelForLane(IdleLane), "Idle");
}


// ReactTypeOfMode 测试


TEST(ReactTypeOfModeTest, ModeConstants) {
    // @source:13
    EXPECT_EQ(NoMode, 0b0000000);
    // @source:15
    EXPECT_EQ(ConcurrentMode, 0b0000001);
    // @source:16
    EXPECT_EQ(ProfileMode, 0b0000010);
    // @source:18
    EXPECT_EQ(StrictLegacyMode, 0b0001000);
    // @source:19
    EXPECT_EQ(StrictEffectsMode, 0b0010000);
    // @source:22
    EXPECT_EQ(SuspenseyImagesMode, 0b0100000);
}

TEST(ReactTypeOfModeTest, ModeCombinations) {
    TypeOfMode mode = ConcurrentMode | StrictLegacyMode;
    
    EXPECT_TRUE(isConcurrentMode(mode));
    EXPECT_TRUE(isStrictMode(mode));
    EXPECT_FALSE(isProfileMode(mode));
}


// ReactRootTags 测试


TEST(ReactRootTagsTest, RootTagConstants) {
    // @source:13
    EXPECT_EQ(LegacyRoot, 0);
    // @source:14
    EXPECT_EQ(ConcurrentRoot, 1);
}

TEST(ReactRootTagsTest, RootTagChecks) {
    EXPECT_TRUE(isConcurrentRoot(ConcurrentRoot));
    EXPECT_FALSE(isConcurrentRoot(LegacyRoot));
    EXPECT_TRUE(isLegacyRoot(LegacyRoot));
    EXPECT_FALSE(isLegacyRoot(ConcurrentRoot));
}


// ReactFiber 测试


TEST(ReactFiberTest, CreateFiberBasic) {
    // 使用默认构造函数测试基本 Fiber 结构
    auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
    
    EXPECT_NE(fiber, nullptr);
    EXPECT_EQ(fiber->tag, FunctionComponent);
    EXPECT_TRUE(fiber->key.isUndefined());
    EXPECT_EQ(fiber->mode, NoMode);
    EXPECT_EQ(fiber->flags, NoFlags);
    EXPECT_EQ(fiber->lanes, NoLanes);
}

TEST(ReactFiberTest, CreateHostRootFiber) {
    auto fiber = createHostRootFiber(ConcurrentRoot, true, false);
    
    EXPECT_NE(fiber, nullptr);
    EXPECT_EQ(fiber->tag, HostRoot);
    EXPECT_TRUE(isConcurrentMode(fiber->mode));
    EXPECT_TRUE(isStrictMode(fiber->mode));
}

TEST(ReactFiberTest, FiberTreeStructure) {
    auto parent = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    auto child1 = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    auto child2 = std::make_shared<Fiber>(HostText, ConcurrentMode);
    
    // 建立树结构
    parent->child = child1;
    child1->setReturn(parent);
    child1->sibling = child2;
    child2->setReturn(parent);
    child2->index = 1;
    
    // 验证结构
    EXPECT_EQ(parent->child, child1);
    EXPECT_EQ(child1->getReturn(), parent);
    EXPECT_EQ(child1->sibling, child2);
    EXPECT_EQ(child2->getReturn(), parent);
    EXPECT_EQ(child2->index, 1);
}

TEST(ReactFiberTest, FiberAlternate) {
    auto current = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    auto workInProgress = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    
    // 设置双向引用
    current->setAlternate(workInProgress);
    workInProgress->setAlternate(current);
    
    // 验证双缓冲
    EXPECT_EQ(current->getAlternate(), workInProgress);
    EXPECT_EQ(workInProgress->getAlternate(), current);
}

TEST(ReactFiberTest, FiberDeletions) {
    auto parent = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    auto child = std::make_shared<Fiber>(HostComponent, ConcurrentMode);
    
    EXPECT_FALSE(parent->hasDeletions());
    
    parent->addDeletion(child);
    
    EXPECT_TRUE(parent->hasDeletions());
    EXPECT_TRUE((parent->flags & ChildDeletion) != 0);
    EXPECT_EQ(parent->deletions.size(), 1);
    
    parent->clearDeletions();
    
    EXPECT_FALSE(parent->hasDeletions());
    EXPECT_EQ(parent->deletions.size(), 0);
}


// ReactFiberRoot 测试


TEST(ReactFiberRootTest, CreateFiberRoot) {
    auto root = createFiberRoot(
        std::string("container"),
        ConcurrentRoot,
        true,
        "react-",
        nullptr,
        nullptr,
        nullptr
    );
    
    EXPECT_NE(root, nullptr);
    EXPECT_EQ(root->tag, ConcurrentRoot);
    EXPECT_EQ(root->identifierPrefix, "react-");
    EXPECT_NE(root->current, nullptr);
    EXPECT_EQ(root->current->tag, HostRoot);
}

TEST(ReactFiberRootTest, RootLaneManagement) {
    auto root = std::make_shared<FiberRoot>();
    
    EXPECT_FALSE(root->hasPendingWork());
    
    // 标记更新
    root->markRootUpdated(DefaultLane, 1000.0);
    
    EXPECT_TRUE(root->hasPendingWork());
    EXPECT_TRUE((root->pendingLanes & DefaultLane) != 0);
    
    // 标记完成
    root->markRootFinished(DefaultLane);
    
    EXPECT_FALSE(root->hasPendingWork());
}

TEST(ReactFiberRootTest, RootSuspension) {
    auto root = std::make_shared<FiberRoot>();
    
    root->markRootUpdated(TransitionLane1 | TransitionLane2, 1000.0);
    
    // 挂起一些 lanes
    root->markRootSuspended(TransitionLane1);
    
    EXPECT_TRUE((root->suspendedLanes & TransitionLane1) != 0);
    EXPECT_FALSE((root->suspendedLanes & TransitionLane2) != 0);
    
    // Ping
    root->markRootPinged(TransitionLane1);
    
    EXPECT_TRUE((root->pingedLanes & TransitionLane1) != 0);
}

} // namespace react::reconciler::tests
