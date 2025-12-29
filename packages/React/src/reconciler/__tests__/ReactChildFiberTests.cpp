/**
 * ReactChildFiber Tests
 *
 * 子节点协调器测试
 * 测试 React Diff 算法的各种场景
 *
 * @source reactjs/packages/react-reconciler/src/ReactChildFiber.js
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <any>

#include "../ReactChildFiber.h"
#include "../ReactFiber.h"
#include "../ReactFiberFlags.h"
#include "../ReactWorkTags.h"
#include "../ReactFiberLane.h"
#include "../ReactTypeOfMode.h"
#include "../../react/ReactElement.h"

namespace react::reconciler::testing {


// 测试辅助工具


/**
 * 创建一个简单的父 Fiber
 */
FiberRef createReturnFiber(TypeOfMode mode = NoMode) {
    auto fiber = std::make_shared<Fiber>(HostComponent, mode);
    return fiber;
}

/**
 * 创建一个子 Fiber
 */
FiberRef createChildFiber(WorkTag tag, int index = 0, TypeOfMode mode = NoMode) {
    auto fiber = std::make_shared<Fiber>(tag, mode);
    fiber->index = index;
    return fiber;
}

/**
 * 创建文本 Fiber
 */
FiberRef createTextFiber(const std::string& text, int index = 0) {
    auto fiber = std::make_shared<Fiber>(HostText, NoMode);
    fiber->stateNode = text;
    fiber->index = index;
    return fiber;
}

/**
 * 链接兄弟 Fibers
 */
void linkSiblings(const std::vector<FiberRef>& fibers) {
    for (size_t i = 0; i + 1 < fibers.size(); ++i) {
        fibers[i]->sibling = fibers[i + 1];
    }
}


// ReactChildFiberReconciler 基础测试


class ChildFiberReconcilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
    }
    
    FiberRef returnFiber;
};

// -----------------------------------------------------------------------------
// 构造函数和工厂函数测试
// -----------------------------------------------------------------------------

TEST_F(ChildFiberReconcilerTest, CreateReconcileChildFibers_ShouldTrackSideEffects) {
    auto reconciler = createReconcileChildFibers();
    EXPECT_TRUE(reconciler.shouldTrackSideEffects());
}

TEST_F(ChildFiberReconcilerTest, CreateMountChildFibers_ShouldNotTrackSideEffects) {
    auto reconciler = createMountChildFibers();
    EXPECT_FALSE(reconciler.shouldTrackSideEffects());
}

TEST_F(ChildFiberReconcilerTest, GetReconcileChildFibers_ReturnsSameInstance) {
    auto& r1 = getReconcileChildFibers();
    auto& r2 = getReconcileChildFibers();
    EXPECT_EQ(&r1, &r2);
}

TEST_F(ChildFiberReconcilerTest, GetMountChildFibers_ReturnsSameInstance) {
    auto& m1 = getMountChildFibers();
    auto& m2 = getMountChildFibers();
    EXPECT_EQ(&m1, &m2);
}


// 删除操作测试


class ChildFiberDeletionTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
        mountReconciler = std::make_unique<ReactChildFiberReconciler>(false);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
    std::unique_ptr<ReactChildFiberReconciler> mountReconciler;
};

TEST_F(ChildFiberDeletionTest, DeleteChild_MarksForDeletion) {
    auto child = createChildFiber(HostComponent);
    
    reconciler->deleteChild(returnFiber, child);
    
    // 应该添加到 deletions 列表
    EXPECT_EQ(returnFiber->deletions.size(), 1);
    EXPECT_EQ(returnFiber->deletions[0], child);
    // 应该设置 ChildDeletion 标志
    EXPECT_TRUE((returnFiber->flags & ChildDeletion) != 0);
    EXPECT_TRUE((child->flags & ChildDeletion) != 0);
}

TEST_F(ChildFiberDeletionTest, DeleteChild_NoopWhenNotTrackingSideEffects) {
    auto child = createChildFiber(HostComponent);
    
    mountReconciler->deleteChild(returnFiber, child);
    
    // 不应该添加到 deletions
    EXPECT_TRUE(returnFiber->deletions.empty());
}

