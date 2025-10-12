#pragma once

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberLane.h"

namespace react {

class ReactRuntime;

FiberNode* unwindWork(
  ReactRuntime& runtime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes entangledRenderLanes);

void unwindInterruptedWork(
  ReactRuntime& runtime,
  FiberNode* current,
  FiberNode* workInProgress,
  Lanes renderLanes);

} // namespace react
