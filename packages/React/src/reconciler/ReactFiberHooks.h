/**
 * React Fiber Hooks
 * 
 * Hooks 是 React 函数组件的核心机制
 * 提供状态管理、副作用、上下文等功能
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberHooks.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>
#include <vector>

#include "ReactFiber.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactHookEffectTags.h"

namespace react::reconciler {

// 前向声明
class ReactFiberWorkLoop;
struct Effect;
struct EffectInstance;

// =============================================================================
// Effect 相关类型定义 (完整定义在 ReactFiberCommitEffects.h)
// =============================================================================

// 前向声明 Effect 和 FunctionComponentUpdateQueue (在 ReactFiberCommitEffects.h 中定义)
struct Effect;
struct EffectInstance;
struct FunctionComponentUpdateQueue;

using EffectRef = std::shared_ptr<Effect>;
using HookEffectRef = std::shared_ptr<Effect>;
using FunctionComponentUpdateQueueRef = std::shared_ptr<FunctionComponentUpdateQueue>;

// =============================================================================
// Hook 类型枚举
// @source:types HookType
// =============================================================================

enum class HookType {
    UseState,
    UseReducer,
    UseEffect,
    UseLayoutEffect,
    UseInsertionEffect,
    UseMemo,
    UseCallback,
    UseRef,
    UseContext,
    UseImperativeHandle,
    UseDebugValue,
    UseDeferredValue,
    UseTransition,
    UseId,
    UseOptimistic,
    UseFormStatus,
    UseActionState,
    Use,
    UseMemoCache
};

// =============================================================================
// HookUpdate 结构 (用于 useState/useReducer)
// @source:164-173 Update
// =============================================================================

template<typename S, typename A>
struct HookUpdate {
    Lane lane = NoLane;
    Lane revertLane = NoLane;
    A action;
    bool hasEagerState = false;
    std::optional<S> eagerState = std::nullopt;
    std::shared_ptr<HookUpdate<S, A>> next = nullptr;
};

// =============================================================================
// HookUpdateQueue 结构 (用于 useState/useReducer)
// @source:175-182 UpdateQueue
// =============================================================================

template<typename S, typename A>
struct HookUpdateQueue {
    std::shared_ptr<HookUpdate<S, A>> pending = nullptr;
    Lanes lanes = NoLanes;
    std::function<void(A)> dispatch = nullptr;
    std::function<S(S, A)> lastRenderedReducer = nullptr;
    std::optional<S> lastRenderedState = std::nullopt;
};

// =============================================================================
// Hook 结构
// @source:186-193 Hook
// =============================================================================

struct Hook {
  std::any memoizedState;
  std::any baseState;
  std::shared_ptr<void> baseQueue = nullptr;  // Update queue
  std::any queue;  // UpdateQueue
  std::shared_ptr<Hook> next = nullptr;
};

using HookRef = std::shared_ptr<Hook>;

// =============================================================================
// Dispatcher 接口
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js
// =============================================================================

struct Dispatcher {
    // =========================================================================
    // 状态 Hooks
    // =========================================================================
    
    /**
     * useState
     * @source:hook useState
     */
    std::function<std::pair<std::any, std::function<void(std::any)>>(std::any initialState)> useState;
    
    /**
     * useReducer
     * @source:hook useReducer
     */
    std::function<std::pair<std::any, std::function<void(std::any)>>(
        std::function<std::any(std::any, std::any)> reducer,
        std::any initialArg,
        std::optional<std::function<std::any(std::any)>> init
    )> useReducer;
    
    // =========================================================================
    // 副作用 Hooks
    // =========================================================================
    
    /**
     * useEffect
     * @source:hook useEffect
     */
    std::function<void(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    )> useEffect;
    
    /**
     * useLayoutEffect
     * @source:hook useLayoutEffect
     */
    std::function<void(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    )> useLayoutEffect;
    
    /**
     * useInsertionEffect
     * @source:hook useInsertionEffect
     */
    std::function<void(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    )> useInsertionEffect;
    
    // =========================================================================
    // 记忆化 Hooks
    // =========================================================================
    
    /**
     * useMemo
     * @source:hook useMemo
     */
    std::function<std::any(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    )> useMemo;
    
    /**
     * useCallback
     * @source:hook useCallback
     */
    std::function<std::any(
        std::any callback,
        std::vector<std::any> deps
    )> useCallback;
    
    // =========================================================================
    // 引用 Hooks
    // =========================================================================
    
    /**
     * useRef
     * @source:hook useRef
     */
    std::function<std::shared_ptr<std::any>(std::any initialValue)> useRef;
    
    /**
     * useImperativeHandle
     * @source:hook useImperativeHandle
     */
    std::function<void(
        std::any ref,
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    )> useImperativeHandle;
    
    // =========================================================================
    // 上下文 Hooks
    // =========================================================================
    
    /**
     * useContext
     * @source:hook useContext
     */
    std::function<std::any(std::any context)> useContext;
    
    // =========================================================================
    // 并发 Hooks
    // =========================================================================
    
    /**
     * useTransition
     * @source:hook useTransition
     */
    std::function<std::pair<bool, std::function<void(std::function<void()>)>>()> useTransition;
    
    /**
     * useDeferredValue
     * @source:hook useDeferredValue
     */
    std::function<std::any(std::any value, std::optional<std::any> initialValue)> useDeferredValue;
    
    // =========================================================================
    // 其他 Hooks
    // =========================================================================
    
    /**
     * useId
     * @source:hook useId
     */
    std::function<std::string()> useId;
    
    /**
     * useDebugValue
     * @source:hook useDebugValue
     */
    std::function<void(std::any value, std::optional<std::function<std::any(std::any)>> format)> useDebugValue;
    
    /**
     * use
     * @source:hook use
     */
    std::function<std::any(std::any usable)> use;
    
    /**
     * useOptimistic
     * @source:hook useOptimistic
     */
    std::function<std::pair<std::any, std::function<void(std::any)>>(
        std::any passthrough,
        std::optional<std::function<std::any(std::any, std::any)>> reducer
    )> useOptimistic;
    
    /**
     * useActionState
     * @source:hook useActionState
     */
    std::function<std::tuple<std::any, std::function<void(std::any)>, bool>(
        std::function<std::any(std::any, std::any)> action,
        std::any initialState,
        std::optional<std::string> permalink
    )> useActionState;
    
    /**
     * useMemoCache
     * @source:hook useMemoCache
     */
    std::function<std::vector<std::any>(size_t size)> useMemoCache;
};

