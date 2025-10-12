#pragma once

#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

#include <string>

namespace react {

struct FiberNode;
struct FiberRoot;
struct SuspenseHydrationCallbacks;
struct TransitionTracingCallbacks;
struct ActivityState;
struct SuspenseState;
class ReactRuntime;

using Container = void*;
using PublicInstance = void*;
using ReactNodeList = facebook::jsi::Value;
using ReactFormState = facebook::jsi::Value;

FiberRoot* createContainer(
    Container containerInfo,
    RootTag tag,
    SuspenseHydrationCallbacks* hydrationCallbacks,
    bool isStrictMode,
    std::nullptr_t concurrentUpdatesByDefaultOverride,
    const std::string& identifierPrefix,
    void (*onUncaughtError)(void*, const void*),
    void (*onCaughtError)(void*, const void*),
    void (*onRecoverableError)(void*, const void*),
    void (*onDefaultTransitionIndicator)(),
    TransitionTracingCallbacks* transitionCallbacks);

FiberRoot* createHydrationContainer(
    const ReactNodeList& initialChildren,
    facebook::jsi::Function* callback,
    Container containerInfo,
    RootTag tag,
    SuspenseHydrationCallbacks* hydrationCallbacks,
    bool isStrictMode,
    std::nullptr_t concurrentUpdatesByDefaultOverride,
    const std::string& identifierPrefix,
    void (*onUncaughtError)(void*, const void*),
    void (*onCaughtError)(void*, const void*),
    void (*onRecoverableError)(void*, const void*),
    void (*onDefaultTransitionIndicator)(),
    TransitionTracingCallbacks* transitionCallbacks,
    ReactFormState* formState);

Lane updateContainer(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    const ReactNodeList& element,
    FiberRoot* container,
    facebook::jsi::Object* parentComponent,
    facebook::jsi::Function* callback);

Lane updateContainerSync(
    ReactRuntime& runtime,
    facebook::jsi::Runtime& jsRuntime,
    const ReactNodeList& element,
    FiberRoot* container,
    facebook::jsi::Object* parentComponent,
    facebook::jsi::Function* callback);

bool injectIntoDevTools();

PublicInstance findHostInstance(const facebook::jsi::Object& component);
PublicInstance findHostInstanceWithWarning(const facebook::jsi::Object& component, const std::string& methodName);
PublicInstance getPublicRootInstance(FiberRoot* container);

void attemptSynchronousHydration(FiberNode* fiber);
void attemptContinuousHydration(FiberNode* fiber);
void attemptHydrationAtCurrentPriority(FiberNode* fiber);

PublicInstance findHostInstanceWithNoPortals(FiberNode* fiber);

bool shouldError(FiberNode* fiber);
bool shouldSuspend(FiberNode* fiber);

} // namespace react