TEST_F(ChildFiberDeletionTest, DeleteRemainingChildren_DeletesAllSiblings) {
    auto child1 = createChildFiber(HostComponent, 0);
    auto child2 = createChildFiber(HostComponent, 1);
    auto child3 = createChildFiber(HostComponent, 2);
    linkSiblings({child1, child2, child3});
    
    reconciler->deleteRemainingChildren(returnFiber, child1);
    
    EXPECT_EQ(returnFiber->deletions.size(), 3);
}

TEST_F(ChildFiberDeletionTest, DeleteRemainingChildren_HandleNullChild) {
    reconciler->deleteRemainingChildren(returnFiber, nullptr);
    
    EXPECT_TRUE(returnFiber->deletions.empty());
}


// 单个节点协调测试


class ChildFiberSingleNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberSingleNodeTest, ReconcileSingleTextNode_CreatesNewTextFiber) {
    auto result = reconciler->reconcileSingleTextNode(
        returnFiber, nullptr, "Hello", DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
    EXPECT_EQ(result->getReturn(), returnFiber);
}

TEST_F(ChildFiberSingleNodeTest, ReconcileSingleTextNode_ReusesExistingTextFiber) {
    auto existing = createTextFiber("Old Text");
    existing->setReturn(returnFiber);
    
    auto result = reconciler->reconcileSingleTextNode(
        returnFiber, existing, "New Text", DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
    // 应该是复用的（检查 alternate）
    EXPECT_EQ(result->getAlternate(), existing);
}

TEST_F(ChildFiberSingleNodeTest, ReconcileSingleTextNode_DeletesNonTextFibers) {
    auto existing = createChildFiber(HostComponent);
    existing->setReturn(returnFiber);
    
    auto result = reconciler->reconcileSingleTextNode(
        returnFiber, existing, "Text", DefaultLane);
    
    // 应该创建新的文本节点
    EXPECT_EQ(result->tag, HostText);
    // 现有的非文本节点应该被删除
    EXPECT_EQ(returnFiber->deletions.size(), 1);
}

TEST_F(ChildFiberSingleNodeTest, ReconcileSingleElement_CreatesNewFiber) {
    react::ReactElement element;
    element.type = std::string("div");
    element.key = std::nullopt;
    
    auto result = reconciler->reconcileSingleElement(
        returnFiber, nullptr, element, DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getReturn(), returnFiber);
}

TEST_F(ChildFiberSingleNodeTest, ReconcileSinglePortal_CreatesNewPortalFiber) {
    react::ReactPortal portal;
    portal.containerInfo = std::string("container");
    portal.children = std::string("children");
    
    auto result = reconciler->reconcileSinglePortal(
        returnFiber, nullptr, portal, DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostPortal);
}


// 数组协调测试


class ChildFiberArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberArrayTest, ReconcileChildrenArray_EmptyArray_DeletesAllChildren) {
    auto child1 = createTextFiber("text1", 0);
    auto child2 = createTextFiber("text2", 1);
    linkSiblings({child1, child2});
    
