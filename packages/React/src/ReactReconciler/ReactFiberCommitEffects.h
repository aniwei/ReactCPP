#pragma once

#include "ReactReconciler/ReactFiberHookTypes.h"

namespace facebook {
namespace jsi {
class Runtime;
class Value;
} // namespace jsi
} // namespace facebook

namespace react {

class ReactRuntime;
struct FiberNode;

void commitHookEffectListUnmount(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    HookFlags flags,
    FiberNode& finishedWork,
    FiberNode* nearestMountedAncestor);

void commitHookEffectListMount(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    HookFlags flags,
    FiberNode& finishedWork);

void commitHookEffects(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& root);

void commitPassiveUnmountOnFiber(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& fiber);

void commitPassiveMountOnFiber(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    FiberNode& fiber);

} // namespace react
