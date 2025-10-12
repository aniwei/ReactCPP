#include "ReactReconciler/ReactFiberRoot.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberCache.h"
#include "ReactReconciler/ReactFiberClassUpdateQueue.h"
#include "ReactReconciler/ReactFiberHostRootState.h"
#include "shared/ReactFeatureFlags.h"

#include "jsi/jsi.h"

#include <functional>
#include <memory>

namespace react {

namespace {

std::function<void(void*, const UncaughtErrorInfo&)> wrapUncaughtErrorCallback(
    void (*callback)(void*, const void*)) {
  if (callback == nullptr) {
    return {};
  }
  return [callback](void* error, const UncaughtErrorInfo& info) {
    callback(error, static_cast<const void*>(&info));
  };
}

std::function<void(void*, const CaughtErrorInfo&)> wrapCaughtErrorCallback(
    void (*callback)(void*, const void*)) {
  if (callback == nullptr) {
    return {};
  }
  return [callback](void* error, const CaughtErrorInfo& info) {
    callback(error, static_cast<const void*>(&info));
  };
}

std::function<void(void*, const UncaughtErrorInfo&)> wrapRecoverableErrorCallback(
    void (*callback)(void*, const void*)) {
  if (callback == nullptr) {
    return {};
  }
  return [callback](void* error, const UncaughtErrorInfo& info) {
    callback(error, static_cast<const void*>(&info));
  };
}

std::function<std::function<void()>()> wrapDefaultTransitionIndicator(
    void (*callback)()) {
  if (callback == nullptr) {
    return {};
  }
  return [callback]() -> std::function<void()> {
    callback();
    return {};
  };
}

} // namespace

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
  TransitionTracingCallbacks* transitionCallbacks) {
  const RootTag rootTag = disableLegacyMode ? RootTag::ConcurrentRoot : tag;

  auto* root = new FiberRoot();
  root->runtime = &runtime;
  root->jsRuntime = nullptr;
  root->tag = rootTag;
  root->containerInfo = containerInfo;
  root->timeoutHandle = noTimeout;
  root->callbackNode = {};
  root->callbackPriority = NoLane;
  root->pendingChildren = nullptr;
  root->pendingContext = nullptr;
  root->context = nullptr;
  root->identifierPrefix = identifierPrefix;
  root->formState = formState;

  root->onUncaughtError = wrapUncaughtErrorCallback(onUncaughtError);
  root->onCaughtError = wrapCaughtErrorCallback(onCaughtError);
  root->onRecoverableError = wrapRecoverableErrorCallback(onRecoverableError);

  if constexpr (enableDefaultTransitionIndicator) {
    root->onDefaultTransitionIndicator = wrapDefaultTransitionIndicator(onDefaultTransitionIndicator);
    root->pendingIndicator = nullptr;
  } else {
    (void)onDefaultTransitionIndicator;
  }

  if constexpr (enableSuspenseCallback) {
    root->hydrationCallbacks = hydrationCallbacks;
  } else {
    (void)hydrationCallbacks;
  }

  if constexpr (enableTransitionTracing) {
    root->transitionCallbacks = transitionCallbacks;
  } else {
    (void)transitionCallbacks;
  }

  root->hostRootState.isDehydrated = hydrate;

  auto* uninitializedFiber = createHostRootFiber(rootTag, isStrictMode);
  root->current = uninitializedFiber;
  uninitializedFiber->stateNode = root;

  void* initialCache = createCacheInstance();
  retainCache(initialCache);
  root->pooledCache = initialCache;
  root->pooledCacheLanes = NoLanes;
  retainCache(initialCache);

  auto initialState = std::make_unique<HostRootMemoizedState>();
  initialState->element = std::make_unique<facebook::jsi::Value>(initialChildren);
  initialState->isDehydrated = hydrate;
  initialState->cache = initialCache;
  uninitializedFiber->memoizedState = initialState.release();

  initializeUpdateQueue(*uninitializedFiber);

  return root;
}

} // namespace react
