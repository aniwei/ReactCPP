/**
 * ReactWorkTags 和 ReactFiberFlags 单元测试
 * 
 * @source reactjs/packages/react-reconciler/src/ReactWorkTags.js
 * @source reactjs/packages/react-reconciler/src/ReactFiberFlags.js
 * 
 * 验证 C++ 实现的常量与 JS 端完全一致
 */

#include <gtest/gtest.h>
#include "reconciler/ReactWorkTags.h"
#include "reconciler/ReactFiberFlags.h"

namespace react::reconciler::tests {

// =============================================================================
// ReactWorkTags 测试
// =============================================================================

TEST(ReactWorkTagsTest, BasicWorkTags) {
    // @source:45
    EXPECT_EQ(FunctionComponent, 0);
    // @source:46
    EXPECT_EQ(ClassComponent, 1);
    // @source:47
    EXPECT_EQ(HostRoot, 3);
    // @source:48
    EXPECT_EQ(HostPortal, 4);
    // @source:49
    EXPECT_EQ(HostComponent, 5);
    // @source:50
    EXPECT_EQ(HostText, 6);
    // @source:51
    EXPECT_EQ(Fragment, 7);
    // @source:52
    EXPECT_EQ(Mode, 8);
}

TEST(ReactWorkTagsTest, ContextWorkTags) {
    // @source:53
    EXPECT_EQ(ContextConsumer, 9);
    // @source:54
    EXPECT_EQ(ContextProvider, 10);
}

TEST(ReactWorkTagsTest, AdvancedWorkTags) {
    // @source:55
    EXPECT_EQ(ForwardRef, 11);
    // @source:56
    EXPECT_EQ(Profiler, 12);
    // @source:57
    EXPECT_EQ(SuspenseComponent, 13);
    // @source:58
    EXPECT_EQ(MemoComponent, 14);
    // @source:59
    EXPECT_EQ(SimpleMemoComponent, 15);
    // @source:60
    EXPECT_EQ(LazyComponent, 16);
    // @source:61
    EXPECT_EQ(IncompleteClassComponent, 17);
    // @source:62
    EXPECT_EQ(DehydratedFragment, 18);
    // @source:63
    EXPECT_EQ(SuspenseListComponent, 19);
}

TEST(ReactWorkTagsTest, ExtendedWorkTags) {
    // @source:64
    EXPECT_EQ(ScopeComponent, 21);
    // @source:65
    EXPECT_EQ(OffscreenComponent, 22);
    // @source:66
    EXPECT_EQ(LegacyHiddenComponent, 23);
    // @source:67
    EXPECT_EQ(CacheComponent, 24);
    // @source:68
    EXPECT_EQ(TracingMarkerComponent, 25);
    // @source:69
    EXPECT_EQ(HostHoistable, 26);
    // @source:70
    EXPECT_EQ(HostSingleton, 27);
    // @source:71
    EXPECT_EQ(IncompleteFunctionComponent, 28);
    // @source:72
    EXPECT_EQ(Throw, 29);
    // @source:73
    EXPECT_EQ(ViewTransitionComponent, 30);
    // @source:74
    EXPECT_EQ(ActivityComponent, 31);
}

TEST(ReactWorkTagsTest, GetWorkTagName) {
    EXPECT_STREQ(getWorkTagName(FunctionComponent), "FunctionComponent");
    EXPECT_STREQ(getWorkTagName(HostComponent), "HostComponent");
    EXPECT_STREQ(getWorkTagName(HostText), "HostText");
    EXPECT_STREQ(getWorkTagName(255), "Unknown");
}

// =============================================================================
// ReactFiberFlags 测试
// =============================================================================

TEST(ReactFiberFlagsTest, CoreFlags) {
    // @source:18 - 这些值不能改变，DevTools 依赖它们
    EXPECT_EQ(NoFlags, 0b0000000000000000000000000000000);
    // @source:19
    EXPECT_EQ(PerformedWork, 0b0000000000000000000000000000001);
    // @source:20
    EXPECT_EQ(Placement, 0b0000000000000000000000000000010);
    // @source:21
    EXPECT_EQ(DidCapture, 0b0000000000000000000000010000000);
    // @source:22
    EXPECT_EQ(Hydrating, 0b0000000000000000001000000000000);
}

TEST(ReactFiberFlagsTest, MutableFlags) {
    // @source:25
    EXPECT_EQ(Update, 0b0000000000000000000000000000100);
    // @source:26
    EXPECT_EQ(Cloned, 0b0000000000000000000000000001000);
    // @source:28
    EXPECT_EQ(ChildDeletion, 0b0000000000000000000000000010000);
    // @source:29
    EXPECT_EQ(ContentReset, 0b0000000000000000000000000100000);
    // @source:30
    EXPECT_EQ(Callback, 0b0000000000000000000000001000000);
    // @source:33
    EXPECT_EQ(ForceClientRender, 0b0000000000000000000000100000000);
    // @source:34
    EXPECT_EQ(Ref, 0b0000000000000000000001000000000);
    // @source:35
    EXPECT_EQ(Snapshot, 0b0000000000000000000010000000000);
    // @source:36
    EXPECT_EQ(Passive, 0b0000000000000000000100000000000);
    // @source:39
    EXPECT_EQ(Visibility, 0b0000000000000000010000000000000);
    // @source:40
    EXPECT_EQ(StoreConsistency, 0b0000000000000000100000000000000);
}

TEST(ReactFiberFlagsTest, ReusedFlags) {
    // @source:44
    EXPECT_EQ(Hydrate, Callback);
    // @source:45
    EXPECT_EQ(ScheduleRetry, StoreConsistency);
    // @source:46
    EXPECT_EQ(ShouldSuspendCommit, Visibility);
    // @source:47
    EXPECT_EQ(ViewTransitionNamedMount, ShouldSuspendCommit);
    // @source:48
    EXPECT_EQ(DidDefer, ContentReset);
    // @source:49
    EXPECT_EQ(FormReset, Snapshot);
    // @source:50
    EXPECT_EQ(AffectedParentLayout, ContentReset);
}

TEST(ReactFiberFlagsTest, CombinedFlags) {
    // @source:52-53
    EXPECT_EQ(LifecycleEffectMask, Passive | Update | Callback | Ref | Snapshot | StoreConsistency);
    // @source:56
    EXPECT_EQ(HostEffectMask, 0b0000000000000000111111111111111);
}

TEST(ReactFiberFlagsTest, InternalFlags) {
    // @source:59
    EXPECT_EQ(Incomplete, 0b0000000000000001000000000000000);
    // @source:60
    EXPECT_EQ(ShouldCapture, 0b0000000000000010000000000000000);
    // @source:61
    EXPECT_EQ(ForceUpdateForLegacySuspense, 0b0000000000000100000000000000000);
    // @source:62
    EXPECT_EQ(DidPropagateContext, 0b0000000000001000000000000000000);
    // @source:63
    EXPECT_EQ(NeedsPropagation, 0b0000000000010000000000000000000);
    // @source:64
    EXPECT_EQ(Forked, 0b0000000000100000000000000000000);
}

TEST(ReactFiberFlagsTest, StaticFlags) {
    // @source:71
    EXPECT_EQ(SnapshotStatic, 0b0000000001000000000000000000000);
    // @source:72
    EXPECT_EQ(LayoutStatic, 0b0000000010000000000000000000000);
    // @source:73
    EXPECT_EQ(RefStatic, LayoutStatic);
    // @source:74
    EXPECT_EQ(PassiveStatic, 0b0000000100000000000000000000000);
    // @source:75
    EXPECT_EQ(MaySuspendCommit, 0b0000001000000000000000000000000);
    // @source:79-80
    EXPECT_EQ(ViewTransitionNamedStatic, SnapshotStatic | MaySuspendCommit);
    // @source:83
    EXPECT_EQ(ViewTransitionStatic, 0b0000010000000000000000000000000);
}

TEST(ReactFiberFlagsTest, DevFlags) {
    // @source:86
    EXPECT_EQ(PlacementDEV, 0b0000100000000000000000000000000);
    // @source:87
    EXPECT_EQ(MountLayoutDev, 0b0001000000000000000000000000000);
    // @source:88
    EXPECT_EQ(MountPassiveDev, 0b0010000000000000000000000000000);
}

TEST(ReactFiberFlagsTest, CommitPhaseMasks) {
    // @source:115-123
    EXPECT_EQ(MutationMask, 
        Placement | Update | ChildDeletion | ContentReset | Ref | Hydrating | Visibility | FormReset);
    // @source:124
    EXPECT_EQ(LayoutMask, Update | Callback | Ref | Visibility);
    // @source:127
    EXPECT_EQ(PassiveMask, Passive | Visibility | ChildDeletion);
    // @source:131
    EXPECT_EQ(PassiveTransitionMask, PassiveMask | Update | Placement);
}

TEST(ReactFiberFlagsTest, FlagOperations) {
    // 测试标志位操作
    Flags flags = NoFlags;
    
    // 添加标志
    flags |= Placement;
    EXPECT_TRUE((flags & Placement) != 0);
    
    // 添加多个标志
    flags |= Update | Ref;
    EXPECT_TRUE((flags & Update) != 0);
    EXPECT_TRUE((flags & Ref) != 0);
    
    // 移除标志
    flags &= ~Placement;
    EXPECT_FALSE((flags & Placement) != 0);
    EXPECT_TRUE((flags & Update) != 0);
    
    // 检查是否包含任一标志
    EXPECT_TRUE((flags & LifecycleEffectMask) != 0);
}

} // namespace react::reconciler::tests
