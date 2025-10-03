#pragma once

#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactFiberStack.h"

#include <optional>

namespace react {

class FiberNode;
class ReactRuntime;

struct HiddenContext {
  Lanes baseLanes{NoLanes};
};

using HiddenContextOptional = std::optional<HiddenContext>;

StackCursor<HiddenContextOptional>& currentTreeHiddenStackCursor();
StackCursor<Lanes>& prevEntangledRenderLanesCursor();

void pushHiddenContext(ReactRuntime& runtime, FiberNode& fiber, const HiddenContext& context);
void reuseHiddenContextOnStack(ReactRuntime& runtime, FiberNode& fiber);
void popHiddenContext(ReactRuntime& runtime, FiberNode& fiber);

bool isCurrentTreeHidden();

} // namespace react
