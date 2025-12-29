#include "ReactFiberHooksImpl.h"

namespace react::reconciler {

int ReactFiberHooksImpl::globalClientIdCounter_ = 0;

ReactFiberHooksImpl::ReactFiberHooksImpl() {
  initializeDispatchers();
}

HookRef ReactFiberHooksImpl::mountWorkInProgressHook() {
  auto hook = std::make_shared<Hook>();
  hook->memoizedState = std::any{};
  hook->baseState = std::any{};
  hook->baseQueue = nullptr;
  hook->queue = std::any{};
  hook->next = nullptr;

  if (context_.workInProgressHook == nullptr) {
    // 第一个 Hook
    context_.currentlyRenderingFiber->memoizedState = hook;
    context_.workInProgressHook = hook;
  } else {
    // 追加到链表末尾
    context_.workInProgressHook->next = hook;
    context_.workInProgressHook = hook;
  }

  return context_.workInProgressHook;
}

HookRef ReactFiberHooksImpl::updateWorkInProgressHook() {
  HookRef nextCurrentHook = nullptr;

  if (context_.currentHook == nullptr) {
    FiberRef current = context_.currentlyRenderingFiber->alternate.lock();
    if (current != nullptr) {
      nextCurrentHook = std::any_cast<HookRef>(current->memoizedState);
    }
  } else {
    nextCurrentHook = context_.currentHook->next;
  }

  HookRef nextWorkInProgressHook = nullptr;
  if (context_.workInProgressHook == nullptr) {
    if (context_.currentlyRenderingFiber->memoizedState.has_value()) {
      nextWorkInProgressHook = std::any_cast<HookRef>(
        context_.currentlyRenderingFiber->memoizedState
      );
    }
  } else {
    nextWorkInProgressHook = context_.workInProgressHook->next;
  }

  if (nextWorkInProgressHook != nullptr) {
    // 重用已存在的 hook
    context_.workInProgressHook = nextWorkInProgressHook;
    context_.currentHook = nextCurrentHook;
  } else {
    // 克隆 current hook
    if (nextCurrentHook == nullptr) {
      throw std::runtime_error("Rendered more hooks than during the previous render.");
    }

    context_.currentHook = nextCurrentHook;

    auto newHook = std::make_shared<Hook>();
    newHook->memoizedState = nextCurrentHook->memoizedState;
    newHook->baseState = nextCurrentHook->baseState;
    newHook->baseQueue = nextCurrentHook->baseQueue;
    newHook->queue = nextCurrentHook->queue;
    newHook->next = nullptr;

    if (context_.workInProgressHook == nullptr) {
      context_.currentlyRenderingFiber->memoizedState = newHook;
      context_.workInProgressHook = newHook;
    } else {
      context_.workInProgressHook->next = newHook;
      context_.workInProgressHook = newHook;
    }
  }

  return context_.workInProgressHook;
}

FunctionComponentUpdateQueueRef ReactFiberHooksImpl::createFunctionComponentUpdateQueue() {
  return std::make_shared<FunctionComponentUpdateQueue>();
}

std::pair<std::any, std::function<void(std::any)>> ReactFiberHooksImpl::mountState(std::any initialState) {
  auto hook = mountWorkInProgressHook();

  // 如果 initialState 是函数，执行它
  if (initialState.type() == typeid(std::function<std::any()>)) {
    auto fn = std::any_cast<std::function<std::any()>>(initialState);
    initialState = fn();
  }

  hook->memoizedState = initialState;
  hook->baseState = initialState;

  // 创建更新队列
  auto queue = std::make_shared<HookUpdateQueue<std::any, std::any>>();
  queue->pending = nullptr;
  queue->lanes = NoLanes;
  queue->lastRenderedReducer = [](std::any state, std::any action) -> std::any {
    if (action.type() == typeid(std::function<std::any(std::any)>)) {
      auto fn = std::any_cast<std::function<std::any(std::any)>>(action);
      return fn(state);
    }
    return action;
  };
  queue->lastRenderedState = initialState;

  hook->queue = queue;

  // 创建 dispatch 函数
  auto fiber = context_.currentlyRenderingFiber;
  std::weak_ptr<HookUpdateQueue<std::any, std::any>> weakQueue = queue;

  auto dispatch = [this, fiber, weakQueue](std::any action) {
    if (auto q = weakQueue.lock()) {
      dispatchSetState(fiber, std::static_pointer_cast<void>(q), action);
    }
  };

  queue->dispatch = dispatch;

  return {hook->memoizedState, dispatch};
}

std::pair<std::any, std::function<void(std::any)>> ReactFiberHooksImpl::mountReducer(
  std::function<std::any(std::any, std::any)> reducer,
  std::any initialArg,
  std::optional<std::function<std::any(std::any)>> init
) {
  auto hook = mountWorkInProgressHook();

  std::any initialState;
  if (init.has_value()) {
    initialState = init.value()(initialArg);
  } else {
    initialState = initialArg;
  }

  hook->memoizedState = initialState;
  hook->baseState = initialState;

  auto queue = std::make_shared<HookUpdateQueue<std::any, std::any>>();
  queue->pending = nullptr;
  queue->lanes = NoLanes;
  queue->lastRenderedReducer = reducer;
  queue->lastRenderedState = initialState;

  hook->queue = queue;

  auto fiber = context_.currentlyRenderingFiber;
  std::weak_ptr<HookUpdateQueue<std::any, std::any>> weakQueue = queue;

  auto dispatch = [this, fiber, weakQueue](std::any action) {
    if (auto q = weakQueue.lock()) {
      dispatchReducerAction(fiber, std::static_pointer_cast<void>(q), action);
    }
  };

  queue->dispatch = dispatch;

  return {hook->memoizedState, dispatch};
}

std::any ReactFiberHooksImpl::mountMemo(
  std::function<std::any()> create,
  std::optional<std::vector<std::any>> deps
) {
  auto hook = mountWorkInProgressHook();
  auto nextDeps = deps.value_or(std::vector<std::any>{});
  auto nextValue = create();

  // 存储 [value, deps]
  hook->memoizedState = std::make_pair(nextValue, nextDeps);

  return nextValue;
}

std::any ReactFiberHooksImpl::mountCallback(std::any callback, std::vector<std::any> deps) {
  auto hook = mountWorkInProgressHook();
  hook->memoizedState = std::make_pair(callback, deps);
  return callback;
}

std::shared_ptr<std::any> ReactFiberHooksImpl::mountRef(std::any initialValue) {
  auto hook = mountWorkInProgressHook();
  auto ref = std::make_shared<std::any>(initialValue);
  hook->memoizedState = ref;
  return ref;
}

std::pair<std::any, std::function<void(std::any)>> ReactFiberHooksImpl::updateState(std::any) {
  return updateReducer(
    [](std::any state, std::any action) -> std::any {
      if (action.type() == typeid(std::function<std::any(std::any)>)) {
        auto fn = std::any_cast<std::function<std::any(std::any)>>(action);
        return fn(state);
      }
      return action;
    },
    std::any{},
    std::nullopt
  );
}

std::pair<std::any, std::function<void(std::any)>> ReactFiberHooksImpl::updateReducer(
  std::function<std::any(std::any, std::any)> reducer,
  std::any,
  std::optional<std::function<std::any(std::any)>>
) {
  auto hook = updateWorkInProgressHook();

  if (!hook->queue.has_value()) {
    throw std::runtime_error(
      "Should have a queue. You are likely calling Hooks conditionally."
    );
  }

  auto queue = std::any_cast<std::shared_ptr<HookUpdateQueue<std::any, std::any>>>(
    hook->queue
  );

  queue->lastRenderedReducer = reducer;

  // 处理 pending 更新
  auto baseState = hook->baseState;
  auto pending = queue->pending;

  if (pending != nullptr) {
    // 处理更新队列
    auto first = pending->next;
    auto update = first;
    auto newState = baseState;

    do {
      // 应用更新
      if (update->hasEagerState && update->eagerState.has_value()) {
        newState = update->eagerState.value();
      } else {
        newState = reducer(newState, update->action);
      }
      update = update->next;
    } while (update != nullptr && update != first);

    hook->memoizedState = newState;
    hook->baseState = newState;
    queue->pending = nullptr;
    queue->lastRenderedState = newState;
  }

  return {hook->memoizedState, queue->dispatch};
}

std::any ReactFiberHooksImpl::updateMemo(
  std::function<std::any()> create,
  std::optional<std::vector<std::any>> deps
) {
  auto hook = updateWorkInProgressHook();
  auto nextDeps = deps.value_or(std::vector<std::any>{});

  auto prevState = std::any_cast<std::pair<std::any, std::vector<std::any>>>(
    hook->memoizedState
  );

  // 比较依赖
  if (areHookInputsEqual(nextDeps, prevState.second)) {
    return prevState.first;
  }

  auto nextValue = create();
  hook->memoizedState = std::make_pair(nextValue, nextDeps);
  return nextValue;
}

std::any ReactFiberHooksImpl::updateCallback(std::any callback, std::vector<std::any> deps) {
  auto hook = updateWorkInProgressHook();

  auto prevState = std::any_cast<std::pair<std::any, std::vector<std::any>>>(
    hook->memoizedState
  );

  if (areHookInputsEqual(deps, prevState.second)) {
    return prevState.first;
  }

  hook->memoizedState = std::make_pair(callback, deps);
  return callback;
}

std::shared_ptr<std::any> ReactFiberHooksImpl::updateRef(std::any) {
  auto hook = updateWorkInProgressHook();
  return std::any_cast<std::shared_ptr<std::any>>(hook->memoizedState);
}

void ReactFiberHooksImpl::mountEffect(
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  mountEffectImpl(
    Passive | PassiveStatic,
    HookPassive,
    create,
    deps
  );
}

void ReactFiberHooksImpl::mountLayoutEffect(
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  mountEffectImpl(
    Update | LayoutStatic,
    HookLayout,
    create,
    deps
  );
}

void ReactFiberHooksImpl::updateEffect(
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  updateEffectImpl(Passive, HookPassive, create, deps);
}

void ReactFiberHooksImpl::updateLayoutEffect(
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  updateEffectImpl(Update, HookLayout, create, deps);
}

void ReactFiberHooksImpl::mountEffectImpl(
  Flags fiberFlags,
  HookFlags hookFlags,
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  auto hook = mountWorkInProgressHook();
  auto nextDeps = deps.value_or(std::vector<std::any>{});
  (void)nextDeps;

  context_.currentlyRenderingFiber->flags |= fiberFlags;

  hook->memoizedState = pushEffect(
    static_cast<HookFlags>(HookHasEffect | hookFlags),
    create,
    std::any{},
    deps
  );
}

void ReactFiberHooksImpl::updateEffectImpl(
  Flags fiberFlags,
  HookFlags hookFlags,
  std::function<std::function<void()>()> create,
  std::optional<std::vector<std::any>> deps
) {
  auto hook = updateWorkInProgressHook();
  auto nextDeps = deps.value_or(std::vector<std::any>{});

  if (hook->memoizedState.has_value()) {
    auto effect = std::any_cast<HookEffectRef>(hook->memoizedState);

    if (context_.currentHook != nullptr) {
      auto prevEffect = std::any_cast<HookEffectRef>(
        context_.currentHook->memoizedState
      );

      if (areHookInputsEqual(nextDeps, prevEffect->deps)) {
        // 依赖没变，不需要重新运行
        hook->memoizedState = pushEffect(
          hookFlags,
          create,
          effect->inst,
          deps
        );
        return;
      }
    }
  }

  context_.currentlyRenderingFiber->flags |= fiberFlags;

  hook->memoizedState = pushEffect(
    static_cast<HookFlags>(HookHasEffect | hookFlags),
    create,
    std::any{},
    deps
  );
}

HookEffectRef ReactFiberHooksImpl::pushEffect(
  HookFlags tag,
  std::function<std::function<void()>()> create,
  std::any inst,
  std::optional<std::vector<std::any>> deps
) {
  auto effect = std::make_shared<Effect>();
  effect->tag = tag;
  effect->create = create;
  effect->inst = inst;
  effect->deps = deps.value_or(std::vector<std::any>{});
  effect->next = nullptr;

  // 添加到更新队列
  FunctionComponentUpdateQueueRef updateQueue;
  if (auto* existingQueue = std::get_if<FunctionComponentUpdateQueueRef>(
        &context_.currentlyRenderingFiber->updateQueue)) {
    updateQueue = *existingQueue;
  } else {
    updateQueue = createFunctionComponentUpdateQueue();
    context_.currentlyRenderingFiber->updateQueue = updateQueue;
  }

  if (updateQueue->lastEffect == nullptr) {
    // 第一个 effect
    effect->next = effect;  // 循环链表
    updateQueue->lastEffect = effect;
  } else {
    // 插入到链表
    auto firstEffect = updateQueue->lastEffect->next;
    updateQueue->lastEffect->next = effect;
    effect->next = firstEffect;
    updateQueue->lastEffect = effect;
  }

  return effect;
}

bool ReactFiberHooksImpl::areHookInputsEqual(
  const std::vector<std::any>& nextDeps,
  const std::vector<std::any>& prevDeps
) {
  if (prevDeps.empty()) {
    return false;
  }

  if (nextDeps.size() != prevDeps.size()) {
    return false;
  }

  for (size_t i = 0; i < prevDeps.size(); i++) {
    if (!shared::objectIs(nextDeps[i], prevDeps[i])) {
      return false;
    }
  }

  return true;
}

void ReactFiberHooksImpl::dispatchAction(
  FiberRef fiber,
  std::shared_ptr<void> queue,
  std::any action
) {
  // 基本实现，派发到适当的处理器
  dispatchSetState(fiber, queue, action);
}

void ReactFiberHooksImpl::dispatchSetState(
  FiberRef fiber,
  std::shared_ptr<void> queuePtr,
  std::any action
) {
  auto queue = std::static_pointer_cast<HookUpdateQueue<std::any, std::any>>(queuePtr);

  // 创建更新
  auto update = std::make_shared<HookUpdate<std::any, std::any>>();
  update->lane = requestUpdateLane();
  update->action = action;
  update->hasEagerState = false;
  update->eagerState = std::nullopt;
  update->next = nullptr;

  // 尝试 eager state 计算
  if (queue->lastRenderedReducer != nullptr && queue->lastRenderedState.has_value()) {
    try {
      auto currentState = queue->lastRenderedState.value();
      auto eagerState = queue->lastRenderedReducer(currentState, action);

      update->hasEagerState = true;
      update->eagerState = eagerState;

      // 如果状态没变，可以 bailout
      if (shared::objectIs(eagerState, currentState)) {
        return;  // Bailout
      }
    } catch (...) {
      // 计算失败，正常排队
    }
  }

  // 添加到更新队列
  if (queue->pending == nullptr) {
    update->next = update;  // 循环链表
  } else {
    update->next = queue->pending->next;
    queue->pending->next = update;
  }
  queue->pending = update;

  // 调度更新
  scheduleUpdateOnFiber(fiber, update->lane);
}

void ReactFiberHooksImpl::dispatchReducerAction(
  FiberRef fiber,
  std::shared_ptr<void> queuePtr,
  std::any action
) {
  auto queue = std::static_pointer_cast<HookUpdateQueue<std::any, std::any>>(queuePtr);

  auto update = std::make_shared<HookUpdate<std::any, std::any>>();
  update->lane = requestUpdateLane();
  update->action = action;
  update->hasEagerState = false;
  update->eagerState = std::nullopt;
  update->next = nullptr;

  // 添加到更新队列
  if (queue->pending == nullptr) {
    update->next = update;
  } else {
    update->next = queue->pending->next;
    queue->pending->next = update;
  }
  queue->pending = update;

  // 调度更新
  scheduleUpdateOnFiber(fiber, update->lane);
}

std::any ReactFiberHooksImpl::renderWithHooks(
  FiberRef current,
  FiberRef workInProgress,
  std::any Component,
  std::any props,
  std::any secondArg,
  Lanes nextRenderLanes
) {
  // 设置渲染上下文
  renderLanes_ = nextRenderLanes;
  context_.currentlyRenderingFiber = workInProgress;

  // 重置 fiber 状态
  workInProgress->memoizedState = std::any{};
  workInProgress->updateQueue = std::monostate{};
  workInProgress->lanes = NoLanes;

  // 选择正确的 Dispatcher
  if (current == nullptr || !current->memoizedState.has_value()) {
    // Mount
    currentDispatcher_ = &hooksDispatcherOnMount_;
  } else {
    // Update
    currentDispatcher_ = &hooksDispatcherOnUpdate_;
  }

  // 调用组件函数
  std::any children;
  try {
    if (Component.type() == typeid(std::function<std::any(std::any, std::any)>)) {
      auto fn = std::any_cast<std::function<std::any(std::any, std::any)>>(Component);
      children = fn(props, secondArg);
    } else if (Component.type() == typeid(std::function<std::any(std::any)>)) {
      auto fn = std::any_cast<std::function<std::any(std::any)>>(Component);
      children = fn(props);
    } else if (Component.type() == typeid(std::function<std::any()>)) {
      auto fn = std::any_cast<std::function<std::any()>>(Component);
      children = fn();
    }
  } catch (...) {
    resetHooksAfterThrow();
    throw;
  }

  // 检查渲染阶段更新
  if (context_.didScheduleRenderPhaseUpdateDuringThisPass) {
    children = renderWithHooksAgain(workInProgress, Component, props, secondArg);
  }

  finishRenderingHooks(current, workInProgress);
  return children;
}

void ReactFiberHooksImpl::finishRenderingHooks(FiberRef /*current*/, FiberRef /*workInProgress*/) {
  // 切换到 Context Only Dispatcher
  currentDispatcher_ = &contextOnlyDispatcher_;

  // 检查是否渲染的 Hooks 数量不对
  if (context_.currentHook != nullptr && context_.currentHook->next != nullptr) {
    throw std::runtime_error(
      "Rendered fewer hooks than expected. This may be caused by "
      "an accidental early return statement."
    );
  }

  // 重置状态
  renderLanes_ = NoLanes;
  context_.currentlyRenderingFiber = nullptr;
  context_.currentHook = nullptr;
  context_.workInProgressHook = nullptr;
  context_.didScheduleRenderPhaseUpdate = false;
  context_.didScheduleRenderPhaseUpdateDuringThisPass = false;

  localIdCounter_ = 0;
  context_.thenableIndexCounter = 0;
  context_.thenableState = std::any{};
}

std::any ReactFiberHooksImpl::renderWithHooksAgain(
  FiberRef workInProgress,
  std::any Component,
  std::any props,
  std::any secondArg
) {
  context_.currentlyRenderingFiber = workInProgress;

  int numberOfReRenders = 0;
  std::any children;

  do {
    context_.didScheduleRenderPhaseUpdateDuringThisPass = false;
    context_.thenableIndexCounter = 0;

    if (numberOfReRenders >= RE_RENDER_LIMIT) {
      throw std::runtime_error(
        "Too many re-renders. React limits the number of renders "
        "to prevent an infinite loop."
      );
    }

    numberOfReRenders++;

    // 重置 Hook 链表
    context_.currentHook = nullptr;
    context_.workInProgressHook = nullptr;

    // 使用 Rerender Dispatcher
    currentDispatcher_ = &hooksDispatcherOnRerender_;

    // 重新调用组件
    if (Component.type() == typeid(std::function<std::any(std::any, std::any)>)) {
      auto fn = std::any_cast<std::function<std::any(std::any, std::any)>>(Component);
      children = fn(props, secondArg);
    } else if (Component.type() == typeid(std::function<std::any(std::any)>)) {
      auto fn = std::any_cast<std::function<std::any(std::any)>>(Component);
      children = fn(props);
    } else if (Component.type() == typeid(std::function<std::any()>)) {
      auto fn = std::any_cast<std::function<std::any()>>(Component);
      children = fn();
    }
  } while (context_.didScheduleRenderPhaseUpdateDuringThisPass);

  return children;
}

void ReactFiberHooksImpl::bailoutHooks(FiberRef current, FiberRef workInProgress, Lanes lanes) {
  workInProgress->updateQueue = current->updateQueue;
  workInProgress->flags &= ~(Passive | Update);
  current->lanes = removeLanes(current->lanes, lanes);
}

bool ReactFiberHooksImpl::checkDidRenderIdHook() {
  bool didRenderIdHook = localIdCounter_ != 0;
  localIdCounter_ = 0;
  return didRenderIdHook;
}

void ReactFiberHooksImpl::resetHooksAfterThrow() {
  context_.currentlyRenderingFiber = nullptr;
  currentDispatcher_ = &contextOnlyDispatcher_;
}

void ReactFiberHooksImpl::resetHooksOnUnwind(FiberRef workInProgress) {
  if (context_.didScheduleRenderPhaseUpdate) {
    // 清除渲染阶段更新
    HookRef hook = std::any_cast<HookRef>(workInProgress->memoizedState);
    while (hook != nullptr) {
      if (hook->queue.has_value()) {
        // 重置 queue.pending
      }
      hook = hook->next;
    }
    context_.didScheduleRenderPhaseUpdate = false;
  }

  renderLanes_ = NoLanes;
  context_.currentlyRenderingFiber = nullptr;
  context_.currentHook = nullptr;
  context_.workInProgressHook = nullptr;
  context_.didScheduleRenderPhaseUpdateDuringThisPass = false;
  localIdCounter_ = 0;
  context_.thenableIndexCounter = 0;
  context_.thenableState = std::any{};
}

Dispatcher& ReactFiberHooksImpl::getCurrentDispatcher() {
  return *currentDispatcher_;
}

Lane ReactFiberHooksImpl::requestUpdateLane() {
  return SyncLane;
}

void ReactFiberHooksImpl::scheduleUpdateOnFiber(FiberRef fiber, Lane lane) {
  (void)fiber;
  (void)lane;
  // TODO: 连接到 ReactFiberWorkLoop
  // 这需要在实际使用时注入 WorkLoop 的引用
}

void ReactFiberHooksImpl::initializeDispatchers() {
  hooksDispatcherOnMount_.useState = [this](std::any initialState) {
    return mountState(initialState);
  };

  hooksDispatcherOnMount_.useReducer = [this](
    std::function<std::any(std::any, std::any)> reducer,
    std::any initialArg,
    std::optional<std::function<std::any(std::any)>> init
  ) {
    return mountReducer(reducer, initialArg, init);
  };

  hooksDispatcherOnMount_.useEffect = [this](
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    mountEffect(create, deps);
  };

  hooksDispatcherOnMount_.useLayoutEffect = [this](
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    mountLayoutEffect(create, deps);
  };

  hooksDispatcherOnMount_.useMemo = [this](
    std::function<std::any()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    return mountMemo(create, deps);
  };

  hooksDispatcherOnMount_.useCallback = [this](
    std::any callback,
    std::vector<std::any> deps
  ) {
    return mountCallback(callback, deps);
  };

  hooksDispatcherOnMount_.useRef = [this](std::any initialValue) {
    return mountRef(initialValue);
  };

  hooksDispatcherOnMount_.useContext = [](std::any context) -> std::any {
    (void)context;
    // TODO: 实现 readContext
    return std::any{};
  };

  hooksDispatcherOnMount_.useTransition = [this]()
    -> std::pair<bool, std::function<void(std::function<void()>)>> {
    return mountTransition();
  };

  hooksDispatcherOnMount_.useDeferredValue = [this](
    std::any value,
    std::optional<std::any> initialValue
  ) {
    return mountDeferredValue(value, initialValue);
  };

  hooksDispatcherOnMount_.useId = [this]() {
    return mountId();
  };

  hooksDispatcherOnUpdate_.useState = [this](std::any initialState) {
    return updateState(initialState);
  };

  hooksDispatcherOnUpdate_.useReducer = [this](
    std::function<std::any(std::any, std::any)> reducer,
    std::any initialArg,
    std::optional<std::function<std::any(std::any)>> init
  ) {
    return updateReducer(reducer, initialArg, init);
  };

  hooksDispatcherOnUpdate_.useEffect = [this](
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    updateEffect(create, deps);
  };

  hooksDispatcherOnUpdate_.useLayoutEffect = [this](
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    updateLayoutEffect(create, deps);
  };

  hooksDispatcherOnUpdate_.useMemo = [this](
    std::function<std::any()> create,
    std::optional<std::vector<std::any>> deps
  ) {
    return updateMemo(create, deps);
  };

  hooksDispatcherOnUpdate_.useCallback = [this](
    std::any callback,
    std::vector<std::any> deps
  ) {
    return updateCallback(callback, deps);
  };

  hooksDispatcherOnUpdate_.useRef = [this](std::any initialValue) {
    return updateRef(initialValue);
  };

  hooksDispatcherOnUpdate_.useContext = [](std::any context) -> std::any {
    (void)context;
    return std::any{};
  };

  hooksDispatcherOnUpdate_.useTransition = [this]()
    -> std::pair<bool, std::function<void(std::function<void()>)>> {
    return updateTransition();
  };

  hooksDispatcherOnUpdate_.useDeferredValue = [this](
    std::any value,
    std::optional<std::any> initialValue
  ) {
    (void)initialValue;
    return updateDeferredValue(value);
  };

  hooksDispatcherOnUpdate_.useId = [this]() {
    return updateId();
  };

  hooksDispatcherOnRerender_ = hooksDispatcherOnUpdate_;

  contextOnlyDispatcher_.useState = [](std::any)
    -> std::pair<std::any, std::function<void(std::any)>> {
    throwInvalidHookError();
    return {{}, nullptr};
  };

  contextOnlyDispatcher_.useReducer = [](
    std::function<std::any(std::any, std::any)>,
    std::any,
    std::optional<std::function<std::any(std::any)>>
  ) -> std::pair<std::any, std::function<void(std::any)>> {
    throwInvalidHookError();
    return {{}, nullptr};
  };

  contextOnlyDispatcher_.useEffect = [](
    std::function<std::function<void()>()>,
    std::optional<std::vector<std::any>>
  ) {
    throwInvalidHookError();
  };

  contextOnlyDispatcher_.useLayoutEffect = [](
    std::function<std::function<void()>()>,
    std::optional<std::vector<std::any>>
  ) {
    throwInvalidHookError();
  };

  contextOnlyDispatcher_.useMemo = [](
    std::function<std::any()>,
    std::optional<std::vector<std::any>>
  ) -> std::any {
    throwInvalidHookError();
    return {};
  };

  contextOnlyDispatcher_.useCallback = [](
    std::any,
    std::vector<std::any>
  ) -> std::any {
    throwInvalidHookError();
    return {};
  };

  contextOnlyDispatcher_.useRef = [](std::any) -> std::shared_ptr<std::any> {
    throwInvalidHookError();
    return nullptr;
  };

  contextOnlyDispatcher_.useContext = [](std::any) -> std::any {
    return std::any{};
  };

  contextOnlyDispatcher_.useTransition = []()
    -> std::pair<bool, std::function<void(std::function<void()>)>> {
    throwInvalidHookError();
    return {false, nullptr};
  };

  contextOnlyDispatcher_.useDeferredValue = [](std::any, std::optional<std::any>)
    -> std::any {
    throwInvalidHookError();
    return {};
  };

  contextOnlyDispatcher_.useId = []() -> std::string {
    throwInvalidHookError();
    return "";
  };

  currentDispatcher_ = &contextOnlyDispatcher_;
}

std::pair<bool, std::function<void(std::function<void()>)>> ReactFiberHooksImpl::mountTransition() {
  auto hook = mountWorkInProgressHook();
  hook->memoizedState = false;

  auto startTransition = [](std::function<void()> callback) {
    callback();
  };

  auto hook2 = mountWorkInProgressHook();
  hook2->memoizedState = startTransition;

  return {false, startTransition};
}

std::pair<bool, std::function<void(std::function<void()>)>> ReactFiberHooksImpl::updateTransition() {
  auto [isPending, dispatch] = updateState(std::any{false});
  (void)dispatch;

  auto hook = updateWorkInProgressHook();
  auto start = std::any_cast<std::function<void(std::function<void()>)>>(
    hook->memoizedState
  );

  return {std::any_cast<bool>(isPending), start};
}

std::any ReactFiberHooksImpl::mountDeferredValue(
  std::any value,
  std::optional<std::any> initialValue
) {
  auto hook = mountWorkInProgressHook();

  if (initialValue.has_value()) {
    hook->memoizedState = initialValue.value();
    return initialValue.value();
  }

  hook->memoizedState = value;
  return value;
}

std::any ReactFiberHooksImpl::updateDeferredValue(std::any value) {
  auto hook = updateWorkInProgressHook();
  (void)hook->memoizedState;

  hook->memoizedState = value;
  return value;
}

std::string ReactFiberHooksImpl::mountId() {
  auto hook = mountWorkInProgressHook();

  std::string id = ":r" + std::to_string(globalClientIdCounter_++) + ":";
  hook->memoizedState = id;
  localIdCounter_++;

  return id;
}

std::string ReactFiberHooksImpl::updateId() {
  auto hook = updateWorkInProgressHook();
  return std::any_cast<std::string>(hook->memoizedState);
}

void ReactFiberHooksImpl::throwInvalidHookError() {
  throw std::runtime_error(
    "Invalid hook call. Hooks can only be called inside of the body "
    "of a function component."
  );
}

} // namespace react::reconciler
