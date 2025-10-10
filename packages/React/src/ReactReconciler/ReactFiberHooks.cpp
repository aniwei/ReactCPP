#include "ReactReconciler/ReactFiberHooks.h"

#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactFiberHookTypes.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactReconciler/ReactFiberNewContext.h"
#include "ReactReconciler/ReactFiberRootScheduler.h"
#include "ReactReconciler/ReactTypeOfMode.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactSharedInternals.h"

#include "jsi/jsi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace react {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

namespace {

constexpr const char* kHookMemoValueProp = "value";
constexpr const char* kHookMemoDepsProp = "deps";
constexpr const char* kRefCurrentProp = "current";

std::unique_ptr<Value> cloneValue(Runtime& jsRuntime, const Value* source) {
  if (source == nullptr) {
    return nullptr;
  }
  return std::make_unique<Value>(jsRuntime, *source);
}

Value cloneValueOrUndefined(Runtime& jsRuntime, const std::unique_ptr<Value>& source) {
  if (!source) {
    return Value::undefined();
  }
  return Value(jsRuntime, *source);
}

bool areHookInputsEqual(Runtime& jsRuntime, const Value& nextDeps, const Value& prevDeps) {
  if (!nextDeps.isObject() || !prevDeps.isObject()) {
    return false;
  }

  Object nextObj = nextDeps.getObject(jsRuntime);
  Object prevObj = prevDeps.getObject(jsRuntime);
  if (!nextObj.isArray(jsRuntime) || !prevObj.isArray(jsRuntime)) {
    return false;
  }

  Array nextArray = nextObj.asArray(jsRuntime);
  Array prevArray = prevObj.asArray(jsRuntime);
  const size_t nextLength = nextArray.size(jsRuntime);
  if (prevArray.size(jsRuntime) != nextLength) {
    return false;
  }

  for (size_t index = 0; index < nextLength; ++index) {
    Value nextValue = nextArray.getValueAtIndex(jsRuntime, index);
    Value prevValue = prevArray.getValueAtIndex(jsRuntime, index);
  if (!Value::strictEquals(jsRuntime, nextValue, prevValue)) {
      return false;
    }
  }

  return true;
}

Hook& appendWorkInProgressHook(HookRuntimeState& state, Hook* hook) {
  if (state.firstWorkInProgressHook == nullptr) {
    state.firstWorkInProgressHook = hook;
    state.workInProgressHook = hook;
  } else {
    state.workInProgressHook->next = hook;
    state.workInProgressHook = hook;
  }
  return *hook;
}

Hook& mountWorkInProgressHook(Runtime& jsRuntime, HookRuntimeState& state) {
  auto* hook = new Hook();
  (void)jsRuntime;
  return appendWorkInProgressHook(state, hook);
}

Hook& updateWorkInProgressHook(Runtime& jsRuntime, HookRuntimeState& state) {
  Hook* currentHook = state.currentHook;
  if (currentHook == nullptr) {
    throw std::logic_error("Rendered more hooks than during the previous render.");
  }

  auto* hook = new Hook();
  hook->memoizedState = cloneValue(jsRuntime, currentHook->memoizedState.get());
  hook->baseState = cloneValue(jsRuntime, currentHook->baseState.get());
  hook->queue = currentHook->queue;
  hook->baseQueue = currentHook->baseQueue;
  hook->memoizedEffect = currentHook->memoizedEffect;

  state.currentHook = currentHook->next;
  state.lastCurrentHook = currentHook;

  return appendWorkInProgressHook(state, hook);
}

std::shared_ptr<HookQueue> ensureHookQueue(Hook& hook) {
  if (!hook.queue) {
    hook.queue = std::make_shared<HookQueue>();
  }
  return hook.queue;
}

FunctionComponentUpdateQueue& ensureFunctionComponentUpdateQueue(FiberNode& fiber) {
  auto* queue = static_cast<FunctionComponentUpdateQueue*>(fiber.updateQueue);
  if (queue == nullptr) {
    queue = new FunctionComponentUpdateQueue();
    fiber.updateQueue = queue;
  }
  return *queue;
}

Value resolveInitialHookState(Runtime& jsRuntime, const Value& initialState) {
  if (initialState.isObject()) {
    Object object = initialState.getObject(jsRuntime);
    if (object.isFunction(jsRuntime)) {
      Function initializer = object.asFunction(jsRuntime);
      return initializer.call(jsRuntime, nullptr, 0);
    }
  }
  return Value(jsRuntime, initialState);
}

Value applyReducer(Runtime& jsRuntime, HookQueue& queue, const Value& prevState, const Value& action) {
  if (queue.isReducer && queue.reducer) {
    Object reducerObject = queue.reducer->getObject(jsRuntime);
    if (!reducerObject.isFunction(jsRuntime)) {
      return Value(jsRuntime, prevState);
    }
    Function reducer = reducerObject.asFunction(jsRuntime);
    std::array<Value, 2> args{Value(jsRuntime, prevState), Value(jsRuntime, action)};
  const Value* rawArgs = args.data();
  return reducer.call(jsRuntime, rawArgs, args.size());
  }

  if (action.isObject()) {
    Object actionObject = action.getObject(jsRuntime);
    if (actionObject.isFunction(jsRuntime)) {
      Function actionFn = actionObject.asFunction(jsRuntime);
      std::array<Value, 1> args{Value(jsRuntime, prevState)};
    const Value* rawArgs = args.data();
    return actionFn.call(jsRuntime, rawArgs, args.size());
    }
  }

  return Value(jsRuntime, action);
}

HookUpdate* detachPendingUpdates(HookQueue& queue) {
  auto* pending = static_cast<HookUpdate*>(queue.pending);
  if (pending == nullptr) {
    return nullptr;
  }

  auto* first = static_cast<HookUpdate*>(pending->next);
  pending->next = nullptr;
  queue.pending = nullptr;
  return first;
}

void mergeQueueState(Runtime& jsRuntime, Hook& hook, HookQueue& queue) {
  HookUpdate* update = detachPendingUpdates(queue);
  if (update == nullptr) {
    return;
  }

  Value state = cloneValueOrUndefined(jsRuntime, hook.memoizedState);
  if (state.isUndefined()) {
    state = Value::undefined();
  }

  HookUpdate* currentUpdate = update;
  while (currentUpdate != nullptr) {
    state = applyReducer(jsRuntime, queue, state, currentUpdate->action);
    auto* nextUpdate = static_cast<HookUpdate*>(currentUpdate->next);
    delete currentUpdate;
    currentUpdate = nextUpdate;
  }

  hook.memoizedState = std::make_unique<Value>(jsRuntime, state);
  hook.baseState = std::make_unique<Value>(jsRuntime, state);
  queue.lastRenderedState = std::make_unique<Value>(jsRuntime, state);
}

Function createDispatchFunction(Runtime& jsRuntime, const std::shared_ptr<HookQueue>& queue) {
  std::weak_ptr<HookQueue> weakQueue = queue;

  auto dispatchHost = [weakQueue](Runtime& innerRuntime, const Value&, const Value* args, size_t count) -> Value {
    auto queuePtr = weakQueue.lock();
    if (!queuePtr) {
      return Value::undefined();
    }

    FiberNode* fiber = queuePtr->fiber;
    ReactRuntime* runtimePtr = queuePtr->runtime;
    if (fiber == nullptr || runtimePtr == nullptr) {
      return Value::undefined();
    }

    auto* update = new HookUpdate();
    const Lane lane = SyncLane;
    update->lane = lane;
    update->next = nullptr;
    if (count > 0) {
      update->action = Value(innerRuntime, args[0]);
    } else {
      update->action = Value::undefined();
    }

    FiberRoot* root = enqueueConcurrentHookUpdate(fiber, queuePtr.get(), update, lane);
    if (root != nullptr) {
      ensureRootIsScheduled(*runtimePtr, innerRuntime, *root);
    }

    return Value::undefined();
  };

  return Function::createFromHostFunction(
      jsRuntime,
      PropNameID::forAscii(jsRuntime, "dispatch"),
      1,
      dispatchHost);
}

Value makeStateHookReturn(Runtime& jsRuntime, Hook& hook, HookQueue& queue) {
  Array result(jsRuntime, 2);
  result.setValueAtIndex(jsRuntime, 0, cloneValueOrUndefined(jsRuntime, hook.memoizedState));
  if (!queue.dispatch) {
    Function dispatchFn = createDispatchFunction(jsRuntime, hook.queue);
    queue.dispatch = std::make_shared<Function>(std::move(dispatchFn));
  }
  result.setValueAtIndex(jsRuntime, 1, Value(jsRuntime, *queue.dispatch));
  return Value(jsRuntime, result);
}

Value mountState(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  HookRuntimeState& state = reactRuntime.hookState();
  Value initial = Value::undefined();
  if (count > 0) {
    initial = Value(jsRuntime, args[0]);
  }
  Value resolved = resolveInitialHookState(jsRuntime, initial);

  Hook& hook = mountWorkInProgressHook(jsRuntime, state);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, resolved);
  hook.baseState = std::make_unique<Value>(jsRuntime, resolved);

