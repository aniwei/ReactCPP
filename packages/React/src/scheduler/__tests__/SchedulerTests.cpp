/**
 * Scheduler 模块单元测试
 * 
 * @source reactjs/packages/scheduler/src/SchedulerMinHeap.js
 * @source reactjs/packages/scheduler/src/SchedulerPriorities.js
 * 
 * 验证 C++ 实现的功能与 JS 端完全一致
 */

#include <gtest/gtest.h>
#include "scheduler/SchedulerPriorities.h"
#include "scheduler/SchedulerMinHeap.h"
#include <random>
#include <algorithm>

namespace react::scheduler::tests {

// =============================================================================
// SchedulerPriorities 测试
// =============================================================================

TEST(SchedulerPrioritiesTest, PriorityValues) {
    // @source:10-15 验证优先级数值
    EXPECT_EQ(NoPriority, 0);
    EXPECT_EQ(ImmediatePriority, 1);
    EXPECT_EQ(UserBlockingPriority, 2);
    EXPECT_EQ(NormalPriority, 3);
    EXPECT_EQ(LowPriority, 4);
    EXPECT_EQ(IdlePriority, 5);
}

TEST(SchedulerPrioritiesTest, PriorityOrder) {
    // 验证优先级排序
    EXPECT_LT(ImmediatePriority, UserBlockingPriority);
    EXPECT_LT(UserBlockingPriority, NormalPriority);
    EXPECT_LT(NormalPriority, LowPriority);
    EXPECT_LT(LowPriority, IdlePriority);
}

TEST(SchedulerPrioritiesTest, GetPriorityName) {
    EXPECT_STREQ(getPriorityName(NoPriority), "NoPriority");
    EXPECT_STREQ(getPriorityName(ImmediatePriority), "ImmediatePriority");
    EXPECT_STREQ(getPriorityName(UserBlockingPriority), "UserBlockingPriority");
    EXPECT_STREQ(getPriorityName(NormalPriority), "NormalPriority");
    EXPECT_STREQ(getPriorityName(LowPriority), "LowPriority");
    EXPECT_STREQ(getPriorityName(IdlePriority), "IdlePriority");
    EXPECT_STREQ(getPriorityName(99), "Unknown");
}

// =============================================================================
// SchedulerMinHeap 测试
// =============================================================================

// 测试用任务结构
struct Task {
    int id;
    double sortIndex;
    
