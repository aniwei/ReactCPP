/**
 * React Shared Internals
 * 
 * 这是 React 内部共享状态的中心化存储
 * 主要用于 Hooks dispatcher、Cache、Transitions 等内部机制
 * 
 * @source reactjs/packages/react/src/ReactSharedInternalsClient.js
 * @source reactjs/packages/react/src/ReactSharedInternalsServer.js
 * @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:397-465
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>

namespace facebook::jsi {
class Runtime;
class Value;
class Object;
class Function;
} // namespace facebook::jsi

namespace react {

class ReactHostRuntime;

using namespace facebook;

// 前向声明
namespace reconciler {
struct Fiber;
} // namespace reconciler


// 类型定义


/**
 * Transition 类型
 * @source reactjs/packages/react/src/ReactStartTransition.js
 */
struct Transition {
    std::string name;
    double startTime;
};

/**
 * 基础状态动作类型
 * @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js
 */
template<typename S>
using BasicStateAction = std::function<S(S)>;

/**
 * Dispatch 类型
 */
template<typename A>
using Dispatch = std::function<void(A)>;


// Dispatcher 接口
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:397-459


/**
 * Hooks Dispatcher 接口
 * 
 * 这是 React Hooks 的核心调度器接口
 * 不同阶段（mount/update/server）会有不同的实现
 */
class Dispatcher {
public:
    virtual ~Dispatcher() = default;
    
    // @source:398 use<T>
    virtual jsi::Value use(jsi::Runtime& runtime, const jsi::Value& usable) = 0;
    
    // @source:399 readContext
    virtual jsi::Value readContext(jsi::Runtime& runtime, const jsi::Object& context) = 0;
    
    // @source:400 useState
    virtual jsi::Value useState(jsi::Runtime& runtime, const jsi::Value& initialState) = 0;
    
    // @source:401-405 useReducer
    virtual jsi::Value useReducer(
        jsi::Runtime& runtime,
        const jsi::Function& reducer,
        const jsi::Value& initialArg,
        const jsi::Value& init
    ) = 0;
    
    // @source:406 useContext
    virtual jsi::Value useContext(jsi::Runtime& runtime, const jsi::Object& context) = 0;
    
    // @source:407 useRef
    virtual jsi::Object useRef(jsi::Runtime& runtime, const jsi::Value& initialValue) = 0;
    
    // @source:408-411 useEffect
    virtual void useEffect(
        jsi::Runtime& runtime,
        const jsi::Function& create,
        const jsi::Value& deps
    ) = 0;
    
    // @source:413 useEffectEvent (可选)
    virtual jsi::Function useEffectEvent(jsi::Runtime& runtime, const jsi::Function& callback) {
        throw std::runtime_error("useEffectEvent not implemented");
    }
    
    // @source:414-417 useInsertionEffect
    virtual void useInsertionEffect(
        jsi::Runtime& runtime,
        const jsi::Function& create,
        const jsi::Value& deps
    ) = 0;
    
    // @source:418-421 useLayoutEffect
    virtual void useLayoutEffect(
        jsi::Runtime& runtime,
        const jsi::Function& create,
        const jsi::Value& deps
    ) = 0;
    
    // @source:422 useCallback
    virtual jsi::Value useCallback(
        jsi::Runtime& runtime,
        const jsi::Value& callback,
        const jsi::Value& deps
    ) = 0;
    
    // @source:423 useMemo
    virtual jsi::Value useMemo(
        jsi::Runtime& runtime,
        const jsi::Function& nextCreate,
        const jsi::Value& deps
    ) = 0;
    
    // @source:424-428 useImperativeHandle
    virtual void useImperativeHandle(
        jsi::Runtime& runtime,
        const jsi::Value& ref,
        const jsi::Function& create,
        const jsi::Value& deps
    ) = 0;
    