  std::shared_ptr<HookQueue> queue = ensureHookQueue(hook);
  queue->runtime = &reactRuntime;
  queue->fiber = state.currentlyRenderingFiber;
  queue->lastRenderedState = std::make_unique<Value>(jsRuntime, resolved);
  queue->isReducer = false;

  Function dispatchFn = createDispatchFunction(jsRuntime, queue);
  queue->dispatch = std::make_shared<Function>(std::move(dispatchFn));

  return makeStateHookReturn(jsRuntime, hook, *queue);
}

Value updateState(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  (void)args;
  (void)count;
  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);
  std::shared_ptr<HookQueue> queue = ensureHookQueue(hook);
  queue->runtime = &reactRuntime;
  queue->fiber = state.currentlyRenderingFiber;
  queue->isReducer = false;

  mergeQueueState(jsRuntime, hook, *queue);

  return makeStateHookReturn(jsRuntime, hook, *queue);
}

Value mountReducer(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useReducer requires a reducer function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();

  Value reducerValue(jsRuntime, args[0]);
  Value initialArg = Value::undefined();
  if (count > 1) {
    initialArg = Value(jsRuntime, args[1]);
  }
  Value initValue = Value::undefined();
  if (count > 2) {
    initValue = Value(jsRuntime, args[2]);
  }

  Value initialState(jsRuntime, initialArg);
  if (initValue.isObject()) {
    Object initObject = initValue.getObject(jsRuntime);
    if (initObject.isFunction(jsRuntime)) {
      Function initFn = initObject.asFunction(jsRuntime);
      std::array<Value, 1> initArgs{Value(jsRuntime, initialArg)};
  const Value* initRawArgs = initArgs.data();
  initialState = initFn.call(jsRuntime, initRawArgs, initArgs.size());
    }
  }

  Hook& hook = mountWorkInProgressHook(jsRuntime, state);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, initialState);
  hook.baseState = std::make_unique<Value>(jsRuntime, initialState);

  std::shared_ptr<HookQueue> queue = ensureHookQueue(hook);
  queue->runtime = &reactRuntime;
  queue->fiber = state.currentlyRenderingFiber;
  queue->isReducer = true;
  queue->reducer = std::make_unique<Value>(jsRuntime, reducerValue);
  queue->lastRenderedState = std::make_unique<Value>(jsRuntime, initialState);

  Function dispatchFn = createDispatchFunction(jsRuntime, queue);
  queue->dispatch = std::make_shared<Function>(std::move(dispatchFn));

  return makeStateHookReturn(jsRuntime, hook, *queue);
}

