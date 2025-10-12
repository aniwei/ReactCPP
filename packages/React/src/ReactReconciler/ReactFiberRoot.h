#pragma once

#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

#include <string>

namespace react {

class ReactRuntime;
struct FiberRoot;
struct SuspenseHydrationCallbacks;
struct TransitionTracingCallbacks;

using ContainerInfo = void*;
using ReactNodeList = facebook::jsi::Value;
using ReactFormState = facebook::jsi::Value;

FiberRoot* createFiberRoot(
    ReactRuntime& runtime,
    ContainerInfo containerInfo,
    RootTag tag,
    bool hydrate,
    const ReactNodeList& initialChildren,
    SuspenseHydrationCallbacks* hydrationCallbacks,
    bool isStrictMode,
    const std::string& identifierPrefix,
    ReactFormState* formState,
    void (*onUncaughtError)(void*, const void*),
    void (*onCaughtError)(void*, const void*),
    void (*onRecoverableError)(void*, const void*),
    void (*onDefaultTransitionIndicator)(),
    TransitionTracingCallbacks* transitionCallbacks);

} // namespace react
