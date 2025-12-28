/**
 * React Captured Value
 * 
 * 捕获的错误值，包含错误本身、来源 Fiber 和调用栈
 * 
 * @source reactjs/packages/react-reconciler/src/ReactCapturedValue.js
 */

#pragma once

#include <memory>
#include <string>
#include <any>
#include <optional>
#include <unordered_map>

#include "ReactFiber.h"

namespace react::reconciler {

// =============================================================================
// CapturedValue 类型
// @source:17-21 CapturedValue
// =============================================================================

/**
 * CapturedValue 表示捕获的错误或异常值
 * 
 * 当错误被抛出时，我们创建一个 CapturedValue 来保存：
 * - 错误值本身
 * - 抛出错误的 Fiber（用于组件堆栈）
 * - 堆栈跟踪字符串
 */
template<typename T>
struct CapturedValue {
  T value;
  FiberWeakRef source;
  std::optional<std::string> stack = std::nullopt;
  std::optional<std::string> digest = std::nullopt;
};

template<typename T>
using CapturedValueRef = std::shared_ptr<CapturedValue<T>>;

// 通用版本使用 std::any
using AnyCapturedValue = CapturedValue<std::any>;
using AnyCapturedValueRef = std::shared_ptr<AnyCapturedValue>;

// =============================================================================
// 错误类型的 CapturedValue
// =============================================================================

/**
 * ErrorCapturedValue 专门用于捕获错误/异常
 */
struct ErrorCapturedValue {
  std::exception_ptr error;
  FiberWeakRef source;
  std::optional<std::string> stack = std::nullopt;
  std::optional<std::string> digest = std::nullopt;
  std::optional<std::string> message = std::nullopt;
};

using ErrorCapturedValueRef = std::shared_ptr<ErrorCapturedValue>;

// =============================================================================
// 工具函数
// @source:23-45 createCapturedValueAtFiber
// =============================================================================

/**
 * 获取 Fiber 的组件堆栈
 * @source ReactFiberComponentStack.js getStackByFiberInDevAndProd
 */
inline std::string getStackByFiber(FiberRef fiber) {
  std::string stack;
  FiberRef current = fiber;
  
  while (current) {
    // 简化实现：只记录组件类型
    std::string componentName;
    switch (current->tag) {
      case FunctionComponent:
        componentName = "FunctionComponent";
        break;
      case ClassComponent:
        componentName = "ClassComponent";
        break;
      case HostRoot:
        componentName = "HostRoot";
        break;
      case HostComponent:
        componentName = "HostComponent";
        break;
      case SuspenseComponent:
        componentName = "SuspenseComponent";
        break;
      default:
        componentName = "UnknownComponent";
        break;
    }
    
    if (!stack.empty()) {
      stack += "\n";
    }
    stack += "    at " + componentName;
    
    current = current->return_.lock();
  }
  
  return stack;
}

/**
 * 在 Fiber 位置创建 CapturedValue
 * @source:23-45 createCapturedValueAtFiber
 */
template<typename T>
inline CapturedValueRef<T> createCapturedValueAtFiber(T value, FiberRef source) {
  auto captured = std::make_shared<CapturedValue<T>>();
  captured->value = std::move(value);
  captured->source = source;
  captured->stack = getStackByFiber(source);
  return captured;
}

/**
 * 从错误创建 CapturedValue
 * @source:47-58 createCapturedValueFromError
 */
inline ErrorCapturedValueRef createCapturedValueFromError(
  std::exception_ptr error,
  const std::optional<std::string>& stack = std::nullopt
) {
  auto captured = std::make_shared<ErrorCapturedValue>();
  captured->error = error;
  captured->stack = stack;
  
  // 尝试获取错误消息
  if (error) {
    try {
      std::rethrow_exception(error);
    } catch (const std::exception& e) {
      captured->message = e.what();
    } catch (...) {
      captured->message = "Unknown error";
    }
  }
  
  return captured;
}

/**
 * 创建带有 digest 的 CapturedValue
 */
inline ErrorCapturedValueRef createCapturedValueWithDigest(
  std::exception_ptr error,
  const std::string& digest,
  const std::optional<std::string>& stack = std::nullopt
) {
  auto captured = createCapturedValueFromError(error, stack);
  captured->digest = digest;
  return captured;
}

} // namespace react::reconciler