Value updateReducer(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useReducer requires a reducer function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();

  Value reducerValue(jsRuntime, args[0]);
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);
  std::shared_ptr<HookQueue> queue = ensureHookQueue(hook);
  queue->runtime = &reactRuntime;
  queue->fiber = state.currentlyRenderingFiber;
  queue->isReducer = true;
  queue->reducer = std::make_unique<Value>(jsRuntime, reducerValue);

  mergeQueueState(jsRuntime, hook, *queue);

  return makeStateHookReturn(jsRuntime, hook, *queue);
}

Value mountRef(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = mountWorkInProgressHook(jsRuntime, state);

  Value initialValue = Value::undefined();
  if (count > 0) {
    initialValue = Value(jsRuntime, args[0]);
  }

  Object refObject(jsRuntime);
  refObject.setProperty(jsRuntime, kRefCurrentProp, initialValue);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, refObject);

  return Value(jsRuntime, refObject);
}

Value updateRef(ReactRuntime& reactRuntime, Runtime& jsRuntime) {
  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);
  if (!hook.memoizedState) {
    Object refObject(jsRuntime);
    refObject.setProperty(jsRuntime, kRefCurrentProp, Value::undefined());
    hook.memoizedState = std::make_unique<Value>(jsRuntime, refObject);
  }
  return Value(jsRuntime, *hook.memoizedState);
}

