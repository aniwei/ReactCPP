#include "ReactCapturedValue.h"

namespace react::reconciler {

std::string getStackByFiber(FiberRef fiber) {
  std::string stack;
  FiberRef current = fiber;

  while (current) {
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

ErrorCapturedValueRef createCapturedValueFromError(
  std::exception_ptr error,
  const std::optional<std::string>& stack
) {
  auto captured = std::make_shared<ErrorCapturedValue>();
  captured->error = error;
  captured->stack = stack;

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

ErrorCapturedValueRef createCapturedValueWithDigest(
  std::exception_ptr error,
  const std::string& digest,
  const std::optional<std::string>& stack
) {
  auto captured = createCapturedValueFromError(error, stack);
  captured->digest = digest;
  return captured;
}

} // namespace react::reconciler
