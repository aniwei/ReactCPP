/**
 * React Fiber Thenable
 * 
 * Thenable (Promise-like) 对象的类型定义和处理函数
 * 用于 Suspense 和 use() hook 的实现
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberThenable.js
 */

#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include <any>
#include <variant>
#include <stdexcept>

namespace react::reconciler {


// Thenable 状态枚举
// @source shared/ReactTypes.js


enum class ThenableStatus {
  Pending,
  Fulfilled,
  Rejected
};


// Thenable 类型定义
// @source shared/ReactTypes.js


/**
 * Thenable 是一个类似 Promise 的对象
 * 
 * @tparam T 解析值的类型
 */
template<typename T>
struct Thenable {
  ThenableStatus status = ThenableStatus::Pending;
  std::optional<T> value = std::nullopt;
  std::optional<std::any> reason = std::nullopt;
  std::function<void(std::any, std::any)> then;
};

// void 特化 - 解决 std::optional<void> 不合法的问题
template<>
struct Thenable<void> {
  ThenableStatus status = ThenableStatus::Pending;
  bool resolved = false; // 用于表示 void thenable 是否完成
  std::optional<std::any> reason = std::nullopt;
  std::function<void(std::any, std::any)> then;
};

template<typename T>
using ThenableRef = std::shared_ptr<Thenable<T>>;


// PendingThenable
// @source shared/ReactTypes.js


template<typename T>
struct PendingThenable : Thenable<T> {
  PendingThenable() {
    this->status = ThenableStatus::Pending;
  }
};


// FulfilledThenable
// @source shared/ReactTypes.js


template<typename T>
struct FulfilledThenable : Thenable<T> {
  FulfilledThenable(T val) {
    this->status = ThenableStatus::Fulfilled;
    this->value = val;
  }
};


// RejectedThenable
// @source shared/ReactTypes.js


template<typename T>
struct RejectedThenable : Thenable<T> {
  RejectedThenable(std::any err) {
    this->status = ThenableStatus::Rejected;
    this->reason = err;
  }
};


// ThenableState 类型
// @source:30-35 ThenableState


struct ThenableState {
  std::vector<std::shared_ptr<Thenable<std::any>>> thenables;
  bool didWarnAboutUncachedPromise = false;
};

using ThenableStateRef = std::shared_ptr<ThenableState>;


// Suspense 异常
// @source:47-67


/**
 * SuspenseException - 用于触发 Suspense 的异常
 * @source:47-55
 */
class SuspenseException : public std::exception {
public:
  std::any thenable;
  
  SuspenseException() = default;
  
  explicit SuspenseException(std::any t) : thenable(std::move(t)) {}
  
  const char* what() const noexcept override {
    return "Suspense Exception: Component suspended";
  }
};

/**
 * SuspenseyCommitException - Suspense commit 阶段异常
 * @source:57-60
 */
class SuspenseyCommitException : public std::exception {
public:
  std::any wakeable;
  
  SuspenseyCommitException() = default;
  
  explicit SuspenseyCommitException(std::any w) : wakeable(std::move(w)) {}
  
  const char* what() const noexcept override {
    return "Suspendey Commit Exception: Commit suspended";
  }
};

/**
 * SuspenseActionException - useActionState 相关异常
 * @source:62-67
 */
class SuspenseActionException : public std::exception {
public:
  const char* what() const noexcept override {
    return "Suspense Action Exception: Action suspended";
  }
};


// noop Suspense commit thenable
// @source:69-79


/**
 * 空操作 thenable，用于触发 fallback
 */
std::shared_ptr<Thenable<void>> noopSuspenseyCommitThenable();


// 工具函数
// @source:81-100


/**
 * 创建 ThenableState
 * @source:81-92 createThenableState
 */
ThenableStateRef createThenableState();

/**
 * 检查 Thenable 是否已解析
 * @source:94-97 isThenableResolved
 */
template<typename T>
inline bool isThenableResolved(const Thenable<T>& thenable) {
  return thenable.status == ThenableStatus::Fulfilled ||
         thenable.status == ThenableStatus::Rejected;
}

/**
 * 跟踪使用的 Thenable
 * @source:99-175 trackUsedThenable
 */
template<typename T>
inline T trackUsedThenable(
  ThenableStateRef thenableState,
  ThenableRef<T> thenable,
  size_t index
) {
  auto& trackedThenables = thenableState->thenables;
  
  if (index >= trackedThenables.size()) {
    trackedThenables.push_back(std::static_pointer_cast<Thenable<std::any>>(thenable));
  }
  
  // 检查状态
  switch (thenable->status) {
    case ThenableStatus::Fulfilled:
      if (thenable->value.has_value()) {
        return thenable->value.value();
      }
      break;
      
    case ThenableStatus::Rejected:
      if (thenable->reason.has_value()) {
        throw thenable->reason.value();
      }
      break;
      
    case ThenableStatus::Pending:
    default:
      // 抛出 SuspenseException 来触发 Suspense
      throw SuspenseException();
  }
  
  // 不应该到达这里
  throw SuspenseException();
}

/**
 * 获取当前 Suspense 处理器抛出的 Thenable
 * @source:300+ getSuspendedThenable
 */
std::shared_ptr<Thenable<std::any>> getSuspendedThenable();


// Thenable 辅助函数


/**
 * 创建一个已 fulfilled 的 Thenable
 */
template<typename T>
inline ThenableRef<T> createFulfilledThenable(T value) {
  auto thenable = std::make_shared<FulfilledThenable<T>>(value);
  return thenable;
}

/**
 * 创建一个已 rejected 的 Thenable
 */
template<typename T>
inline ThenableRef<T> createRejectedThenable(std::any reason) {
  auto thenable = std::make_shared<RejectedThenable<T>>(reason);
  return thenable;
}

/**
 * 创建一个 pending 的 Thenable
 */
template<typename T>
inline ThenableRef<T> createPendingThenable() {
  return std::make_shared<PendingThenable<T>>();
}

/**
 * Resolve 一个 Thenable
 */
template<typename T>
inline void resolveThenable(ThenableRef<T> thenable, T value) {
  thenable->status = ThenableStatus::Fulfilled;
  thenable->value = value;
}

/**
 * Reject 一个 Thenable
 */
template<typename T>
inline void rejectThenable(ThenableRef<T> thenable, std::any reason) {
  thenable->status = ThenableStatus::Rejected;
  thenable->reason = reason;
}

} // namespace react::reconciler
