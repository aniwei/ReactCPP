/**
 * React Fiber Hooks Implementation
 * 
 * Hooks 的完整实现
 * 包括 useState, useReducer, useEffect, useMemo, useCallback, useRef 等
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberHooks.js
 */

#pragma once

#include "ReactFiberHooks.h"
#include "../shared/objectIs.h"

namespace react::reconciler {

// =============================================================================
// Effect Instance (保存 destroy 函数)
// @source:202-215 EffectInstance
// =============================================================================

struct EffectInstance {
    std::function<void()> destroy;
    
    EffectInstance() : destroy(nullptr) {}
};

// =============================================================================
// Hook 常量
// =============================================================================

constexpr int RE_RENDER_LIMIT = 25;

// =============================================================================
// ReactFiberHooksImpl 实现类
// =============================================================================

class ReactFiberHooksImpl : public ReactFiberHooks {
public:
    ReactFiberHooksImpl() {
        initializeDispatchers();
    }
    
    // =========================================================================
    // 核心渲染 API 实现
    // =========================================================================
    
    /**
     * 使用 Hooks 渲染组件
     * @source:500-620 renderWithHooks
     */
    std::any renderWithHooks(
        FiberRef current,
        FiberRef workInProgress,
        std::any Component,
        std::any props,
        std::any secondArg,
        Lanes nextRenderLanes
    ) override {
        // 设置渲染上下文
        renderLanes_ = nextRenderLanes;
        context_.currentlyRenderingFiber = workInProgress;
        
        // 重置 fiber 状态
        workInProgress->memoizedState = std::any{};
        workInProgress->updateQueue = std::any{};
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
    
    /**
     * 结束 Hooks 渲染
     * @source:622-700 finishRenderingHooks
     */
    void finishRenderingHooks(FiberRef current, FiberRef workInProgress) override {
        // 切换到 Context Only Dispatcher
        currentDispatcher_ = &contextOnlyDispatcher_;
        
        // 检查是否渲染的 Hooks 数量不对
        if (context_.currentHook != nullptr && 
            context_.currentHook->next != nullptr) {
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
    
    /**
     * 重新渲染
     * @source:755-830 renderWithHooksAgain
     */
    std::any renderWithHooksAgain(
        FiberRef workInProgress,
        std::any Component,
        std::any props,
        std::any secondArg
    ) override {
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
    
    /**
     * Bailout Hooks
     * @source:890-920 bailoutHooks
     */
    void bailoutHooks(
        FiberRef current,
        FiberRef workInProgress,
        Lanes lanes
    ) override {
        workInProgress->updateQueue = current->updateQueue;
        workInProgress->flags &= ~(Flags::PassiveEffect | Flags::UpdateEffect);
        current->lanes = removeLanes(current->lanes, lanes);
    }
    
    bool checkDidRenderIdHook() override {
        bool didRenderIdHook = localIdCounter_ != 0;
        localIdCounter_ = 0;
        return didRenderIdHook;
    }
    
    void resetHooksAfterThrow() override {
        context_.currentlyRenderingFiber = nullptr;
        currentDispatcher_ = &contextOnlyDispatcher_;
    }
    
    void resetHooksOnUnwind(FiberRef workInProgress) override {
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
    
    Dispatcher& getCurrentDispatcher() override {
        return *currentDispatcher_;
    }
    
private:
    // 当前 Dispatcher
    Dispatcher* currentDispatcher_ = nullptr;
    
    // 渲染 Lanes
    Lanes renderLanes_ = NoLanes;
    
    // ID 计数器
    int localIdCounter_ = 0;
    
    // 全局客户端 ID 计数器
    static inline int globalClientIdCounter_ = 0;
    
    // =========================================================================
    // 内部 Hook 操作
    // =========================================================================
    
    /**
     * 挂载工作中的 Hook
     * @source:980-1000 mountWorkInProgressHook
     */
    HookRef mountWorkInProgressHook() override {
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
    
    /**
     * 更新工作中的 Hook
     * @source:1000-1080 updateWorkInProgressHook
     */
    HookRef updateWorkInProgressHook() override {
        HookRef nextCurrentHook = nullptr;
        
        if (context_.currentHook == nullptr) {
            auto current = context_.currentlyRenderingFiber->alternate;
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
                throw std::runtime_error(
                    "Rendered more hooks than during the previous render."
                );
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
    
    FunctionComponentUpdateQueueRef createFunctionComponentUpdateQueue() override {
        return std::make_shared<FunctionComponentUpdateQueue>();
    }
    
    // =========================================================================
    // Mount 实现
    // =========================================================================
    
    /**
     * mountState
     * @source:1910-1935 mountState
     */
    std::pair<std::any, std::function<void(std::any)>> mountState(std::any initialState) override {
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
    
    /**
     * mountReducer
     * @source:1260-1300 mountReducer
     */
    std::pair<std::any, std::function<void(std::any)>> mountReducer(
        std::function<std::any(std::any, std::any)> reducer,
        std::any initialArg,
        std::optional<std::function<std::any(std::any)>> init
    ) override {
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
    
    /**
     * mountEffect
     * @source:2680-2700 mountEffect
     */
    void mountEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
        mountEffectImpl(
            Flags::PassiveEffect | Flags::PassiveStaticEffect,
            HookPassive,
            create,
            deps
        );
    }
    
    /**
     * mountLayoutEffect
     * @source:2760-2780 mountLayoutEffect
     */
    void mountLayoutEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
        mountEffectImpl(
            Flags::UpdateEffect | Flags::LayoutStaticEffect,
            HookLayout,
            create,
            deps
        );
    }
    
    /**
     * mountMemo
     * @source:2920-2945 mountMemo
     */
    std::any mountMemo(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
        auto hook = mountWorkInProgressHook();
        auto nextDeps = deps.value_or(std::vector<std::any>{});
        auto nextValue = create();
        
        // 存储 [value, deps]
        hook->memoizedState = std::make_pair(nextValue, nextDeps);
        
        return nextValue;
    }
    
    /**
     * mountCallback
     * @source:2900-2915 mountCallback
     */
    std::any mountCallback(std::any callback, std::vector<std::any> deps) override {
        auto hook = mountWorkInProgressHook();
        hook->memoizedState = std::make_pair(callback, deps);
        return callback;
    }
    
    /**
     * mountRef
     * @source:2610-2620 mountRef
     */
    std::shared_ptr<std::any> mountRef(std::any initialValue) override {
        auto hook = mountWorkInProgressHook();
        auto ref = std::make_shared<std::any>(initialValue);
        hook->memoizedState = ref;
        return ref;
    }
    
    // =========================================================================
    // Update 实现
    // =========================================================================
    
    /**
     * updateState
     * @source:1937-1945 updateState
     */
    std::pair<std::any, std::function<void(std::any)>> updateState(std::any) override {
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
    
    /**
     * updateReducer
     * @source:1302-1530 updateReducer
     */
    std::pair<std::any, std::function<void(std::any)>> updateReducer(
        std::function<std::any(std::any, std::any)> reducer,
        std::any,
        std::optional<std::function<std::any(std::any)>>
    ) override {
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
    
    /**
     * updateEffect
     * @source:2705-2730 updateEffect
     */
    void updateEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
        updateEffectImpl(Flags::PassiveEffect, HookPassive, create, deps);
    }
    
    /**
     * updateLayoutEffect
     * @source:2785-2810 updateLayoutEffect
     */
    void updateLayoutEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
        updateEffectImpl(Flags::UpdateEffect, HookLayout, create, deps);
    }
    
    /**
     * updateMemo
     * @source:2950-2985 updateMemo
     */
    std::any updateMemo(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    ) override {
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
    
    /**
     * updateCallback
     * @source:2895-2915 updateCallback
     */
    std::any updateCallback(std::any callback, std::vector<std::any> deps) override {
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
    
    /**
     * updateRef
     * @source:2625-2630 updateRef
     */
    std::shared_ptr<std::any> updateRef(std::any) override {
        auto hook = updateWorkInProgressHook();
        return std::any_cast<std::shared_ptr<std::any>>(hook->memoizedState);
    }
    
    // =========================================================================
    // Effect 辅助函数
    // =========================================================================
    
    /**
     * mountEffectImpl
     * @source:2630-2660 mountEffectImpl
     */
    void mountEffectImpl(
        Flags fiberFlags,
        HookFlags hookFlags,
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) {
        auto hook = mountWorkInProgressHook();
        auto nextDeps = deps.value_or(std::vector<std::any>{});
        
        context_.currentlyRenderingFiber->flags |= fiberFlags;
        
        hook->memoizedState = pushEffect(
            static_cast<HookFlags>(HookHasEffect | hookFlags),
            create,
            std::any{},  // inst
            deps
        );
    }
    
    /**
     * updateEffectImpl
     * @source:2665-2710 updateEffectImpl
     */
    void updateEffectImpl(
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
    
    /**
     * pushEffect
     * @source:2560-2600 pushEffect
     */
    HookEffectRef pushEffect(
        HookFlags tag,
        std::function<std::function<void()>()> create,
        std::any inst,
        std::optional<std::vector<std::any>> deps
    ) override {
        auto effect = std::make_shared<HookEffect>();
        effect->tag = tag;
        effect->create = create;
        effect->inst = inst;
        effect->deps = deps.value_or(std::vector<std::any>{});
        effect->next = nullptr;
        
        // 添加到更新队列
        FunctionComponentUpdateQueueRef updateQueue;
        if (context_.currentlyRenderingFiber->updateQueue.has_value()) {
            updateQueue = std::any_cast<FunctionComponentUpdateQueueRef>(
                context_.currentlyRenderingFiber->updateQueue
            );
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
    
    // =========================================================================
    // 依赖比较
    // =========================================================================
    
    /**
     * 比较 Hook 输入是否相等
     * @source:455-500 areHookInputsEqual
     */
    bool areHookInputsEqual(
        const std::vector<std::any>& nextDeps,
        const std::vector<std::any>& prevDeps
    ) override {
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
    
    // =========================================================================
    // Dispatch 函数
    // =========================================================================
    
    void dispatchAction(
        FiberRef fiber,
        std::shared_ptr<void> queue,
        std::any action
    ) override {
        // 基本实现，派发到适当的处理器
        dispatchSetState(fiber, queue, action);
    }
    
    /**
     * dispatchSetState
     * @source:3350-3450 dispatchSetState
     */
    void dispatchSetState(
        FiberRef fiber,
        std::shared_ptr<void> queuePtr,
        std::any action
    ) override {
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
    
    /**
     * dispatchReducerAction
     * @source:3450-3520 dispatchReducerAction
     */
    void dispatchReducerAction(
        FiberRef fiber,
        std::shared_ptr<void> queuePtr,
        std::any action
    ) override {
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
    
    // =========================================================================
    // 辅助函数
    // =========================================================================
    
    /**
     * 请求更新 Lane
     */
    Lane requestUpdateLane() {
        // 简化实现，返回 SyncLane
        return SyncLane;
    }
    
    /**
     * 调度 Fiber 更新 (需要连接到 WorkLoop)
     */
    void scheduleUpdateOnFiber(FiberRef fiber, Lane lane) {
        // TODO: 连接到 ReactFiberWorkLoop
        // 这需要在实际使用时注入 WorkLoop 的引用
    }
    
    // =========================================================================
    // Dispatcher 初始化
    // =========================================================================
    
    void initializeDispatchers() override {
        // Mount Dispatcher
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
        
        // Update Dispatcher
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
            return std::any{};
        };
        
        hooksDispatcherOnUpdate_.useTransition = [this]() 
            -> std::pair<bool, std::function<void(std::function<void()>)>> {
            return updateTransition();
        };
        
        hooksDispatcherOnUpdate_.useDeferredValue = [this](
            std::any value,
            std::optional<std::any>
        ) {
            return updateDeferredValue(value);
        };
        
        hooksDispatcherOnUpdate_.useId = [this]() {
            return updateId();
        };
        
        // Rerender Dispatcher (类似 Update)
        hooksDispatcherOnRerender_ = hooksDispatcherOnUpdate_;
        
        // Context Only Dispatcher (抛出错误)
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
            // Context 在组件外部也可以读取
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
    
    // =========================================================================
    // 其他 Hooks 实现
    // =========================================================================
    
    /**
     * mountTransition
     * @source:3300-3320 mountTransition
     */
    std::pair<bool, std::function<void(std::function<void()>)>> mountTransition() {
        auto hook = mountWorkInProgressHook();
        hook->memoizedState = false;
        
        auto fiber = context_.currentlyRenderingFiber;
        
        auto startTransition = [this, fiber](std::function<void()> callback) {
            // 简化实现
            callback();
        };
        
        auto hook2 = mountWorkInProgressHook();
        hook2->memoizedState = startTransition;
        
        return {false, startTransition};
    }
    
    /**
     * updateTransition
     */
    std::pair<bool, std::function<void(std::function<void()>)>> updateTransition() {
        auto [isPending, dispatch] = updateState(std::any{false});
        
        auto hook = updateWorkInProgressHook();
        auto start = std::any_cast<std::function<void(std::function<void()>)>>(
            hook->memoizedState
        );
        
        return {std::any_cast<bool>(isPending), start};
    }
    
    /**
     * mountDeferredValue
     * @source:2990-3020 mountDeferredValue
     */
    std::any mountDeferredValue(std::any value, std::optional<std::any> initialValue) {
        auto hook = mountWorkInProgressHook();
        
        if (initialValue.has_value()) {
            hook->memoizedState = initialValue.value();
            return initialValue.value();
        }
        
        hook->memoizedState = value;
        return value;
    }
    
    /**
     * updateDeferredValue
     */
    std::any updateDeferredValue(std::any value) {
        auto hook = updateWorkInProgressHook();
        auto prevValue = hook->memoizedState;
        
        // 简化实现 - 总是返回新值
        hook->memoizedState = value;
        return value;
    }
    
    /**
     * mountId
     * @source:3470-3510 mountId
     */
    std::string mountId() {
        auto hook = mountWorkInProgressHook();
        
        std::string id = ":r" + std::to_string(globalClientIdCounter_++) + ":";
        hook->memoizedState = id;
        localIdCounter_++;
        
        return id;
    }
    
    /**
     * updateId
     */
    std::string updateId() {
        auto hook = updateWorkInProgressHook();
        return std::any_cast<std::string>(hook->memoizedState);
    }
    
    /**
     * 无效 Hook 调用错误
     */
    static void throwInvalidHookError() {
        throw std::runtime_error(
            "Invalid hook call. Hooks can only be called inside of the body "
            "of a function component."
        );
    }
};

// =============================================================================
// 全局 Hooks 实例
// =============================================================================

inline std::shared_ptr<ReactFiberHooksImpl> globalHooksInstance = nullptr;

inline ReactFiberHooks& getHooks() {
    if (!globalHooksInstance) {
        globalHooksInstance = std::make_shared<ReactFiberHooksImpl>();
    }
    return *globalHooksInstance;
}

inline void setHooks(std::shared_ptr<ReactFiberHooks> hooks) {
    globalHooksInstance = std::dynamic_pointer_cast<ReactFiberHooksImpl>(hooks);
}

} // namespace react::reconciler
