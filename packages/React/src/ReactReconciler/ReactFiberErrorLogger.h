#pragma once

#include "ReactReconciler/ReactCapturedValue.h"
#include "ReactReconciler/ReactFiberLane.h"

namespace react {

void logUncaughtError(FiberRoot& root, const CapturedValue& errorInfo);
void logCaughtError(FiberRoot& root, FiberNode& fiber, const CapturedValue& errorInfo);

} // namespace react