// =============================================================================
// Hooks 上下文状态
// @source:220-280 ReactFiberHooks.js globals
// =============================================================================

struct HooksContext {
    // @source:230 当前正在渲染的 Fiber
    FiberRef currentlyRenderingFiber = nullptr;
    
    // @source:235 当前 Hook
    HookRef currentHook = nullptr;
    
    // @source:238 工作中的 Hook
    HookRef workInProgressHook = nullptr;
    
    // @source:242 是否在重新渲染期间
    bool didScheduleRenderPhaseUpdate = false;
    
    // @source:245 是否在重新渲染期间有更新
    bool didScheduleRenderPhaseUpdateDuringThisPass = false;
    
    // @source:248 渲染阶段更新计数
    int renderPhaseUpdateCount = 0;
    
    // @source:252 Thenable 状态
    std::any thenableState;
    
    // @source:255 Thenable 索引计数
    int thenableIndexCounter = 0;
    
    /**
     * 重置上下文
     */
    void reset() {
        currentlyRenderingFiber = nullptr;
        currentHook = nullptr;
        workInProgressHook = nullptr;
        didScheduleRenderPhaseUpdate = false;
        didScheduleRenderPhaseUpdateDuringThisPass = false;
        renderPhaseUpdateCount = 0;
        thenableState = std::any{};
        thenableIndexCounter = 0;
    }
};