    std::vector<std::any> emptyChildren;
    auto result = reconciler->reconcileChildrenArray(
        returnFiber, child1, emptyChildren, DefaultLane);
    
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(returnFiber->deletions.size(), 2);
}

TEST_F(ChildFiberArrayTest, ReconcileChildrenArray_NewArray_CreatesAllChildren) {
    std::vector<std::any> newChildren = {
        std::string("text1"),
        std::string("text2"),
        std::string("text3")
    };
    
    auto result = reconciler->reconcileChildrenArray(
        returnFiber, nullptr, newChildren, DefaultLane);
    
    ASSERT_NE(result, nullptr);
    
    // 检查链表结构
    int count = 0;
    auto current = result;
    while (current != nullptr) {
        count++;
        EXPECT_EQ(current->getReturn(), returnFiber);
        current = current->sibling;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ChildFiberArrayTest, ReconcileChildrenArray_SameLength_UpdatesInPlace) {
    // 旧子节点
    auto old1 = createTextFiber("old1", 0);
    auto old2 = createTextFiber("old2", 1);
    linkSiblings({old1, old2});
    old1->setReturn(returnFiber);
    old2->setReturn(returnFiber);
    
    // 新子节点
    std::vector<std::any> newChildren = {
        std::string("new1"),
        std::string("new2")
    };
    
    auto result = reconciler->reconcileChildrenArray(
        returnFiber, old1, newChildren, DefaultLane);
    
    ASSERT_NE(result, nullptr);
    
    // 检查结果长度
    int count = 0;
    auto current = result;
    while (current != nullptr) {
        count++;
        current = current->sibling;
    }
    EXPECT_EQ(count, 2);
}


// 位置放置测试


class ChildFiberPlacementTest : public ::testing::Test {
protected:
    void SetUp() override {
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
        mountReconciler = std::make_unique<ReactChildFiberReconciler>(false);
    }
    
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
    std::unique_ptr<ReactChildFiberReconciler> mountReconciler;
};

TEST_F(ChildFiberPlacementTest, PlaceChild_SetsIndex) {
    auto fiber = createChildFiber(HostComponent);
    
    reconciler->placeChild(fiber, 0, 5);
    
    EXPECT_EQ(fiber->index, 5);
}

TEST_F(ChildFiberPlacementTest, PlaceChild_MarksPlacementForNewFiber) {
    auto fiber = createChildFiber(HostComponent);
    
    reconciler->placeChild(fiber, 0, 0);
    
    EXPECT_TRUE((fiber->flags & Placement) != 0);
}

TEST_F(ChildFiberPlacementTest, PlaceChild_NoTrackSideEffects_MarksForked) {
    auto fiber = createChildFiber(HostComponent);
    
    mountReconciler->placeChild(fiber, 0, 0);
    
    EXPECT_TRUE((fiber->flags & Forked) != 0);
    EXPECT_FALSE((fiber->flags & Placement) != 0);
}

TEST_F(ChildFiberPlacementTest, PlaceChild_MoveDetection) {
    // 创建有 alternate 的 fiber
    auto oldFiber = createChildFiber(HostComponent, 3);
    auto newFiber = createChildFiber(HostComponent);
    newFiber->setAlternate(oldFiber);
    oldFiber->setAlternate(newFiber);
    
    // lastPlacedIndex > oldIndex 意味着需要移动
    int result = reconciler->placeChild(newFiber, 5, 2);
    
    // 因为 oldIndex (3) < lastPlacedIndex (5)，应该标记为 Placement
    EXPECT_TRUE((newFiber->flags & Placement) != 0);
    EXPECT_EQ(result, 5); // 返回 lastPlacedIndex
}

TEST_F(ChildFiberPlacementTest, PlaceChild_NoMoveNeeded) {
    // 创建有 alternate 的 fiber
    auto oldFiber = createChildFiber(HostComponent, 3);
    auto newFiber = createChildFiber(HostComponent);
    newFiber->setAlternate(oldFiber);
    oldFiber->setAlternate(newFiber);
    
    // lastPlacedIndex <= oldIndex，不需要移动
    int result = reconciler->placeChild(newFiber, 1, 2);
    
    EXPECT_FALSE((newFiber->flags & Placement) != 0);
    EXPECT_EQ(result, 3); // 返回 oldIndex
}


// Fiber 复用测试


class ChildFiberReuseTest : public ::testing::Test {
protected:
    void SetUp() override {
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberReuseTest, UseFiber_CreatesClone) {
    auto original = createChildFiber(HostComponent);
    original->lanes = SyncLane;
    original->childLanes = DefaultLane;
    
    auto clone = reconciler->useFiber(original, std::any{});
    
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->tag, original->tag);
    EXPECT_EQ(clone->mode, original->mode);
    EXPECT_EQ(clone->index, 0);
    EXPECT_EQ(clone->sibling, nullptr);
}

TEST_F(ChildFiberReuseTest, UseFiber_SetsAlternate) {
    auto original = createChildFiber(HostComponent);
    
    auto clone = reconciler->useFiber(original, std::any{});
    
    EXPECT_EQ(clone->getAlternate(), original);
    EXPECT_EQ(original->getAlternate(), clone);
}


// 映射剩余子节点测试


class ChildFiberMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberMapTest, MapRemainingChildren_MapsAllChildren) {
    auto child1 = createChildFiber(HostComponent, 0);
    auto child2 = createChildFiber(HostComponent, 1);
    auto child3 = createChildFiber(HostComponent, 2);
    linkSiblings({child1, child2, child3});
    
    auto map = reconciler->mapRemainingChildren(returnFiber, child1);
    
