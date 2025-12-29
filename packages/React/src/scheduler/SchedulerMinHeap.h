/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @source reactjs/packages/scheduler/src/SchedulerMinHeap.js
 * 
 * SchedulerMinHeap - 调度器最小堆实现
 * 
 * 本文件与 ReactJS 的 SchedulerMinHeap.js 完全 1:1 对应
 * 
 * 用于按照 sortIndex 和 id 排序的任务队列
 */

#pragma once

#include <vector>
#include <cstdint>
#include <optional>

namespace react::scheduler {


// Node 基类定义
// @source:10-14


/**
 * 堆节点必须实现的接口
 */
struct HeapNode {
    int32_t id = 0;
    double sortIndex = 0.0;
    
    HeapNode() = default;
    HeapNode(int32_t id_, double sortIndex_) : id(id_), sortIndex(sortIndex_) {}
    virtual ~HeapNode() = default;
};


// 比较函数
// @source:85-89


/**
 * 比较两个节点
 * 首先比较 sortIndex，如果相同则比较 id
 * 
 * @source:85-89
 */
template<typename T>
inline int compare(const T& a, const T& b) {
    double diff = a.sortIndex - b.sortIndex;
    if (diff != 0.0) {
        return diff < 0.0 ? -1 : 1;
    }
    return a.id - b.id;
}


// MinHeap 实现


/**
 * 最小堆模板类
 * 
 * @tparam T 节点类型，必须继承自 HeapNode 或提供 id 和 sortIndex 成员
 */
template<typename T>
class MinHeap {
public:
    using Heap = std::vector<T>;
    
    MinHeap() = default;
    
    /**
     * 将节点推入堆中
     * 
     * @source:16-20 - push
     */
    void push(const T& node) {
        size_t index = heap_.size();
        heap_.push_back(node);
        siftUp(index);
    }
    
    void push(T&& node) {
        size_t index = heap_.size();
        heap_.push_back(std::move(node));
        siftUp(index);
    }
    
    /**
     * 查看堆顶元素（不移除）
     * 
     * @source:22-24 - peek
     * @return 堆顶元素，如果堆为空则返回 nullopt
     */
    std::optional<T> peek() const {
        if (heap_.empty()) {
            return std::nullopt;
        }
        return heap_[0];
    }
    
    /**
     * 获取堆顶元素的指针（不移除）
     * 
     * @return 堆顶元素指针，如果堆为空则返回 nullptr
     */
    const T* peekPtr() const {
        if (heap_.empty()) {
            return nullptr;
        }
        return &heap_[0];
    }
    
    T* peekPtr() {
        if (heap_.empty()) {
            return nullptr;
        }
        return &heap_[0];
    }
    
    /**
     * 弹出堆顶元素
     * 
     * @source:26-38 - pop
     * @return 堆顶元素，如果堆为空则返回 nullopt
     */
    std::optional<T> pop() {
        if (heap_.empty()) {
            return std::nullopt;
        }
        
        T first = std::move(heap_[0]);
        
        if (heap_.size() > 1) {
            heap_[0] = std::move(heap_.back());
            heap_.pop_back();
            siftDown(0);
        } else {
            heap_.pop_back();
        }
        
        return first;
    }
    
    /**
     * 检查堆是否为空
     */
    bool empty() const {
        return heap_.empty();
    }
    
    /**
     * 获取堆的大小
     */
    size_t size() const {
        return heap_.size();
    }
    
    /**
     * 清空堆
     */
    void clear() {
        heap_.clear();
    }
    
private:
    Heap heap_;
    
    /**
     * 向上调整堆
     * 
     * @source:40-54 - siftUp
     */
    void siftUp(size_t index) {
        while (index > 0) {
            size_t parentIndex = (index - 1) >> 1;
            const T& parent = heap_[parentIndex];
            const T& node = heap_[index];
            
            if (compare(parent, node) > 0) {
                // 父节点更大，交换位置
                std::swap(heap_[parentIndex], heap_[index]);
                index = parentIndex;
            } else {
                // 父节点更小，退出
                return;
            }
        }
    }
    
    /**
     * 向下调整堆
     * 
     * @source:56-83 - siftDown
     */
    void siftDown(size_t index) {
        size_t length = heap_.size();
        size_t halfLength = length >> 1;
        
        while (index < halfLength) {
            size_t leftIndex = (index + 1) * 2 - 1;
            size_t rightIndex = leftIndex + 1;
            
            const T& left = heap_[leftIndex];
            const T& node = heap_[index];
            
            // 如果左子节点或右子节点更小，则与较小的那个交换
            if (compare(left, node) < 0) {
                if (rightIndex < length && compare(heap_[rightIndex], left) < 0) {
                    std::swap(heap_[index], heap_[rightIndex]);
                    index = rightIndex;
                } else {
                    std::swap(heap_[index], heap_[leftIndex]);
                    index = leftIndex;
                }
            } else if (rightIndex < length && compare(heap_[rightIndex], node) < 0) {
                std::swap(heap_[index], heap_[rightIndex]);
                index = rightIndex;
            } else {
                // 两个子节点都不更小，退出
                return;
            }
        }
    }
};

} // namespace react::scheduler