// =============================================================================
// ReactFiberHooks 类
// @source reactjs/packages/react-reconciler/src/ReactFiberHooks.js
// =============================================================================

class ReactFiberHooks {
public:
    ReactFiberHooks() = default;
    ~ReactFiberHooks() = default;
    
    // =========================================================================
    // 核心渲染 API
    // =========================================================================
    
    /**
     * 使用 Hooks 渲染组件
     * @source:520-650 renderWithHooks
     */
    std::any renderWithHooks(
        FiberRef current,
        FiberRef workInProgress,
        std::any Component,
        std::any props,
        std::any secondArg,
        Lanes nextRenderLanes
    );
    
    /**
     * 退出 Hooks 渲染
     * @source:655-720 finishRenderingHooks
     */
    void finishRenderingHooks(FiberRef current, FiberRef workInProgress);
    
    /**
     * 重新渲染 (处理渲染阶段更新)
     * @source:725-850 rerender
     */
    std::any renderWithHooksAgain(
        FiberRef workInProgress,
        std::any Component,
        std::any props,
        std::any secondArg
    );
    
    /**
     * bailout Hooks
     * @source:855-920 bailoutHooks
     */
    void bailoutHooks(
        FiberRef current,
        FiberRef workInProgress,
        Lanes lanes
    );
    
    /**
     * 检查是否收到更新
     * @source:925-945 checkDidRenderIdHook
     */
    bool checkDidRenderIdHook();
    
    // =========================================================================
    // 错误处理
    // =========================================================================
    
    /**
     * 抛出后重置 Hooks
     * @source:950-1020 resetHooksAfterThrow
     */
    void resetHooksAfterThrow();
    
    /**
     * 在展开时重置 Hooks
     * @source:1025-1090 resetHooksOnUnwind
     */
    void resetHooksOnUnwind(FiberRef workInProgress);
    
    // =========================================================================
    // Dispatcher 管理
    // =========================================================================
    
    /**
     * 获取当前 Dispatcher
     */
    Dispatcher& getCurrentDispatcher();
    
    /**
     * 获取 Mount Dispatcher
     */
    const Dispatcher& getHooksDispatcherOnMount() const {
        return hooksDispatcherOnMount_;
    }
    
    /**
     * 获取 Update Dispatcher
     */
    const Dispatcher& getHooksDispatcherOnUpdate() const {
        return hooksDispatcherOnUpdate_;
    }
    
    /**
     * 获取 Rerender Dispatcher
     */
    const Dispatcher& getHooksDispatcherOnRerender() const {
        return hooksDispatcherOnRerender_;
    }
    
    /**
     * 获取 Context Only Dispatcher (无效状态)
     */
    const Dispatcher& getContextOnlyDispatcher() const {
        return contextOnlyDispatcher_;
    }
    
    // =========================================================================
    // 上下文访问
    // =========================================================================
    
    HooksContext& getContext() { return context_; }
    const HooksContext& getContext() const { return context_; }
    
    /**
     * 获取当前渲染的 Fiber
     */
    FiberRef getCurrentlyRenderingFiber() const {
        return context_.currentlyRenderingFiber;
    }

private:
    // Hooks 上下文
    HooksContext context_;
    
    // Dispatchers
    Dispatcher hooksDispatcherOnMount_;
    Dispatcher hooksDispatcherOnUpdate_;
    Dispatcher hooksDispatcherOnRerender_;
    Dispatcher contextOnlyDispatcher_;
    
    // =========================================================================
    // 内部 Hook 实现
    // =========================================================================
    
    /**
     * 挂载工作中的 Hook
     * @source:1095-1140 mountWorkInProgressHook
     */
    HookRef mountWorkInProgressHook();
    
    /**
     * 更新工作中的 Hook
     * @source:1145-1220 updateWorkInProgressHook
     */
    HookRef updateWorkInProgressHook();
    