    // @source:429 useDebugValue
    virtual void useDebugValue(
        jsi::Runtime& runtime,
        const jsi::Value& value,
        const jsi::Value& formatterFn
    ) = 0;
    
    // @source:430 useDeferredValue
    virtual jsi::Value useDeferredValue(
        jsi::Runtime& runtime,
        const jsi::Value& value,
        const jsi::Value& initialValue
    ) = 0;
    
    // @source:431-434 useTransition
    virtual jsi::Value useTransition(jsi::Runtime& runtime) = 0;
    
    // @source:435-439 useSyncExternalStore
    virtual jsi::Value useSyncExternalStore(
        jsi::Runtime& runtime,
        const jsi::Function& subscribe,
        const jsi::Function& getSnapshot,
        const jsi::Value& getServerSnapshot
    ) = 0;
    
    // @source:440 useId
    virtual std::string useId(jsi::Runtime& runtime) = 0;
    
    // @source:441 useCacheRefresh
    virtual jsi::Function useCacheRefresh(jsi::Runtime& runtime) = 0;
    
    // @source:442 useMemoCache
    virtual jsi::Array useMemoCache(jsi::Runtime& runtime, int size) = 0;
    
    // @source:443 useHostTransitionStatus
    virtual jsi::Value useHostTransitionStatus(jsi::Runtime& runtime) = 0;
    
    // @source:444-447 useOptimistic
    virtual jsi::Value useOptimistic(
        jsi::Runtime& runtime,
        const jsi::Value& passthrough,
        const jsi::Value& reducer
    ) = 0;
    
    // @source:448-452 useFormState (deprecated alias)
    virtual jsi::Value useFormState(
        jsi::Runtime& runtime,
        const jsi::Function& action,
        const jsi::Value& initialState,
        const jsi::Value& permalink
    ) {
        return useActionState(runtime, action, initialState, permalink);
    }
    
    // @source:453-457 useActionState
    virtual jsi::Value useActionState(
        jsi::Runtime& runtime,
        const jsi::Function& action,
        const jsi::Value& initialState,
        const jsi::Value& permalink
    ) = 0;
};


// AsyncDispatcher 接口
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:460-465


/**
 * 异步 Dispatcher，用于 Cache 和 Server Components
 */
class AsyncDispatcher {
public:
    virtual ~AsyncDispatcher() = default;
    
    // @source:461 getCacheForType
    virtual jsi::Value getCacheForType(jsi::Runtime& runtime, const jsi::Function& resourceType) = 0;
    
    // @source:462 cacheSignal
    virtual jsi::Value cacheSignal(jsi::Runtime& runtime) = 0;
    
    // @source:464 getOwner (DEV-only)
    virtual jsi::Value getOwner(jsi::Runtime& runtime) = 0;
};


// SharedStateClient
// @source reactjs/packages/react/src/ReactSharedInternalsClient.js:24-50


/**
 * Client 端共享状态
 */
struct SharedStateClient {
    // @source:25 H: ReactCurrentDispatcher for Hooks
    Dispatcher* H = nullptr;
    
    // @source:26 A: ReactCurrentCache for Cache
    AsyncDispatcher* A = nullptr;
    
    // @source:27 T: ReactCurrentBatchConfig for Transitions
    Transition* T = nullptr;
    
    // @source:28 S: onStartTransitionFinish callback
    std::function<void(Transition*, jsi::Value)> S = nullptr;
    
    // @source:29 G: onStartGestureTransitionFinish (enableGestureTransition)
    std::function<std::function<void()>(Transition*, jsi::Value, jsi::Value)> G = nullptr;
    
    // =========================================================================
    // DEV-only 字段
    // =========================================================================
    
#ifdef DEV
    // @source:33 actQueue
    std::vector<std::function<void()>>* actQueue = nullptr;
    
    // @source:36 asyncTransitions 计数
    int asyncTransitions = 0;
    