Value mountMemo(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useMemo requires an initialization function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = mountWorkInProgressHook(jsRuntime, state);

  Value createValue(jsRuntime, args[0]);
  Object createObject = createValue.getObject(jsRuntime);
  if (!createObject.isFunction(jsRuntime)) {
    throw std::invalid_argument("useMemo requires a function as the first argument.");
  }
  Function createFn = createObject.asFunction(jsRuntime);

  Value depsValue = Value::undefined();
  if (count > 1) {
    depsValue = Value(jsRuntime, args[1]);
  }

  Value memoizedResult = createFn.call(jsRuntime, nullptr, 0);

  Object memoState(jsRuntime);
  memoState.setProperty(jsRuntime, kHookMemoValueProp, Value(jsRuntime, memoizedResult));
  memoState.setProperty(jsRuntime, kHookMemoDepsProp, depsValue);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, memoState);

  return memoizedResult;
}

Value updateMemo(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useMemo requires an initialization function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);

  Value createValue(jsRuntime, args[0]);
  Object createObject = createValue.getObject(jsRuntime);
  if (!createObject.isFunction(jsRuntime)) {
    throw std::invalid_argument("useMemo requires a function as the first argument.");
  }
  Function createFn = createObject.asFunction(jsRuntime);

  Value nextDeps = Value::undefined();
  if (count > 1) {
    nextDeps = Value(jsRuntime, args[1]);
  }

  Value prevValue = Value::undefined();
  Value prevDeps = Value::undefined();
  if (hook.memoizedState && hook.memoizedState->isObject()) {
    Object memoState = hook.memoizedState->getObject(jsRuntime);
    if (memoState.hasProperty(jsRuntime, kHookMemoValueProp)) {
      prevValue = memoState.getProperty(jsRuntime, kHookMemoValueProp);
    }
    if (memoState.hasProperty(jsRuntime, kHookMemoDepsProp)) {
      prevDeps = memoState.getProperty(jsRuntime, kHookMemoDepsProp);
    }
  }

  const bool hasDeps = !nextDeps.isUndefined() && !nextDeps.isNull();
  if (hasDeps && !prevDeps.isUndefined() && areHookInputsEqual(jsRuntime, nextDeps, prevDeps)) {
    return prevValue;
  }

  Value nextValue = createFn.call(jsRuntime, nullptr, 0);
  Object memoState(jsRuntime);
  memoState.setProperty(jsRuntime, kHookMemoValueProp, Value(jsRuntime, nextValue));
  memoState.setProperty(jsRuntime, kHookMemoDepsProp, nextDeps);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, memoState);

  return nextValue;
}

Value mountCallback(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useCallback requires a function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = mountWorkInProgressHook(jsRuntime, state);

  Value callbackValue(jsRuntime, args[0]);
  Object callbackObject = callbackValue.getObject(jsRuntime);
  if (!callbackObject.isFunction(jsRuntime)) {
    throw std::invalid_argument("useCallback requires a function.");
  }

  Value depsValue = Value::undefined();
  if (count > 1) {
    depsValue = Value(jsRuntime, args[1]);
  }

  Object memoState(jsRuntime);
  memoState.setProperty(jsRuntime, kHookMemoValueProp, callbackValue);
  memoState.setProperty(jsRuntime, kHookMemoDepsProp, depsValue);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, memoState);

  return callbackValue;
}