    EXPECT_EQ(map.size(), 3);
    EXPECT_EQ(map["0"], child1);
    EXPECT_EQ(map["1"], child2);
    EXPECT_EQ(map["2"], child3);
}

TEST_F(ChildFiberMapTest, MapRemainingChildren_EmptyForNull) {
    auto map = reconciler->mapRemainingChildren(returnFiber, nullptr);
    
    EXPECT_TRUE(map.empty());
}


// 文本节点更新测试


class ChildFiberTextUpdateTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberTextUpdateTest, UpdateTextNode_CreatesNew_WhenNoCurrent) {
    auto result = reconciler->updateTextNode(
        returnFiber, nullptr, "Hello", DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
}

TEST_F(ChildFiberTextUpdateTest, UpdateTextNode_CreatesNew_WhenCurrentIsNotText) {
    auto current = createChildFiber(HostComponent);
    
    auto result = reconciler->updateTextNode(
        returnFiber, current, "Hello", DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
    // 不应该有 alternate 指向非文本节点
}

TEST_F(ChildFiberTextUpdateTest, UpdateTextNode_ReusesExisting_WhenCurrentIsText) {
    auto current = createTextFiber("Old");
    
    auto result = reconciler->updateTextNode(
        returnFiber, current, "New", DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
    EXPECT_EQ(result->getAlternate(), current);
}


// 顶层协调 API 测试


class ChildFiberReconcileAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
    }
    
    FiberRef returnFiber;
};

TEST_F(ChildFiberReconcileAPITest, ReconcileChildFibers_Null_DeletesAll) {
    auto existing = createTextFiber("text");
    
    auto result = reconcileChildFibers(
        returnFiber, existing, std::any{}, DefaultLane);
    
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(returnFiber->deletions.size(), 1);
}

TEST_F(ChildFiberReconcileAPITest, ReconcileChildFibers_Text_CreatesTextNode) {
    auto result = reconcileChildFibers(
        returnFiber, nullptr, std::string("Hello"), DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
}

TEST_F(ChildFiberReconcileAPITest, MountChildFibers_DoesNotTrackDeletions) {
    auto existing = createTextFiber("text");
    
    auto result = mountChildFibers(
        returnFiber, existing, std::any{}, DefaultLane);
    
    // mount 不跟踪删除
    EXPECT_TRUE(returnFiber->deletions.empty());
}


// 辅助函数测试


class ChildFiberHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberHelperTest, GetElementKey_ReturnsKeyFromElement) {
    react::ReactElement element;
    element.key = "test-key";
    
    auto key = reconciler->getElementKey(std::any(element));
    
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key.value(), "test-key");
}

TEST_F(ChildFiberHelperTest, GetElementKey_ReturnsNulloptForNoKey) {
    react::ReactElement element;
    element.key = std::nullopt;
    
    auto key = reconciler->getElementKey(std::any(element));
    
    EXPECT_FALSE(key.has_value());
}

TEST_F(ChildFiberHelperTest, GetElementKey_ReturnsNulloptForNonElement) {
    auto key = reconciler->getElementKey(std::string("not an element"));
    
    EXPECT_FALSE(key.has_value());
}


// 边界情况测试


class ChildFiberEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberEdgeCaseTest, HandleNullReturnFiber_Gracefully) {
    // 测试空父节点不会崩溃
    EXPECT_NO_THROW({
        reconciler->deleteChild(nullptr, createChildFiber(HostComponent));
    });
}

TEST_F(ChildFiberEdgeCaseTest, HandleEmptyTextContent) {
    auto result = reconciler->reconcileSingleTextNode(
        returnFiber, nullptr, "", DefaultLane);
    
    // 空文本也应该创建节点
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
}

TEST_F(ChildFiberEdgeCaseTest, HandleLargeSiblingList) {
    // 创建较长的子节点列表
    std::vector<std::any> children;
    for (int i = 0; i < 100; i++) {
        children.push_back(std::string("text" + std::to_string(i)));
    }
    
    auto result = reconciler->reconcileChildrenArray(
        returnFiber, nullptr, children, DefaultLane);
    
    // 验证所有节点都被创建
    int count = 0;
    auto current = result;
    while (current != nullptr) {
        count++;
        current = current->sibling;
    }
    EXPECT_EQ(count, 100);
}


// createChild 测试


class ChildFiberCreateChildTest : public ::testing::Test {
protected:
    void SetUp() override {
        returnFiber = createReturnFiber();
        reconciler = std::make_unique<ReactChildFiberReconciler>(true);
    }
    
    FiberRef returnFiber;
    std::unique_ptr<ReactChildFiberReconciler> reconciler;
};

