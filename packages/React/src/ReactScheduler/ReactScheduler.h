#pragma once

#include "ReactScheduler/Scheduler.h"
#include "ReactScheduler/SchedulerPriorities.h"
#include "ReactScheduler/SchedulerMinHeap.h"
#include <chrono>
#include <memory>
#include <vector>
#include <functional>

namespace react {

/**
 * Internal task representation for the scheduler
 * Implements HeapNode interface for use with SchedulerMinHeap
 */
struct SchedulerTask : public HeapNode {
  using Callback = std::function<std::function<void(bool)>()>;
  
  uint64_t id;
  Callback callback;
  SchedulerPriority priorityLevel;
  double startTime;
  double expirationTime;
  double sortIndex; // Inherited from HeapNode but explicitly typed
  bool isQueued{false};
  
  SchedulerTask(uint64_t taskId, Callback cb, SchedulerPriority priority, double start, double expiration)
    : id(taskId), callback(std::move(cb)), priorityLevel(priority), startTime(start), expirationTime(expiration) {
    this->HeapNode::id = taskId;
    sortIndex = expiration; // Default to expiration time for immediate tasks
    this->HeapNode::sortIndex = sortIndex;
  }
};

/**
 * Default React Scheduler Implementation
 * 
 * This implementation closely follows the JavaScript Scheduler behavior:
 * - Min-heap based task queue with priority sorting
 * - Separate timer queue for delayed tasks
 * - Time-slicing with configurable frame intervals
 * - Priority-based timeout calculation
 * - Message loop integration for yielding
 */
class ReactScheduler : public Scheduler {
private:
  // Task queues
  SchedulerMinHeap<SchedulerTask> taskQueue_;
  SchedulerMinHeap<SchedulerTask> timerQueue_;
  
  // Current state
  uint64_t nextTaskId_{1};
  SchedulerPriority currentPriorityLevel_{SchedulerPriority::NormalPriority};
  SchedulerTask* currentTask_{nullptr};
  
  // Scheduling state
  bool isHostCallbackScheduled_{false};
  bool isHostTimeoutScheduled_{false};
  bool isPerformingWork_{false};
  bool isMessageLoopRunning_{false};
  bool needsPaint_{false};
  
  // Time management
  double frameInterval_{5.0}; // 5ms default frame interval (200 FPS)
  double startTime_{-1.0};
  std::chrono::steady_clock::time_point baseTime_;
  
  // Task storage - we need to keep tasks alive
  std::vector<std::unique_ptr<SchedulerTask>> taskStorage_;
  
public:
  ReactScheduler();
  ~ReactScheduler() override = default;

  // Scheduler interface implementation
  TaskHandle scheduleTask(
    SchedulerPriority priority,
    Task task,
    const TaskOptions& options = {}) override;

  void cancelTask(TaskHandle handle) override;
  
  SchedulerPriority getCurrentPriorityLevel() const override;
  
  SchedulerPriority runWithPriority(
    SchedulerPriority priority,
    const std::function<void()>& fn) override;

  bool shouldYield() const override;
  
  double now() const override;

  // Additional Scheduler functionality
  void forceFrameRate(double fps);
  void requestPaint();
  bool flushWork(double initialTime);
  void advanceTimers(double currentTime);
  
  // Message loop integration
  void startMessageLoop();
  void stopMessageLoop();
  bool performWorkUntilDeadline();
  
private:
  // Internal helpers
  SchedulerTask* createTask(
    SchedulerPriority priority,
    Task task,
    double startTime,
    double expirationTime);
    
  void scheduleHostCallback();
  void cancelHostCallback();
  void scheduleHostTimeout(double delay);
  void cancelHostTimeout();
  void handleTimeout(double currentTime);
  bool workLoop(double initialTime);
  double priorityTimeout(SchedulerPriority priority) const;
  void removeTaskFromStorage(uint64_t taskId);
};

} // namespace react