Value updateCallback(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useCallback requires a function.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);

  Value nextCallback(jsRuntime, args[0]);
  Object callbackObject = nextCallback.getObject(jsRuntime);
  if (!callbackObject.isFunction(jsRuntime)) {
    throw std::invalid_argument("useCallback requires a function.");
  }

  Value nextDeps = Value::undefined();
  if (count > 1) {
    nextDeps = Value(jsRuntime, args[1]);
  }

  Value prevCallback = Value::undefined();
  Value prevDeps = Value::undefined();
  if (hook.memoizedState && hook.memoizedState->isObject()) {
    Object memoState = hook.memoizedState->getObject(jsRuntime);
    if (memoState.hasProperty(jsRuntime, kHookMemoValueProp)) {
      prevCallback = memoState.getProperty(jsRuntime, kHookMemoValueProp);
    }
    if (memoState.hasProperty(jsRuntime, kHookMemoDepsProp)) {
      prevDeps = memoState.getProperty(jsRuntime, kHookMemoDepsProp);
    }
  }

  const bool hasDeps = !nextDeps.isUndefined() && !nextDeps.isNull();
  if (hasDeps && !prevDeps.isUndefined() && areHookInputsEqual(jsRuntime, nextDeps, prevDeps)) {
    return prevCallback;
  }

  Object memoState(jsRuntime);
  memoState.setProperty(jsRuntime, kHookMemoValueProp, nextCallback);
  memoState.setProperty(jsRuntime, kHookMemoDepsProp, nextDeps);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, memoState);

  return nextCallback;
}

Value mountContext(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useContext requires a context object.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  if (state.currentlyRenderingFiber == nullptr) {
    throw std::logic_error("useContext called outside of a component render.");
  }

  Hook& hook = mountWorkInProgressHook(jsRuntime, state);
  Value contextValue(jsRuntime, args[0]);
  Value result = readContext(jsRuntime, *state.currentlyRenderingFiber, contextValue);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, result);
  return result;
}

Value updateContext(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useContext requires a context object.");
  }

  HookRuntimeState& state = reactRuntime.hookState();
  if (state.currentlyRenderingFiber == nullptr) {
    throw std::logic_error("useContext called outside of a component render.");
  }

  Hook& hook = updateWorkInProgressHook(jsRuntime, state);
  Value contextValue(jsRuntime, args[0]);
  Value result = readContext(jsRuntime, *state.currentlyRenderingFiber, contextValue);
  hook.memoizedState = std::make_unique<Value>(jsRuntime, result);
  return result;
}

Value normalizeHookDeps(Runtime& jsRuntime, const Value& maybeDeps) {
  if (maybeDeps.isUndefined()) {
    return Value::null();
  }
  return Value(jsRuntime, maybeDeps);
}

Value createEffectInstance(Runtime& jsRuntime) {
  Object instance(jsRuntime);
  instance.setProperty(jsRuntime, "destroy", Value::undefined());
  return Value(jsRuntime, instance);
}

Effect* pushEffectImpl(Runtime& jsRuntime, FiberNode& fiber, HookFlags effectTag, const Value& createValue, const Value& depsValue, const Value& instValue) {
  auto& updateQueue = ensureFunctionComponentUpdateQueue(fiber);
  auto* effect = new Effect(jsRuntime, effectTag, createValue, depsValue, instValue);

  if (updateQueue.lastEffect == nullptr) {
    effect->next = effect;
    updateQueue.lastEffect = effect;
  } else {
    Effect* first = updateQueue.lastEffect->next;
    updateQueue.lastEffect->next = effect;
    effect->next = first;
    updateQueue.lastEffect = effect;
  }

  return effect;
}

Effect* pushSimpleEffect(Runtime& jsRuntime, FiberNode& fiber, HookFlags effectTag, const Value& createValue, const Value& depsValue, const Value& instValue) {
  return pushEffectImpl(jsRuntime, fiber, effectTag, createValue, depsValue, instValue);
}

void mountEffectImpl(ReactRuntime& reactRuntime, Runtime& jsRuntime, HookFlags hookTag, FiberFlags fiberFlags, const Value& createValue, const Value& depsValue) {
  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = mountWorkInProgressHook(jsRuntime, state);
  FiberNode* fiber = state.currentlyRenderingFiber;
  if (fiber == nullptr) {
    throw std::logic_error("mountEffectImpl called without a currently rendering fiber.");
  }

  Value normalizedDeps = normalizeHookDeps(jsRuntime, depsValue);
  Value inst = createEffectInstance(jsRuntime);

  fiber->flags = static_cast<FiberFlags>(fiber->flags | fiberFlags);

  Effect* effect = pushSimpleEffect(jsRuntime, *fiber, HookFlags::HasEffect | hookTag, createValue, normalizedDeps, inst);
  hook.memoizedEffect = effect;
}

