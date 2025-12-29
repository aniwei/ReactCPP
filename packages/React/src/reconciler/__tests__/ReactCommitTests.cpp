/**
 * React Fiber Commit 阶段单元测试
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberCommitWork.js
 * @source reactjs/packages/react-reconciler/src/ReactFiberCommitEffects.js
 * @source reactjs/packages/react-reconciler/src/ReactEventPriorities.js
 * @source reactjs/packages/react-reconciler/src/ReactHookEffectTags.js
 */

#include <gtest/gtest.h>
#include "reconciler/ReactEventPriorities.h"
#include "reconciler/ReactHookEffectTags.h"
#include "reconciler/ReactFiberCommitEffects.h"
#include "reconciler/ReactFiberCommitWork.h"
#include "reconciler/ReactFiberLane.h"
#include "reconciler/ReactFiber.h"

namespace react::reconciler::tests {


// ReactEventPriorities 测试
// @source reactjs/packages/react-reconciler/src/ReactEventPriorities.js


TEST(ReactEventPrioritiesTest, EventPriorityConstants) {
  // @source:23-27
  EXPECT_EQ(NoEventPriority, NoLane);
  EXPECT_EQ(DiscreteEventPriority, SyncLane);
  EXPECT_EQ(ContinuousEventPriority, InputContinuousLane);
  EXPECT_EQ(DefaultEventPriority, DefaultLane);
  EXPECT_EQ(IdleEventPriority, IdleLane);
}

TEST(ReactEventPrioritiesTest, HigherEventPriority) {
  // @source:29-32 higherEventPriority
  // 数值越小优先级越高
  EXPECT_EQ(
    higherEventPriority(DiscreteEventPriority, ContinuousEventPriority),
    DiscreteEventPriority
  );
  
  EXPECT_EQ(
    higherEventPriority(DefaultEventPriority, DiscreteEventPriority),
    DiscreteEventPriority
  );
  
  EXPECT_EQ(
    higherEventPriority(IdleEventPriority, DefaultEventPriority),
    DefaultEventPriority
  );
  
  // NoEventPriority (0) 特殊处理
  EXPECT_EQ(
    higherEventPriority(NoEventPriority, DiscreteEventPriority),
    DiscreteEventPriority
  );
}

TEST(ReactEventPrioritiesTest, LowerEventPriority) {
  // @source:34-37 lowerEventPriority
  EXPECT_EQ(
    lowerEventPriority(DiscreteEventPriority, ContinuousEventPriority),
    ContinuousEventPriority
  );
  
  EXPECT_EQ(
    lowerEventPriority(DiscreteEventPriority, DefaultEventPriority),
    DefaultEventPriority
  );
  
  // NoEventPriority (0) 特殊处理
  EXPECT_EQ(
    lowerEventPriority(NoEventPriority, DiscreteEventPriority),
    NoEventPriority
  );
}

TEST(ReactEventPrioritiesTest, IsHigherEventPriority) {
  // @source:39-42 isHigherEventPriority
  EXPECT_TRUE(isHigherEventPriority(DiscreteEventPriority, ContinuousEventPriority));
  EXPECT_TRUE(isHigherEventPriority(DiscreteEventPriority, DefaultEventPriority));
  EXPECT_TRUE(isHigherEventPriority(ContinuousEventPriority, DefaultEventPriority));
  
  EXPECT_FALSE(isHigherEventPriority(DefaultEventPriority, DiscreteEventPriority));
  EXPECT_FALSE(isHigherEventPriority(DiscreteEventPriority, DiscreteEventPriority));
  EXPECT_FALSE(isHigherEventPriority(NoEventPriority, DiscreteEventPriority));
}

TEST(ReactEventPrioritiesTest, EventPriorityToLane) {
  // @source:44-46 eventPriorityToLane
  EXPECT_EQ(eventPriorityToLane(DiscreteEventPriority), SyncLane);
  EXPECT_EQ(eventPriorityToLane(ContinuousEventPriority), InputContinuousLane);
  EXPECT_EQ(eventPriorityToLane(DefaultEventPriority), DefaultLane);
  EXPECT_EQ(eventPriorityToLane(IdleEventPriority), IdleLane);
}

TEST(ReactEventPrioritiesTest, LanesToEventPriority) {
  // @source:48-59 lanesToEventPriority
  EXPECT_EQ(lanesToEventPriority(SyncLane), DiscreteEventPriority);
  EXPECT_EQ(lanesToEventPriority(InputContinuousLane), ContinuousEventPriority);
  EXPECT_EQ(lanesToEventPriority(DefaultLane), DefaultEventPriority);
  EXPECT_EQ(lanesToEventPriority(IdleLane), IdleEventPriority);
  
  // 多个 Lanes 时，取最高优先级
  EXPECT_EQ(
    lanesToEventPriority(SyncLane | DefaultLane),
    DiscreteEventPriority
  );
}


// ReactHookEffectTags 测试
// @source reactjs/packages/react-reconciler/src/ReactHookEffectTags.js


TEST(ReactHookEffectTagsTest, HookFlagsConstants) {
  // @source:12-20
  EXPECT_EQ(HookNoFlags, 0b0000);
  EXPECT_EQ(HookHasEffect, 0b0001);
  EXPECT_EQ(HookInsertion, 0b0010);
  EXPECT_EQ(HookLayout, 0b0100);
  EXPECT_EQ(HookPassive, 0b1000);
}

TEST(ReactHookEffectTagsTest, HasHookFlags) {
  HookFlags insertionEffect = HookInsertion | HookHasEffect;
  HookFlags layoutEffect = HookLayout | HookHasEffect;
  HookFlags passiveEffect = HookPassive | HookHasEffect;
  
  EXPECT_TRUE(hasHookFlags(insertionEffect, HookHasEffect));
  EXPECT_TRUE(hasHookFlags(insertionEffect, HookInsertion));
  EXPECT_FALSE(hasHookFlags(insertionEffect, HookLayout));
  
  EXPECT_TRUE(hasHookFlags(layoutEffect, HookLayout));
  EXPECT_FALSE(hasHookFlags(layoutEffect, HookPassive));
  
  EXPECT_TRUE(hasHookFlags(passiveEffect, HookPassive));
}

TEST(ReactHookEffectTagsTest, AddRemoveHookFlags) {
  HookFlags flags = HookNoFlags;
  
  flags = addHookFlags(flags, HookHasEffect);
  EXPECT_EQ(flags, HookHasEffect);
  
  flags = addHookFlags(flags, HookLayout);
  EXPECT_EQ(flags, HookHasEffect | HookLayout);
  
  flags = removeHookFlags(flags, HookHasEffect);
  EXPECT_EQ(flags, HookLayout);
  
  flags = removeHookFlags(flags, HookLayout);
  EXPECT_EQ(flags, HookNoFlags);
}

TEST(ReactHookEffectTagsTest, GetHookEffectName) {
  EXPECT_STREQ(getHookEffectName(HookLayout), "useLayoutEffect");
  EXPECT_STREQ(getHookEffectName(HookInsertion), "useInsertionEffect");
  EXPECT_STREQ(getHookEffectName(HookPassive), "useEffect");
  EXPECT_STREQ(getHookEffectName(HookNoFlags), "unknown");
}


// Effect 结构测试
// @source reactjs/packages/react-reconciler/src/ReactFiberHooks.js


TEST(EffectTest, EffectStructure) {
  auto effect = std::make_shared<Effect>();
  
  EXPECT_EQ(effect->tag, HookNoFlags);
  EXPECT_EQ(effect->create, nullptr);
  EXPECT_EQ(effect->inst.destroy, nullptr);
  EXPECT_TRUE(effect->deps.empty());
  EXPECT_EQ(effect->next, nullptr);
}

TEST(EffectTest, EffectListCircular) {
  // 创建循环链表
  auto effect1 = std::make_shared<Effect>();
  auto effect2 = std::make_shared<Effect>();
  auto effect3 = std::make_shared<Effect>();
  
  effect1->next = effect2;
  effect2->next = effect3;
  effect3->next = effect1;  // 循环
  
  // 遍历验证
  auto current = effect1;
  int count = 0;
  do {
    count++;
    current = current->next;
  } while (current && current != effect1);
  
  EXPECT_EQ(count, 3);
}


// CommitHookEffectListMount/Unmount 测试
// @source reactjs/packages/react-reconciler/src/ReactFiberCommitEffects.js


TEST(CommitEffectsTest, CommitHookEffectListMount) {
  // 创建 Fiber
  auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
  
  // 创建效果
  bool effectCreated = false;
  bool cleanupCalled = false;
  
  auto effect = std::make_shared<Effect>();
  effect->tag = HookPassive | HookHasEffect;
  effect->create = [&effectCreated, &cleanupCalled]() -> std::function<void()> {
    effectCreated = true;
    return [&cleanupCalled]() {
      cleanupCalled = true;
    };
  };
  effect->next = effect;  // 循环链表 (单个元素)
  
  // 创建 updateQueue
  FunctionComponentUpdateQueue updateQueue;
  updateQueue.lastEffect = effect;
  fiber->updateQueue = updateQueue;
  
  // 执行 mount
  commitHookEffectListMount(HookPassive | HookHasEffect, fiber);
  
  EXPECT_TRUE(effectCreated);
  EXPECT_FALSE(cleanupCalled);
  EXPECT_NE(effect->inst.destroy, nullptr);
  
  // 执行 unmount
  commitHookEffectListUnmount(HookPassive | HookHasEffect, fiber, nullptr);
  
  EXPECT_TRUE(cleanupCalled);
  EXPECT_EQ(effect->inst.destroy, nullptr);
}

TEST(CommitEffectsTest, CommitHookEffectListFlagsFiltering) {
  auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
  
  bool layoutEffectCreated = false;
  bool passiveEffectCreated = false;
  
  auto layoutEffect = std::make_shared<Effect>();
  layoutEffect->tag = HookLayout | HookHasEffect;
  layoutEffect->create = [&layoutEffectCreated]() -> std::function<void()> {
    layoutEffectCreated = true;
    return nullptr;
  };
  
  auto passiveEffect = std::make_shared<Effect>();
  passiveEffect->tag = HookPassive | HookHasEffect;
  passiveEffect->create = [&passiveEffectCreated]() -> std::function<void()> {
    passiveEffectCreated = true;
    return nullptr;
  };
  
  // 链接效果
  layoutEffect->next = passiveEffect;
  passiveEffect->next = layoutEffect;
  
  FunctionComponentUpdateQueue updateQueue;
  updateQueue.lastEffect = passiveEffect;
  fiber->updateQueue = updateQueue;
  
  // 只触发 Layout 效果
  commitHookEffectListMount(HookLayout | HookHasEffect, fiber);
  
  EXPECT_TRUE(layoutEffectCreated);
  EXPECT_FALSE(passiveEffectCreated);
  
  // 触发 Passive 效果
  commitHookEffectListMount(HookPassive | HookHasEffect, fiber);
  
  EXPECT_TRUE(passiveEffectCreated);
}


// CommitPhase 枚举测试
// @source reactjs/packages/react-reconciler/src/ReactFiberCommitWork.js


TEST(CommitWorkTest, CommitPhaseEnum) {
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::BeforeMutation), 0);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::Mutation), 1);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::LayoutPhase), 2);
  EXPECT_EQ(static_cast<uint8_t>(CommitPhase::PassivePhase), 3);
}


