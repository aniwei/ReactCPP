#include "ReactReconciler/ReactFiberErrorLogger.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactWorkTags.h"
#include "shared/ReactGlobalError.h"

#include <string>
#include <sstream>

namespace react {

namespace {

void reportCallbackFailure(const char* callbackName, const std::exception& ex) {
  reportGlobalError(std::string(callbackName) + " threw: " + ex.what());
}

void reportCallbackFailure(const char* callbackName) {
  reportGlobalError(std::string(callbackName) + " threw an unknown exception");
}

UncaughtErrorInfo buildUncaughtInfo(const CapturedValue& errorInfo) {
  UncaughtErrorInfo info{};
  info.componentStack = errorInfo.stack;
  return info;
}

CaughtErrorInfo buildCaughtInfo(FiberNode& boundary, const CapturedValue& errorInfo) {
  CaughtErrorInfo info{};
  info.componentStack = errorInfo.stack;
  if (boundary.tag == WorkTag::ClassComponent) {
    info.errorBoundary = boundary.stateNode;
  }
  return info;
}

} // namespace

void defaultOnUncaughtError(void* /*error*/, const UncaughtErrorInfo& info) {
  if (!info.componentStack.empty()) {
    std::ostringstream message;
    message << "An error occurred in one of your React components.\n" << info.componentStack;
    reportGlobalError(message.str());
    return;
  }

  reportGlobalError("An error occurred in one of your React components.");
}

void defaultOnCaughtError(void* /*error*/, const CaughtErrorInfo& info) {
  if (!info.componentStack.empty()) {
    std::ostringstream message;
    message
      << "The above error was captured by a React error boundary.\n"
      << info.componentStack;
    reportGlobalError(message.str());
    return;
  }

  reportGlobalError("The above error was captured by a React error boundary.");
}

void logUncaughtError(FiberRoot& root, const CapturedValue& errorInfo) {
  const auto info = buildUncaughtInfo(errorInfo);
  if (root.onUncaughtError) {
    try {
      root.onUncaughtError(errorInfo.value, info);
      return;
    } catch (const std::exception& ex) {
      reportCallbackFailure("onUncaughtError", ex);
    } catch (...) {
      reportCallbackFailure("onUncaughtError");
    }
  }

  defaultOnUncaughtError(errorInfo.value, info);
}

void logCaughtError(FiberRoot& root, FiberNode& fiber, const CapturedValue& errorInfo) {
  const auto info = buildCaughtInfo(fiber, errorInfo);
  if (root.onCaughtError) {
    try {
      root.onCaughtError(errorInfo.value, info);
      return;
    } catch (const std::exception& ex) {
      reportCallbackFailure("onCaughtError", ex);
    } catch (...) {
      reportCallbackFailure("onCaughtError");
    }
  }

  defaultOnCaughtError(errorInfo.value, info);
}

} // namespace react