void updateEffectImpl(ReactRuntime& reactRuntime, Runtime& jsRuntime, HookFlags hookTag, FiberFlags fiberFlags, const Value& createValue, const Value& depsValue) {
  HookRuntimeState& state = reactRuntime.hookState();
  Hook& hook = updateWorkInProgressHook(jsRuntime, state);
  FiberNode* fiber = state.currentlyRenderingFiber;
  if (fiber == nullptr) {
    throw std::logic_error("updateEffectImpl called without a currently rendering fiber.");
  }

  Hook* currentHook = state.lastCurrentHook;
  Effect* prevEffect = currentHook != nullptr ? currentHook->memoizedEffect : nullptr;

  Value normalizedDeps = normalizeHookDeps(jsRuntime, depsValue);
  Value inst = prevEffect != nullptr ? Value(jsRuntime, prevEffect->inst) : createEffectInstance(jsRuntime);

  bool shouldRunEffect = true;
  if (!normalizedDeps.isNull() && prevEffect != nullptr) {
    const Value& prevDeps = prevEffect->deps;
    if (!prevDeps.isUndefined() && areHookInputsEqual(jsRuntime, normalizedDeps, prevDeps)) {
      shouldRunEffect = false;
    }
  }

  HookFlags effectTag = hookTag;
  if (shouldRunEffect) {
    fiber->flags = static_cast<FiberFlags>(fiber->flags | fiberFlags);
    effectTag = HookFlags::HasEffect | hookTag;
  }

  Effect* effect = pushSimpleEffect(jsRuntime, *fiber, effectTag, createValue, normalizedDeps, inst);
  hook.memoizedEffect = effect;
}

Value mountEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  mountEffectImpl(reactRuntime, jsRuntime, HookFlags::Passive, static_cast<FiberFlags>(Passive | PassiveStatic), createValue, depsValue);
  return Value::undefined();
}

Value updateEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  updateEffectImpl(reactRuntime, jsRuntime, HookFlags::Passive, Passive, createValue, depsValue);
  return Value::undefined();
}

Value mountInsertionEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useInsertionEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  mountEffectImpl(reactRuntime, jsRuntime, HookFlags::Insertion, Update, createValue, depsValue);
  return Value::undefined();
}

Value updateInsertionEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useInsertionEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  updateEffectImpl(reactRuntime, jsRuntime, HookFlags::Insertion, Update, createValue, depsValue);
  return Value::undefined();
}

Value mountLayoutEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useLayoutEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  FiberFlags fiberFlags = static_cast<FiberFlags>(Update | LayoutStatic);
  if ((reactRuntime.hookState().currentlyRenderingFiber->mode & StrictEffectsMode) != NoMode) {
    fiberFlags = static_cast<FiberFlags>(fiberFlags | MountLayoutDev);
  }

  mountEffectImpl(reactRuntime, jsRuntime, HookFlags::Layout, fiberFlags, createValue, depsValue);
  return Value::undefined();
}

Value updateLayoutEffect(ReactRuntime& reactRuntime, Runtime& jsRuntime, const Value* args, size_t count) {
  if (count == 0) {
    throw std::invalid_argument("useLayoutEffect requires a create function.");
  }

  Value createValue(jsRuntime, args[0]);
  Value depsValue = count > 1 ? Value(jsRuntime, args[1]) : Value::undefined();

  updateEffectImpl(reactRuntime, jsRuntime, HookFlags::Layout, Update, createValue, depsValue);
  return Value::undefined();
}

Value unsupportedHook(Runtime&, const Value&, const Value*, size_t) {
  throw std::runtime_error("Requested hook is not yet supported in ReactCPP.");
}