// EffectInstance 测试


TEST(EffectInstanceTest, DefaultValues) {
  EffectInstance inst;
  EXPECT_EQ(inst.destroy, nullptr);
}

TEST(EffectInstanceTest, DestroyFunction) {
  bool called = false;
  
  EffectInstance inst;
  inst.destroy = [&called]() {
    called = true;
  };
  
  EXPECT_NE(inst.destroy, nullptr);
  inst.destroy();
  EXPECT_TRUE(called);
}


// SafelyCallDestroy 测试


TEST(SafelyCallDestroyTest, NormalDestroy) {
  bool destroyCalled = false;
  
  auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
  
  safelyCallDestroy(nullptr, fiber, nullptr, [&destroyCalled]() {
    destroyCalled = true;
  });
  
  EXPECT_TRUE(destroyCalled);
}

TEST(SafelyCallDestroyTest, NullDestroy) {
  auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
  
  // 不应该抛出异常
  safelyCallDestroy(nullptr, fiber, nullptr, nullptr);
}

TEST(SafelyCallDestroyTest, DestroyWithException) {
  auto fiber = std::make_shared<Fiber>(FunctionComponent, NoMode);
  
  bool errorCaptured = false;
  CaptureCommitPhaseErrorFn captureCommitPhaseError = [&errorCaptured](
    FiberRef, FiberRef, std::exception_ptr
  ) {
    errorCaptured = true;
  };
  
  // 抛出异常的销毁函数
  safelyCallDestroy(&captureCommitPhaseError, fiber, nullptr, []() {
    throw std::runtime_error("test error");
  });
  
  EXPECT_TRUE(errorCaptured);
}


// FunctionComponentUpdateQueue 测试


TEST(FunctionComponentUpdateQueueTest, DefaultValues) {
  FunctionComponentUpdateQueue queue;
  EXPECT_EQ(queue.lastEffect, nullptr);
}

TEST(FunctionComponentUpdateQueueTest, WithEffects) {
  FunctionComponentUpdateQueue queue;
  
  auto effect = std::make_shared<Effect>();
  effect->tag = HookPassive | HookHasEffect;
  effect->next = effect;
  
  queue.lastEffect = effect;
  
  EXPECT_EQ(queue.lastEffect, effect);
  EXPECT_EQ(queue.lastEffect->next, effect);
}

} // namespace react::reconciler::tests
