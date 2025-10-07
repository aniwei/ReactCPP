#pragma once

#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"

namespace facebook {
namespace jsi {
class Runtime;
} // namespace jsi
} // namespace facebook

namespace react {

class FiberNode;
struct FiberRoot;
class ReactRuntime;

bool throwException(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberRoot& root,
    FiberNode* returnFiber,
    FiberNode& unitOfWork,
    void* thrownValue,
    Lanes renderLanes);

} // namespace react
