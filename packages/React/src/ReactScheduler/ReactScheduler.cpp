#include "ReactScheduler/ReactScheduler.h"
#include <algorithm>
#include <chrono>

namespace react {

ReactScheduler::ReactScheduler() 
  : baseTime_(std::chrono::steady_clock::now()) {
}

TaskHandle ReactScheduler::scheduleTask(
    SchedulerPriority priority,
    Task task,
    const TaskOptions& options) {
  
  const double currentTime = now();
  double startTime = currentTime;
  
  // Handle delay option
  if (options.delayMs > 0.0) {
    startTime = currentTime + options.delayMs;
  }
  
  // Calculate timeout based on priority
  double timeout = priorityTimeout(priority);
  if (options.timeoutMs > 0.0) {
    timeout = options.timeoutMs;
  }
  
  const double expirationTime = startTime + timeout;
  
  // Wrap the task to match expected callback signature
  auto wrappedCallback = [task = std::move(task)](bool didTimeout) -> std::function<void(bool)> {
    (void)didTimeout; // didTimeout is handled by scheduler logic
    task();
    return nullptr; // No continuation by default
  };
  
  SchedulerTask* newTask = createTask(priority, std::move(wrappedCallback), startTime, expirationTime);
  
  if (startTime > currentTime) {
    // This is a delayed task - add to timer queue
    newTask->sortIndex = startTime;
    newTask->HeapNode::sortIndex = startTime;
    timerQueue_.push(newTask);
    
    // If this is the earliest timer and no callback is scheduled, set up timeout
    if (taskQueue_.empty() && newTask == timerQueue_.peek()) {
      if (isHostTimeoutScheduled_) {
        cancelHostTimeout();
      }
      scheduleHostTimeout(startTime - currentTime);
    }
  } else {
    // Immediate task - add to task queue
    newTask->sortIndex = expirationTime;
    newTask->HeapNode::sortIndex = expirationTime;
    taskQueue_.push(newTask);
    newTask->isQueued = true;
    
    // Schedule host callback if needed
    if (!isHostCallbackScheduled_ && !isPerformingWork_) {
      scheduleHostCallback();
    }
  }
  
  return TaskHandle{newTask->id};
}

void ReactScheduler::cancelTask(TaskHandle handle) {
  if (!handle) {
    return;
  }
  
  // Find and null out the callback to mark as cancelled
  // We can't remove from heap efficiently, so we just mark as cancelled
  for (auto& taskPtr : taskStorage_) {
    if (taskPtr && taskPtr->id == handle.id) {
      taskPtr->callback = nullptr;
      taskPtr->isQueued = false;
      break;
    }
  }
}

SchedulerPriority ReactScheduler::getCurrentPriorityLevel() const {
  return currentPriorityLevel_;
}

SchedulerPriority ReactScheduler::runWithPriority(
    SchedulerPriority priority,
    const std::function<void()>& fn) {
  
  // Validate priority
  if (!isValidPriority(priority)) {
    priority = SchedulerPriority::NormalPriority;
  }
  
  const SchedulerPriority previousPriority = currentPriorityLevel_;
  currentPriorityLevel_ = priority;
  
  try {
    fn();
  } catch (...) {
    currentPriorityLevel_ = previousPriority;
    throw;
  }
  
  currentPriorityLevel_ = previousPriority;
  return previousPriority;
}

bool ReactScheduler::shouldYield() const {
  if (needsPaint_) {
    return true;
  }
  
  if (startTime_ < 0.0) {
    return false;
  }
  
  const double timeElapsed = now() - startTime_;
  return timeElapsed >= frameInterval_;
}

double ReactScheduler::now() const {
  const auto elapsed = std::chrono::steady_clock::now() - baseTime_;
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

void ReactScheduler::forceFrameRate(double fps) {
  if (fps < 0.0 || fps > 125.0) {
    // Invalid frame rate, ignore
    return;
  }
  
  if (fps > 0.0) {
    frameInterval_ = 1000.0 / fps;
  } else {
    frameInterval_ = 5.0; // Reset to default 5ms
  }
}

void ReactScheduler::requestPaint() {
  needsPaint_ = true;
}

bool ReactScheduler::flushWork(double initialTime) {
  // Reset host callback state
  isHostCallbackScheduled_ = false;
  
  if (isHostTimeoutScheduled_) {
    isHostTimeoutScheduled_ = false;
    cancelHostTimeout();
  }
  
  isPerformingWork_ = true;
  const SchedulerPriority previousPriority = currentPriorityLevel_;
  
  bool hasMoreWork = false;
  
  try {
    hasMoreWork = workLoop(initialTime);
  } catch (...) {
    currentTask_ = nullptr;
    currentPriorityLevel_ = previousPriority;
    isPerformingWork_ = false;
    throw;
  }
  
  currentTask_ = nullptr;
  currentPriorityLevel_ = previousPriority;
  isPerformingWork_ = false;
  
  return hasMoreWork;
}

void ReactScheduler::advanceTimers(double currentTime) {
  // Move expired timers to task queue
  SchedulerTask* timer = timerQueue_.peek();
  while (timer != nullptr) {
    if (timer->callback == nullptr) {
      // Timer was cancelled
      timerQueue_.pop();
    } else if (timer->startTime <= currentTime) {
      // Timer fired - move to task queue
      timerQueue_.pop();
      timer->sortIndex = timer->expirationTime;
      timer->HeapNode::sortIndex = timer->expirationTime;
      taskQueue_.push(timer);
      timer->isQueued = true;
    } else {
      // Remaining timers are not ready
      break;
    }
    timer = timerQueue_.peek();
  }
}

bool ReactScheduler::workLoop(double initialTime) {
  double currentTime = initialTime;
  advanceTimers(currentTime);
  currentTask_ = taskQueue_.peek();
  
  while (currentTask_ != nullptr) {
    // Check if we should yield due to time slice expiration
    if (currentTask_->expirationTime > currentTime && shouldYield()) {
      break;
    }
    
    if (currentTask_->callback) {
      // Execute the task
      currentTask_->callback = nullptr; // Clear callback before execution
      currentPriorityLevel_ = currentTask_->priorityLevel;
      
      const bool didUserCallbackTimeout = currentTask_->expirationTime <= currentTime;
      
      // Execute callback and check for continuation
      auto continuationCallback = currentTask_->callback 
        ? currentTask_->callback(didUserCallbackTimeout) 
        : nullptr;
        
      currentTime = now();
      
      if (continuationCallback) {
        // Task yielded with continuation
        currentTask_->callback = [continuationCallback](bool timeout) -> std::function<void(bool)> {
          continuationCallback(timeout);
          return nullptr;
        };
        advanceTimers(currentTime);
        return true; // Has more work
      } else {
        // Task completed
        if (currentTask_ == taskQueue_.peek()) {
          taskQueue_.pop();
        }
        advanceTimers(currentTime);
      }
    } else {
      // Task was cancelled
      taskQueue_.pop();
    }
    
    currentTask_ = taskQueue_.peek();
  }
  
  // Check if there's more work
  if (currentTask_ != nullptr) {
    return true;
  } else {
    // Schedule timeout for next timer if needed
    SchedulerTask* firstTimer = timerQueue_.peek();
    if (firstTimer != nullptr) {
      scheduleHostTimeout(firstTimer->startTime - currentTime);
    }
    return false;
  }
}

SchedulerTask* ReactScheduler::createTask(
    SchedulerPriority priority,
    Task task,
    double startTime,
    double expirationTime) {
  
  const uint64_t taskId = nextTaskId_++;
  
  // Convert Task to SchedulerTask::Callback
  auto callback = [task = std::move(task)](bool didTimeout) -> std::function<void(bool)> {
    (void)didTimeout;
    task();
    return nullptr; // No continuation
  };
  
  auto taskPtr = std::make_unique<SchedulerTask>(
    taskId, std::move(callback), priority, startTime, expirationTime);
  
  SchedulerTask* rawPtr = taskPtr.get();
  taskStorage_.push_back(std::move(taskPtr));
  
  return rawPtr;
}

double ReactScheduler::priorityTimeout(SchedulerPriority priority) const {
  return priorityToTimeout(priority);
}

void ReactScheduler::scheduleHostCallback() {
  isHostCallbackScheduled_ = true;
  // In a real implementation, this would integrate with the event loop
  // For now, we'll mark that a callback is needed
}

void ReactScheduler::cancelHostCallback() {
  isHostCallbackScheduled_ = false;
}

void ReactScheduler::scheduleHostTimeout(double delay) {
  isHostTimeoutScheduled_ = true;
  // In a real implementation, this would set up a timer
  // For now, we'll just mark that a timeout is scheduled
  (void)delay;
}

void ReactScheduler::cancelHostTimeout() {
  isHostTimeoutScheduled_ = false;
}

void ReactScheduler::handleTimeout(double currentTime) {
  isHostTimeoutScheduled_ = false;
  advanceTimers(currentTime);
  
  if (!isHostCallbackScheduled_) {
    if (taskQueue_.peek() != nullptr) {
      scheduleHostCallback();
    } else {
      SchedulerTask* firstTimer = timerQueue_.peek();
      if (firstTimer != nullptr) {
        scheduleHostTimeout(firstTimer->startTime - currentTime);
      }
    }
  }
}

void ReactScheduler::startMessageLoop() {
  isMessageLoopRunning_ = true;
}

void ReactScheduler::stopMessageLoop() {
  isMessageLoopRunning_ = false;
}

bool ReactScheduler::performWorkUntilDeadline() {
  if (!isMessageLoopRunning_) {
    return false;
  }
  
  needsPaint_ = false;
  const double currentTime = now();
  startTime_ = currentTime;
  
  bool hasMoreWork = false;
  try {
    hasMoreWork = flushWork(currentTime);
  } catch (...) {
    // If work throws, we still have more work to do
    hasMoreWork = true;
  }
  
  if (hasMoreWork) {
    // Schedule next work iteration
    return true;
  } else {
    isMessageLoopRunning_ = false;
    return false;
  }
}

void ReactScheduler::removeTaskFromStorage(uint64_t taskId) {
  auto it = std::remove_if(taskStorage_.begin(), taskStorage_.end(),
    [taskId](const std::unique_ptr<SchedulerTask>& task) {
      return task && task->id == taskId;
    });
  taskStorage_.erase(it, taskStorage_.end());
}

} // namespace react