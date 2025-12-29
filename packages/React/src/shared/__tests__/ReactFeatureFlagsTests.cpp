/**
 * ReactFeatureFlags 单元测试
 * 
 * @source reactjs/packages/shared/ReactFeatureFlags.js
 * 
 * 验证 C++ 实现的 Feature Flags 与 JS 端完全一致
 */

#include <gtest/gtest.h>
#include "shared/ReactFeatureFlags.h"

namespace react::shared::tests {


// Feature Flag 常量值验证


TEST(ReactFeatureFlagsTest, KillswitchFlags) {
    // @source:24
    EXPECT_TRUE(enableHydrationLaneScheduling);
}

TEST(ReactFeatureFlagsTest, ModerateEffortFlags) {
    // @source:33
    EXPECT_FALSE(disableSchedulerTimeoutInWorkLoop);
}

TEST(ReactFeatureFlagsTest, SlatedForRemovalFlags) {
    // @source:48
    EXPECT_FALSE(enableSuspenseCallback);
    // @source:51
    EXPECT_FALSE(enableScopeAPI);
    // @source:54
    EXPECT_FALSE(enableCreateEventHandleAPI);
    // @source:57
    EXPECT_FALSE(enableLegacyFBSupport);
}

TEST(ReactFeatureFlagsTest, OngoingExperimentFlags) {
    // @source:66
    EXPECT_FALSE(enableYieldingBeforePassive);
    // @source:69
    EXPECT_FALSE(enableThrottledScheduling);
    // @source:87
    EXPECT_FALSE(enableSuspenseyImages);
    // @source:100
    EXPECT_FALSE(enableObjectFiber);
    // @source:102
    EXPECT_FALSE(enableTransitionTracing);
    // @source:105
    EXPECT_FALSE(enableLegacyHidden);
    // @source:108
    EXPECT_FALSE(enableSuspenseAvoidThisFallback);
    // @source:113
    EXPECT_FALSE(enableNoCloningMemoCache);
    // @source:122
    EXPECT_TRUE(alwaysThrottleRetries);
    // @source:124
    EXPECT_FALSE(passChildrenWhenCloningPersistedNodes);
    // @source:130
    EXPECT_FALSE(enablePersistedModeClonedFlag);
    // @source:132
    EXPECT_TRUE(enableEagerAlternateStateNodeCleanup);
    // @source:137
    EXPECT_FALSE(enableRetryLaneExpiration);
    // @source:146
    EXPECT_FALSE(enableInfiniteRenderLoopDetection);
}

TEST(ReactFeatureFlagsTest, ExpirationConstants) {
    // @source:138
    EXPECT_EQ(retryLaneExpirationMs, 5000);
    // @source:139
    EXPECT_EQ(syncLaneExpirationMs, 250);
    // @source:140
    EXPECT_EQ(transitionLaneExpirationMs, 5000);
}

TEST(ReactFeatureFlagsTest, NextMajorFlags) {
    // @source:161
    EXPECT_TRUE(renameElementSymbol);
    // @source:166
    EXPECT_FALSE(enableHiddenSubtreeInsertionEffectCleanup);
    // @source:171
    EXPECT_TRUE(disableLegacyContext);
    // @source:175
    EXPECT_TRUE(disableLegacyContextForFunctionComponents);
    // @source:178
    EXPECT_FALSE(enableMoveBefore);
    // @source:181
    EXPECT_TRUE(disableClientCache);
    // @source:184
    EXPECT_TRUE(enableReactTestRendererWarning);
    // @source:189
    EXPECT_TRUE(disableLegacyMode);
}

TEST(ReactFeatureFlagsTest, ReactDOMFlags) {
    // @source:211
    EXPECT_TRUE(disableCommentsAsDOMContainers);
    // @source:213
    EXPECT_FALSE(enableTrustedTypesIntegration);
    // @source:218
    EXPECT_FALSE(disableInputAttributeSyncing);
    // @source:221
    EXPECT_FALSE(disableTextareaChildren);
}

TEST(ReactFeatureFlagsTest, DebuggingFlags) {
    // @source:247
    EXPECT_TRUE(enableAsyncDebugInfo);
    // @source:252
    EXPECT_EQ(ownerStackLimit, 10000);
}


// 编译期配置验证


TEST(ReactFeatureFlagsTest, BuildModeConstants) {
    // 确保构建模式宏正确定义
    #if REACT_EXPERIMENTAL
        EXPECT_TRUE(__EXPERIMENTAL__);
    #else
        EXPECT_FALSE(__EXPERIMENTAL__);
    #endif
    
    #if REACT_PROFILE
        EXPECT_TRUE(__PROFILE__);
    #else
        EXPECT_FALSE(__PROFILE__);
    #endif
}

} // namespace react::shared::tests