Object createDispatcher(ReactRuntime& reactRuntime, Runtime& jsRuntime, bool isMount) {
  Object dispatcher(jsRuntime);

  auto set = [&jsRuntime, &dispatcher](const char* name, Function fn) {
    dispatcher.setProperty(jsRuntime, name, Value(jsRuntime, fn));
  };

  if (isMount) {
    set("useState", Function::createFromHostFunction(
                           jsRuntime,
                           PropNameID::forAscii(jsRuntime, "useState"),
                           1,
                           [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                             return mountState(reactRuntime, runtimeRef, args, count);
                           }));
    set("useReducer", Function::createFromHostFunction(
                             jsRuntime,
                             PropNameID::forAscii(jsRuntime, "useReducer"),
                             3,
                             [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                               return mountReducer(reactRuntime, runtimeRef, args, count);
                             }));
    set("useRef", Function::createFromHostFunction(
                           jsRuntime,
                           PropNameID::forAscii(jsRuntime, "useRef"),
                           1,
                           [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                             return mountRef(reactRuntime, runtimeRef, args, count);
                           }));
    set("useMemo", Function::createFromHostFunction(
                            jsRuntime,
                            PropNameID::forAscii(jsRuntime, "useMemo"),
                            2,
                            [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                              return mountMemo(reactRuntime, runtimeRef, args, count);
                            }));
    set("useCallback", Function::createFromHostFunction(
                                jsRuntime,
                                PropNameID::forAscii(jsRuntime, "useCallback"),
                                2,
                                [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                  return mountCallback(reactRuntime, runtimeRef, args, count);
                                }));
    set("useContext", Function::createFromHostFunction(
                               jsRuntime,
                               PropNameID::forAscii(jsRuntime, "useContext"),
                               1,
                               [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                 return mountContext(reactRuntime, runtimeRef, args, count);
                               }));
    set("useEffect", Function::createFromHostFunction(
                              jsRuntime,
                              PropNameID::forAscii(jsRuntime, "useEffect"),
                              2,
                              [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                return mountEffect(reactRuntime, runtimeRef, args, count);
                              }));
    set("useLayoutEffect", Function::createFromHostFunction(
                                   jsRuntime,
                                   PropNameID::forAscii(jsRuntime, "useLayoutEffect"),
                                   2,
                                   [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                     return mountLayoutEffect(reactRuntime, runtimeRef, args, count);
                                   }));
    set("useInsertionEffect", Function::createFromHostFunction(
                                      jsRuntime,
                                      PropNameID::forAscii(jsRuntime, "useInsertionEffect"),
                                      2,
                                      [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                        return mountInsertionEffect(reactRuntime, runtimeRef, args, count);
                                      }));
  } else {
    set("useState", Function::createFromHostFunction(
                           jsRuntime,
                           PropNameID::forAscii(jsRuntime, "useState"),
                           1,
                           [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                             return updateState(reactRuntime, runtimeRef, args, count);
                           }));
    set("useReducer", Function::createFromHostFunction(
                             jsRuntime,
                             PropNameID::forAscii(jsRuntime, "useReducer"),
                             3,
                             [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                               return updateReducer(reactRuntime, runtimeRef, args, count);
                             }));
    set("useRef", Function::createFromHostFunction(
                           jsRuntime,
                           PropNameID::forAscii(jsRuntime, "useRef"),
                           1,
                           [&reactRuntime](Runtime& runtimeRef, const Value&, const Value*, size_t) {
                             return updateRef(reactRuntime, runtimeRef);
                           }));
    set("useMemo", Function::createFromHostFunction(
                            jsRuntime,
                            PropNameID::forAscii(jsRuntime, "useMemo"),
                            2,
                            [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                              return updateMemo(reactRuntime, runtimeRef, args, count);
                            }));
    set("useCallback", Function::createFromHostFunction(
                                jsRuntime,
                                PropNameID::forAscii(jsRuntime, "useCallback"),
                                2,
                                [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                  return updateCallback(reactRuntime, runtimeRef, args, count);
                                }));
    set("useContext", Function::createFromHostFunction(
                               jsRuntime,
                               PropNameID::forAscii(jsRuntime, "useContext"),
                               1,
                               [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                 return updateContext(reactRuntime, runtimeRef, args, count);
                               }));
    set("useEffect", Function::createFromHostFunction(
                              jsRuntime,
                              PropNameID::forAscii(jsRuntime, "useEffect"),
                              2,
                              [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                return updateEffect(reactRuntime, runtimeRef, args, count);
                              }));
    set("useLayoutEffect", Function::createFromHostFunction(
                                   jsRuntime,
                                   PropNameID::forAscii(jsRuntime, "useLayoutEffect"),
                                   2,
                                   [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                     return updateLayoutEffect(reactRuntime, runtimeRef, args, count);
                                   }));
    set("useInsertionEffect", Function::createFromHostFunction(
                                      jsRuntime,
                                      PropNameID::forAscii(jsRuntime, "useInsertionEffect"),
                                      2,
                                      [&reactRuntime](Runtime& runtimeRef, const Value&, const Value* args, size_t count) {
                                        return updateInsertionEffect(reactRuntime, runtimeRef, args, count);
                                      }));
  }

  const char* unsupportedHooks[] = {
      "useImperativeHandle",
      "useDeferredValue",
      "useTransition",
      "useId",
      "useSyncExternalStore",
      "useMutableSource",
      "useDebugValue",
      "use",
  };

  for (const char* name : unsupportedHooks) {
    set(name, Function::createFromHostFunction(
                    jsRuntime,
                    PropNameID::forAscii(jsRuntime, name),
                    1,
                    unsupportedHook));
  }

  return dispatcher;
}

