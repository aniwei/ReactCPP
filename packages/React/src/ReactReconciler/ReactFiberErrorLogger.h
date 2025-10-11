#pragma once

#include "ReactReconciler/ReactCapturedValue.h"
#include "ReactReconciler/ReactFiberLane.h"

#include <functional>

namespace react {

void defaultOnUncaughtError(void* error, const UncaughtErrorInfo& info);
void defaultOnCaughtError(void* error, const CaughtErrorInfo& info);

void logUncaughtError(FiberRoot& root, const CapturedValue& errorInfo);
void logCaughtError(FiberRoot& root, FiberNode& fiber, const CapturedValue& errorInfo);

} // namespace react
