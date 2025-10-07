#pragma once

// Partial translation of reactjs/packages/react-reconciler/src/ReactChildFiber.js

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

namespace react {

FiberNode* cloneChildFibers(FiberNode* current, FiberNode& workInProgress);

void resetChildFibers(FiberNode& workInProgress, Lanes renderLanes);

FiberNode* mountChildFibers(
	facebook::jsi::Runtime& jsRuntime,
	FiberNode& workInProgress,
	const facebook::jsi::Value& nextChildren,
	Lanes renderLanes);

FiberNode* reconcileChildFibers(
	facebook::jsi::Runtime& jsRuntime,
	FiberNode* currentFirstChild,
	FiberNode& workInProgress,
	const facebook::jsi::Value& nextChildren,
	Lanes renderLanes);

void* getPortalContainerInfo(FiberNode& fiber);

} // namespace react
