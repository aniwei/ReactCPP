#pragma once

#include <vector>
#include <cstdint>

namespace react {

/**
 * Node interface for SchedulerMinHeap
 * Must provide id and sortIndex for comparison
 */
struct HeapNode {
  uint64_t id{0};
  double sortIndex{0.0};
  
  virtual ~HeapNode() = default;
};

/**
 * Min-heap implementation for Scheduler tasks
 * 
 * This implementation matches the JavaScript SchedulerMinHeap exactly:
 * - Primary comparison by sortIndex (typically expiration time)  
 * - Secondary comparison by id (insertion order for FIFO within same priority)
 * - Standard binary heap with array-based storage
 */
template<typename T>
class SchedulerMinHeap {
private:
  std::vector<T*> heap_;

  static int compare(const T* a, const T* b) {
    // Compare sort index first, then task id for stable ordering
    const double diff = a->sortIndex - b->sortIndex;
    if (diff != 0.0) {
      return diff < 0.0 ? -1 : 1;
    }
    // Secondary sort by ID for FIFO behavior within same priority
    return a->id < b->id ? -1 : (a->id > b->id ? 1 : 0);
  }

  void siftUp(T* node, size_t index) {
    while (index > 0) {
      const size_t parentIndex = (index - 1) >> 1;
      T* parent = heap_[parentIndex];
      
      if (compare(parent, node) > 0) {
        // Parent is larger, swap positions
        heap_[parentIndex] = node;
        heap_[index] = parent;
        index = parentIndex;
      } else {
        // Parent is smaller or equal, heap property satisfied
        return;
      }
    }
  }

  void siftDown(T* node, size_t index) {
    const size_t length = heap_.size();
    const size_t halfLength = length >> 1;
    
    while (index < halfLength) {
      const size_t leftIndex = (index + 1) * 2 - 1;
      T* left = heap_[leftIndex];
      const size_t rightIndex = leftIndex + 1;
      T* right = rightIndex < length ? heap_[rightIndex] : nullptr;

      // Find the smaller child to potentially swap with
      if (compare(left, node) < 0) {
        if (right != nullptr && compare(right, left) < 0) {
          // Right child is smallest
          heap_[index] = right;
          heap_[rightIndex] = node;
          index = rightIndex;
        } else {
          // Left child is smallest
          heap_[index] = left;
          heap_[leftIndex] = node;
          index = leftIndex;
        }
      } else if (right != nullptr && compare(right, node) < 0) {
        // Only right child is smaller than node
        heap_[index] = right;
        heap_[rightIndex] = node;
        index = rightIndex;
      } else {
        // Neither child is smaller, heap property satisfied
        return;
      }
    }
  }

public:
  SchedulerMinHeap() = default;
  
  // Non-copyable but movable
  SchedulerMinHeap(const SchedulerMinHeap&) = delete;
  SchedulerMinHeap& operator=(const SchedulerMinHeap&) = delete;
  SchedulerMinHeap(SchedulerMinHeap&&) = default;
  SchedulerMinHeap& operator=(SchedulerMinHeap&&) = default;

  /**
   * Push a new node onto the heap
   * Maintains min-heap property after insertion
   */
  void push(T* node) {
    if (node == nullptr) {
      return;
    }
    
    const size_t index = heap_.size();
    heap_.push_back(node);
    siftUp(node, index);
  }

  /**
   * Peek at the minimum element without removing it
   * Returns nullptr if heap is empty
   */
  T* peek() const {
    return heap_.empty() ? nullptr : heap_[0];
  }

  /**
   * Remove and return the minimum element
   * Returns nullptr if heap is empty
   */
  T* pop() {
    if (heap_.empty()) {
      return nullptr;
    }

    T* first = heap_[0];
    T* last = heap_.back();
    heap_.pop_back();

    if (!heap_.empty() && last != first) {
      heap_[0] = last;
      siftDown(last, 0);
    }

    return first;
  }

  /**
   * Check if the heap is empty
   */
  bool empty() const {
    return heap_.empty();
  }

  /**
   * Get the current size of the heap
   */
  size_t size() const {
    return heap_.size();
  }

  /**
   * Clear all elements from the heap
   */
  void clear() {
    heap_.clear();
  }

  /**
   * Get direct access to underlying vector (for debugging)
   * Use with caution - modifying this can break heap invariants
   */
  const std::vector<T*>& data() const {
    return heap_;
  }
};

} // namespace react