    bool operator==(const Task& other) const {
        return id == other.id && sortIndex == other.sortIndex;
    }
};

TEST(SchedulerMinHeapTest, EmptyHeap) {
    MinHeap<Task> heap;
    EXPECT_EQ(heap.size(), 0);
    EXPECT_FALSE(heap.peek().has_value());
    EXPECT_FALSE(heap.pop().has_value());
}

TEST(SchedulerMinHeapTest, SingleElement) {
    MinHeap<Task> heap;
    Task task{1, 10.0};
    
    heap.push(task);
    
    EXPECT_EQ(heap.size(), 1);
    EXPECT_EQ(heap.peek()->id, 1);
    EXPECT_EQ(heap.peek()->sortIndex, 10.0);
    
    auto popped = heap.pop();
    EXPECT_EQ(popped->id, 1);
    EXPECT_EQ(heap.size(), 0);
}

TEST(SchedulerMinHeapTest, MultipleElements_AscendingOrder) {
    MinHeap<Task> heap;
    
    // 按升序插入
    heap.push({1, 10.0});
    heap.push({2, 20.0});
    heap.push({3, 30.0});
    
    EXPECT_EQ(heap.size(), 3);
    
    // 验证按 sortIndex 升序弹出
    auto task1 = heap.pop();
    EXPECT_EQ(task1->id, 1);
    EXPECT_EQ(task1->sortIndex, 10.0);
    
    auto task2 = heap.pop();
    EXPECT_EQ(task2->id, 2);
    EXPECT_EQ(task2->sortIndex, 20.0);
    
    auto task3 = heap.pop();
    EXPECT_EQ(task3->id, 3);
    EXPECT_EQ(task3->sortIndex, 30.0);
}

TEST(SchedulerMinHeapTest, MultipleElements_DescendingOrder) {
    MinHeap<Task> heap;
    
    // 按降序插入
    heap.push({3, 30.0});
    heap.push({2, 20.0});
    heap.push({1, 10.0});
    
    // 仍然按 sortIndex 升序弹出
    EXPECT_EQ(heap.pop()->sortIndex, 10.0);
    EXPECT_EQ(heap.pop()->sortIndex, 20.0);
    EXPECT_EQ(heap.pop()->sortIndex, 30.0);
}

TEST(SchedulerMinHeapTest, MultipleElements_RandomOrder) {
    MinHeap<Task> heap;
    
    // 随机顺序插入
    heap.push({5, 50.0});
    heap.push({1, 10.0});
    heap.push({3, 30.0});
    heap.push({2, 20.0});
    heap.push({4, 40.0});
    
    // 验证按顺序弹出
    EXPECT_EQ(heap.pop()->sortIndex, 10.0);
    EXPECT_EQ(heap.pop()->sortIndex, 20.0);
    EXPECT_EQ(heap.pop()->sortIndex, 30.0);
    EXPECT_EQ(heap.pop()->sortIndex, 40.0);
    EXPECT_EQ(heap.pop()->sortIndex, 50.0);
}

TEST(SchedulerMinHeapTest, DuplicateSortIndex) {
    MinHeap<Task> heap;
    
    // 相同 sortIndex，按 id 排序
    heap.push({2, 10.0});
    heap.push({1, 10.0});
    heap.push({3, 10.0});
    
    // 应该按 id 升序弹出
    EXPECT_EQ(heap.pop()->id, 1);
    EXPECT_EQ(heap.pop()->id, 2);
    EXPECT_EQ(heap.pop()->id, 3);
}

TEST(SchedulerMinHeapTest, LargeHeap) {
    MinHeap<Task> heap;
    constexpr int N = 1000;
    
    // 插入 1000 个随机排序的任务
    std::vector<double> sortIndices;
    for (int i = 0; i < N; ++i) {
        sortIndices.push_back(static_cast<double>(i));
    }
    
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(sortIndices.begin(), sortIndices.end(), g);
    
    for (int i = 0; i < N; ++i) {
        heap.push({i, sortIndices[i]});
    }
    
    // 验证按顺序弹出
    double lastSortIndex = -1.0;
    for (int i = 0; i < N; ++i) {
        auto task = heap.pop();
        EXPECT_GT(task->sortIndex, lastSortIndex);
        lastSortIndex = task->sortIndex;
    }
}

// =============================================================================
// MinHeap 与 JS 行为一致性测试
// =============================================================================

TEST(SchedulerMinHeapTest, PeekDoesNotRemove) {
    // @source:17 peek 方法不应该移除元素
    MinHeap<Task> heap;
    heap.push({1, 10.0});
    
    auto peeked1 = heap.peek();
    auto peeked2 = heap.peek();
    
    EXPECT_EQ(peeked1->id, peeked2->id);
    EXPECT_EQ(heap.size(), 1);
}

TEST(SchedulerMinHeapTest, SiftUpBehavior) {
    // @source:34 测试 siftUp 行为 - 新元素插入后正确上浮
    MinHeap<Task> heap;
    
    heap.push({1, 30.0});
    heap.push({2, 20.0});
    heap.push({3, 10.0}); // 应该上浮到顶部
    
    EXPECT_EQ(heap.peek()->id, 3);
    EXPECT_EQ(heap.peek()->sortIndex, 10.0);
}

TEST(SchedulerMinHeapTest, SiftDownBehavior) {
    // @source:53 测试 siftDown 行为 - pop 后堆正确重组
    MinHeap<Task> heap;
    
    heap.push({1, 10.0});
    heap.push({2, 20.0});
    heap.push({3, 30.0});
    heap.push({4, 40.0});
    heap.push({5, 50.0});
    
    // 移除最小元素
    heap.pop();
    
    // 验证堆顶是下一个最小元素
    EXPECT_EQ(heap.peek()->sortIndex, 20.0);
    
    // 继续移除
    heap.pop();
    EXPECT_EQ(heap.peek()->sortIndex, 30.0);
}

TEST(SchedulerMinHeapTest, HeapPropertyAfterOperations) {
    MinHeap<Task> heap;
    
    // 混合操作
    heap.push({1, 50.0});
    heap.push({2, 10.0});
    heap.pop();  // 移除 10.0
    heap.push({3, 30.0});
    heap.push({4, 20.0});
    heap.pop();  // 移除 20.0
    heap.push({5, 40.0});
    
    // 验证剩余元素顺序: 30, 40, 50
    EXPECT_EQ(heap.pop()->sortIndex, 30.0);
    EXPECT_EQ(heap.pop()->sortIndex, 40.0);
    EXPECT_EQ(heap.pop()->sortIndex, 50.0);
}

// =============================================================================
// 模拟 Scheduler 任务调度场景
// =============================================================================

struct SchedulerTask {
    int id;
    double sortIndex;  // expirationTime
    PriorityLevel priorityLevel;
    bool isQueued = false;
};

TEST(SchedulerMinHeapTest, SchedulerTaskScenario) {
    MinHeap<SchedulerTask> taskQueue;
    
    // 模拟不同优先级的任务
    SchedulerTask immediateTask{1, 100.0, ImmediatePriority, true};
    SchedulerTask userBlockingTask{2, 200.0, UserBlockingPriority, true};
    SchedulerTask normalTask{3, 300.0, NormalPriority, true};
    SchedulerTask lowTask{4, 400.0, LowPriority, true};
    SchedulerTask idleTask{5, 500.0, IdlePriority, true};
    
    // 按随机顺序插入
    taskQueue.push(normalTask);
    taskQueue.push(idleTask);
    taskQueue.push(immediateTask);
    taskQueue.push(lowTask);
    taskQueue.push(userBlockingTask);
    
    // 验证按 expirationTime (sortIndex) 顺序执行
    EXPECT_EQ(taskQueue.pop()->priorityLevel, ImmediatePriority);
    EXPECT_EQ(taskQueue.pop()->priorityLevel, UserBlockingPriority);
    EXPECT_EQ(taskQueue.pop()->priorityLevel, NormalPriority);
    EXPECT_EQ(taskQueue.pop()->priorityLevel, LowPriority);
    EXPECT_EQ(taskQueue.pop()->priorityLevel, IdlePriority);
}

} // namespace react::scheduler::tests
