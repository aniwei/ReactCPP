#pragma once

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberLane.h"

#include <functional>

namespace facebook {
namespace jsi {
class Runtime;
class Value;
} // namespace jsi
} // namespace facebook

namespace react {

class ReactRuntime;

using FunctionComponentRender = std::function<facebook::jsi::Value()>;

facebook::jsi::Value renderWithHooks(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& workInProgress,
    FiberNode* current,
    Lanes renderLanes,
    const FunctionComponentRender& componentRender);

void resetHooksAfterSubmit(ReactRuntime& runtime, facebook::jsi::Runtime& jsRuntime);

} // namespace react