    // @source:39 用于 legacy 模式的批处理
    bool isBatchingLegacy = false;
    bool didScheduleLegacyUpdate = false;
    
    // @source:44 跟踪是否在当前批次中调用了 use
    bool didUsePromise = false;
    
    // @source:47 跟踪第一个未捕获的 act 错误
    std::vector<jsi::Value> thrownErrors;
    
    // @source:50 getCurrentStack 函数
    std::function<std::string()> getCurrentStack = nullptr;
    
    // @source:53 recentlyCreatedOwnerStacks
    int recentlyCreatedOwnerStacks = 0;
#endif
};


// SharedStateServer
// @source reactjs/packages/react/src/ReactSharedInternalsServer.js:24-43


/**
 * Server 端共享状态
 */
struct SharedStateServer {
    // @source:27 H: ReactCurrentDispatcher for Hooks
    Dispatcher* H = nullptr;
    
    // @source:28 A: ReactCurrentCache for Cache
    AsyncDispatcher* A = nullptr;
    
    // =========================================================================
    // Taint Registry (enableTaint)
    // =========================================================================
    
    // 这些在 C++ 中通常不需要实现
    // 因为 Taint 主要用于 Server Components 的安全检查
    
    // =========================================================================
    // DEV-only 字段
    // =========================================================================
    
#ifdef DEV
    // @source:41 getCurrentStack 函数
    std::function<std::string()> getCurrentStack = nullptr;
    
    // @source:44 recentlyCreatedOwnerStacks
    int recentlyCreatedOwnerStacks = 0;
#endif
};


// ReactSharedInternals 单例


/**
 * ReactSharedInternals 全局单例
 * 
 * 这是 React 运行时的核心共享状态容器
 * 所有 Hook 调用都会通过这里获取 dispatcher
 */
class ReactSharedInternals {
public:
    static std::unique_ptr<ReactSharedInternals> create();
    
    // 禁用拷贝
    ReactSharedInternals(const ReactSharedInternals&) = delete;
    ReactSharedInternals& operator=(const ReactSharedInternals&) = delete;
    
    // =========================================================================
    // Dispatcher 管理
    // =========================================================================
    
    /**
     * 设置当前 Hook Dispatcher
     * 在渲染开始时由 Reconciler 调用
     */
    void setDispatcher(Dispatcher* dispatcher);
    
    /**
     * 获取当前 Hook Dispatcher
     * @return 当前 dispatcher，如果在渲染阶段外则为 nullptr
     */
    Dispatcher* getDispatcher() const;
    
    /**
     * 解析 Dispatcher (带错误检查)
     * @source reactjs/packages/react/src/ReactHooks.js:24-40
     */
    Dispatcher& resolveDispatcher();
    
    // =========================================================================
    // AsyncDispatcher 管理
    // =========================================================================
    
    void setAsyncDispatcher(AsyncDispatcher* dispatcher);
    
    AsyncDispatcher* getAsyncDispatcher() const;
    
    // =========================================================================
    // Transition 管理
    // =========================================================================
    
    void setTransition(Transition* transition);
    
    Transition* getTransition() const;
    
    // =========================================================================
    // Client State 访问
    // =========================================================================
    
    SharedStateClient& getClientState();
    
    const SharedStateClient& getClientState() const;
    
    // =========================================================================
    // Server State 访问
    // =========================================================================
    
    SharedStateServer& getServerState();
    
    const SharedStateServer& getServerState() const;

private:
    ReactSharedInternals() = default;
    
    SharedStateClient client_;
    SharedStateServer server_;
};


// 便捷访问函数

Dispatcher* getCurrentDispatcher(ReactHostRuntime& hostRuntime);
Dispatcher& resolveDispatcher(ReactHostRuntime& hostRuntime);
AsyncDispatcher* getCurrentAsyncDispatcher(ReactHostRuntime& hostRuntime);
Transition* getCurrentTransition(ReactHostRuntime& hostRuntime);

} // namespace react