void installDispatcher(ReactRuntime& runtime, Runtime& jsRuntime, bool isMount) {
  HookRuntimeState& state = runtime.hookState();
  Object internals = getReactSharedInternals(jsRuntime);
  Value prior = getReactSharedInternalsProperty(jsRuntime, internals, ReactSharedInternalsKeys::kDispatcher);
  if (!prior.isUndefined() && !prior.isNull()) {
    state.previousDispatcher = std::make_unique<Value>(jsRuntime, prior);
  } else {
    state.previousDispatcher.reset();
  }

  Object dispatcher = createDispatcher(runtime, jsRuntime, isMount);
  setReactSharedInternalsProperty(
      jsRuntime,
      internals,
      ReactSharedInternalsKeys::kDispatcher,
      Value(jsRuntime, dispatcher));
}

void resetDispatcher(ReactRuntime& runtime, Runtime& jsRuntime) {
  HookRuntimeState& state = runtime.hookState();
  Object internals = getReactSharedInternals(jsRuntime);
  if (state.previousDispatcher) {
    setReactSharedInternalsProperty(
        jsRuntime,
        internals,
        ReactSharedInternalsKeys::kDispatcher,
        Value(jsRuntime, *state.previousDispatcher));
  } else {
    setReactSharedInternalsProperty(
        jsRuntime,
        internals,
        ReactSharedInternalsKeys::kDispatcher,
        Value::null());
  }
  state.previousDispatcher.reset();
}

void resetHookRenderState(HookRuntimeState& state) {
  state.currentlyRenderingFiber = nullptr;
  state.currentHook = nullptr;
  state.workInProgressHook = nullptr;
  state.firstWorkInProgressHook = nullptr;
  state.lastCurrentHook = nullptr;
  state.renderLanes = NoLanes;
}

} // namespace

Value renderWithHooks(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& workInProgress,
    FiberNode* current,
    Lanes renderLanes,
    const FunctionComponentRender& componentRender) {
  HookRuntimeState& state = runtime.hookState();
  state.currentlyRenderingFiber = &workInProgress;
  state.renderLanes = renderLanes;
  state.firstWorkInProgressHook = nullptr;
  state.workInProgressHook = nullptr;
  state.currentHook = current != nullptr ? static_cast<Hook*>(current->memoizedState) : nullptr;
  state.lastCurrentHook = nullptr;

  installDispatcher(runtime, jsRuntime, current == nullptr);

  Value children;
  try {
    children = componentRender();
  } catch (...) {
    resetDispatcher(runtime, jsRuntime);
    resetHookRenderState(state);
    throw;
  }

  workInProgress.memoizedState = state.firstWorkInProgressHook;

  resetDispatcher(runtime, jsRuntime);
  resetHookRenderState(state);

  return children;
}

void resetHooksAfterSubmit(ReactRuntime&, Runtime&) {
  // Placeholder for future hook reset logic (e.g., passive effect queues).
}

} // namespace react