    /**
     * 创建 Function Component 更新队列
     */
    FunctionComponentUpdateQueueRef createFunctionComponentUpdateQueue();
    
    // =========================================================================
    // Mount 实现
    // =========================================================================
    
    /**
     * mountState
     */
    std::pair<std::any, std::function<void(std::any)>> mountState(std::any initialState);
    
    /**
     * mountReducer
     */
    std::pair<std::any, std::function<void(std::any)>> mountReducer(
        std::function<std::any(std::any, std::any)> reducer,
        std::any initialArg,
        std::optional<std::function<std::any(std::any)>> init
    );
    
    /**
     * mountEffect
     */
    void mountEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * mountLayoutEffect
     */
    void mountLayoutEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * mountMemo
     */
    std::any mountMemo(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * mountCallback
     */
    std::any mountCallback(std::any callback, std::vector<std::any> deps);
    
    /**
     * mountRef
     */
    std::shared_ptr<std::any> mountRef(std::any initialValue);
    
    // =========================================================================
    // Update 实现
    // =========================================================================
    
    /**
     * updateState
     */
    std::pair<std::any, std::function<void(std::any)>> updateState(std::any initialState);
    
    /**
     * updateReducer
     */
    std::pair<std::any, std::function<void(std::any)>> updateReducer(
        std::function<std::any(std::any, std::any)> reducer,
        std::any initialArg,
        std::optional<std::function<std::any(std::any)>> init
    );
    
    /**
     * updateEffect
     */
    void updateEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * updateLayoutEffect
     */
    void updateLayoutEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * updateMemo
     */
    std::any updateMemo(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * updateCallback
     */
    std::any updateCallback(std::any callback, std::vector<std::any> deps);
    
    /**
     * updateRef
     */
    std::shared_ptr<std::any> updateRef(std::any initialValue);
    
    // =========================================================================
    // 辅助方法
    // =========================================================================
    
    /**
     * 推送 Effect
     */
    HookEffectRef pushEffect(
        HookFlags tag,
        std::function<std::function<void()>()> create,
        std::any inst,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * 检查依赖是否变化
     */
    bool areHookInputsEqual(
        const std::vector<std::any>& nextDeps,
        const std::vector<std::any>& prevDeps
    );
    
    /**
     * dispatch Action
     */
    void dispatchAction(
        FiberRef fiber,
        std::shared_ptr<void> queue,
        std::any action
    );
    
    /**
     * dispatch Set State Action
     */
    void dispatchSetState(
        FiberRef fiber,
        std::shared_ptr<void> queue,
        std::any action
    );
    
    /**
     * dispatch Reducer Action
     */
    void dispatchReducerAction(
        FiberRef fiber,
        std::shared_ptr<void> queue,
        std::any action
    );
    
    /**
     * 初始化 Dispatchers
     */
    void initializeDispatchers();
};

// =============================================================================
// 全局 Hooks 实例
// =============================================================================

/**
 * 获取全局 Hooks 实例
 */
ReactFiberHooks& getHooks();

/**
 * 设置全局 Hooks 实例
 */
void setHooks(std::shared_ptr<ReactFiberHooks> hooks);

// =============================================================================
// 便捷函数
// =============================================================================

/**
 * 比较 Hook 输入是否相等
 * @source:1225-1260 areHookInputsEqual
 */
bool areHookInputsEqual(
    const std::vector<std::any>& nextDeps,
    const std::vector<std::any>& prevDeps
);

/**
 * 基本状态 Reducer
 * @source:1265-1275 basicStateReducer
 */
template<typename S>
S basicStateReducer(S state, std::any action) {
    if (action.type() == typeid(std::function<S(S)>)) {
        auto fn = std::any_cast<std::function<S(S)>>(action);
        return fn(state);
    }
    return std::any_cast<S>(action);
}

} // namespace react::reconciler