TEST_F(ChildFiberCreateChildTest, CreateChild_Text_CreatesTextFiber) {
    auto result = reconciler->createChild(returnFiber, std::string("Hello"), DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
    EXPECT_EQ(result->getReturn(), returnFiber);
}

TEST_F(ChildFiberCreateChildTest, CreateChild_Element_CreatesFiber) {
    react::ReactElement element;
    element.type = std::string("div");
    
    auto result = reconciler->createChild(returnFiber, std::any(element), DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getReturn(), returnFiber);
}

TEST_F(ChildFiberCreateChildTest, CreateChild_Portal_CreatesPortalFiber) {
    react::ReactPortal portal;
    portal.containerInfo = std::string("container");
    
    auto result = reconciler->createChild(returnFiber, std::any(portal), DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostPortal);
}

TEST_F(ChildFiberCreateChildTest, CreateChild_Null_ReturnsNull) {
    auto result = reconciler->createChild(returnFiber, std::any{}, DefaultLane);
    
    EXPECT_EQ(result, nullptr);
}

TEST_F(ChildFiberCreateChildTest, CreateChild_Number_CreatesTextFiber) {
    auto result = reconciler->createChild(returnFiber, 42, DefaultLane);
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->tag, HostText);
}


// placeSingleChild 工具函数测试


class PlaceSingleChildHelperTest : public ::testing::Test {
protected:
};

TEST_F(PlaceSingleChildHelperTest, PlaceSingleChild_MarksPlacement_WhenTrackingAndNoAlternate) {
    auto fiber = createChildFiber(HostComponent);
    EXPECT_TRUE(fiber->alternate.expired());
    
    placeSingleChild(fiber, true);
    
    EXPECT_TRUE((fiber->flags & Placement) != 0);
}

TEST_F(PlaceSingleChildHelperTest, PlaceSingleChild_NoPlacement_WhenNotTracking) {
    auto fiber = createChildFiber(HostComponent);
    
    placeSingleChild(fiber, false);
    
    EXPECT_FALSE((fiber->flags & Placement) != 0);
}

TEST_F(PlaceSingleChildHelperTest, PlaceSingleChild_NoPlacement_WhenHasAlternate) {
    auto fiber = createChildFiber(HostComponent);
    auto alternate = createChildFiber(HostComponent);
    fiber->setAlternate(alternate);
    
    placeSingleChild(fiber, true);
    
    // 有 alternate 时不标记 Placement
    EXPECT_FALSE((fiber->flags & Placement) != 0);
}


// cloneChildFibers 测试


class CloneChildFibersTest : public ::testing::Test {
protected:
    void SetUp() override {
        workInProgress = createReturnFiber();
    }
    
    FiberRef workInProgress;
};

TEST_F(CloneChildFibersTest, CloneChildFibers_NoopForNullChild) {
    workInProgress->child = nullptr;
    
    EXPECT_NO_THROW({
        cloneChildFibers(nullptr, workInProgress);
    });
    
    EXPECT_EQ(workInProgress->child, nullptr);
}

TEST_F(CloneChildFibersTest, CloneChildFibers_ClonesSingleChild) {
    auto child = createChildFiber(HostComponent);
    workInProgress->child = child;
    
    cloneChildFibers(nullptr, workInProgress);
    
    auto cloned = workInProgress->child;
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->tag, child->tag);
    EXPECT_EQ(cloned->getReturn(), workInProgress);
}

TEST_F(CloneChildFibersTest, CloneChildFibers_ClonesAllSiblings) {
    auto child1 = createChildFiber(HostComponent, 0);
    auto child2 = createChildFiber(HostComponent, 1);
    auto child3 = createChildFiber(HostComponent, 2);
    linkSiblings({child1, child2, child3});
    workInProgress->child = child1;
    
    cloneChildFibers(nullptr, workInProgress);
    
    // 验证所有子节点都被克隆
    int count = 0;
    auto current = workInProgress->child;
    while (current != nullptr) {
        EXPECT_EQ(current->getReturn(), workInProgress);
        count++;
        current = current->sibling;
    }
    EXPECT_EQ(count, 3);
}


// resetChildReconcilerOnUnwind 测试


TEST(ResetChildReconcilerTest, ResetChildReconcilerOnUnwind_NoThrow) {
    // 简单测试不抛出异常
    EXPECT_NO_THROW({
        resetChildReconcilerOnUnwind();
    });
}

} // namespace react::reconciler::testing
