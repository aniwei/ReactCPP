/**
 * React Fiber Work Loop
 * 
 * 工作循环是 React Reconciler 的核心
 * 负责调度、执行和提交 Fiber 树的更新
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberWorkLoop.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <chrono>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "ReactRootTags.h"
#include "../scheduler/SchedulerPriorities.h"

namespace react::reconciler {

// =============================================================================
// 执行上下文 (Execution Context)
// @source:368-372 ReactFiberWorkLoop.js
// =============================================================================

enum class ExecutionContext : uint8_t {
    NoContext      = 0b000,
    BatchedContext = 0b001,
    RenderContext  = 0b010,
    CommitContext  = 0b100
};

inline ExecutionContext operator|(ExecutionContext a, ExecutionContext b) {
    return static_cast<ExecutionContext>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
    );
}

inline ExecutionContext operator&(ExecutionContext a, ExecutionContext b) {
    return static_cast<ExecutionContext>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
    );
}

inline ExecutionContext operator~(ExecutionContext a) {
    return static_cast<ExecutionContext>(~static_cast<uint8_t>(a));
}

inline bool hasContext(ExecutionContext context, ExecutionContext flag) {
    return (static_cast<uint8_t>(context) & static_cast<uint8_t>(flag)) != 0;
}

// =============================================================================
// 根节点退出状态 (Root Exit Status)
// @source:374-381 ReactFiberWorkLoop.js
// =============================================================================

enum class RootExitStatus : uint8_t {
    InProgress = 0,
    FatalErrored = 1,
    Errored = 2,
    Suspended = 3,
    SuspendedWithDelay = 4,
    Completed = 5,
    SuspendedAtTheShell = 6
};

// =============================================================================
// 挂起原因 (Suspended Reason)
// @source:393-402 ReactFiberWorkLoop.js
// =============================================================================

enum class SuspendedReason : uint8_t {
    NotSuspended = 0,
    SuspendedOnError = 1,
    SuspendedOnData = 2,
    SuspendedOnImmediate = 3,
    SuspendedOnInstance = 4,
    SuspendedOnInstanceAndReadyToContinue = 5,
    SuspendedOnDeprecatedThrowPromise = 6,
    SuspendedAndReadyToContinue = 7,
    SuspendedOnHydration = 8,
    SuspendedOnAction = 9
};

// =============================================================================
// 捕获的值 (Captured Value)
// @source reactjs/packages/react-reconciler/src/ReactCapturedValue.js
// =============================================================================

struct CapturedValue {
    std::any value;
    FiberWeakRef source;
    std::optional<std::string> stack;
};

// =============================================================================
// 工作循环全局状态
// @source:383-454 ReactFiberWorkLoop.js
// =============================================================================

class WorkLoopState {
public:
    // @source:383-384 执行上下文
    ExecutionContext executionContext = ExecutionContext::NoContext;
    
    // @source:386 当前正在处理的 FiberRoot
    FiberRootRef workInProgressRoot = nullptr;
    
    // @source:388 当前正在处理的 Fiber 节点
    FiberRef workInProgress = nullptr;
    
    // @source:390 当前正在渲染的 Lanes
    Lanes workInProgressRootRenderLanes = NoLanes;
    
    // @source:406 挂起原因
    SuspendedReason workInProgressSuspendedReason = SuspendedReason::NotSuspended;
    
    // @source:407 抛出的值
    std::any workInProgressThrownValue;
    
    // @source:412 是否跳过了挂起的兄弟节点
    bool workInProgressRootDidSkipSuspendedSiblings = false;
    
    // @source:416 是否为预渲染
    bool workInProgressRootIsPrerendering = false;
    
    // @source:420 是否附加了 ping 监听器
    bool workInProgressRootDidAttachPingListener = false;
    
    // @source:430 entangled lanes
    Lanes entangledRenderLanes = NoLanes;
    
    // @source:433 根节点退出状态
    RootExitStatus workInProgressRootExitStatus = RootExitStatus::InProgress;
    
    // @source:436 跳过的 lanes
    Lanes workInProgressRootSkippedLanes = NoLanes;
    
    // @source:438 交织更新的 lanes
    Lanes workInProgressRootInterleavedUpdatedLanes = NoLanes;
    
    // @source:440 渲染阶段更新的 lanes
    Lanes workInProgressRootRenderPhaseUpdatedLanes = NoLanes;
    
    // @source:442 pinged lanes
    Lanes workInProgressRootPingedLanes = NoLanes;
    
    // @source:444 deferred lane
    Lane workInProgressDeferredLane = NoLane;
    
    // @source:447 挂起的重试 lanes
    Lanes workInProgressSuspendedRetryLanes = NoLanes;
    
    // @source:450 并发错误
    std::vector<CapturedValue> workInProgressRootConcurrentErrors;
    
    // @source:453 可恢复错误
    std::vector<CapturedValue> workInProgressRootRecoverableErrors;
    
    // @source:456 是否包含递归渲染更新
    bool workInProgressRootDidIncludeRecursiveRenderUpdate = false;
    
    // @source:459 是否包含提交阶段更新
    bool didIncludeCommitPhaseUpdate = false;
    
    // @source:464 最近回退时间
    double globalMostRecentFallbackTime = 0.0;
    
    // @source:465 回退节流时间 (ms)
    static constexpr double FALLBACK_THROTTLE_MS = 300.0;
    
    // @source:469 渲染目标时间
    double workInProgressRootRenderTargetTime = std::numeric_limits<double>::infinity();
    
    // @source:472 渲染超时时间 (ms)
    static constexpr double RENDER_TIMEOUT_MS = 500.0;
    
    // 重置状态
    void reset() {
        executionContext = ExecutionContext::NoContext;
        workInProgressRoot = nullptr;
        workInProgress = nullptr;
        workInProgressRootRenderLanes = NoLanes;
        workInProgressSuspendedReason = SuspendedReason::NotSuspended;
        workInProgressThrownValue = std::any{};
        workInProgressRootDidSkipSuspendedSiblings = false;
        workInProgressRootIsPrerendering = false;
        workInProgressRootDidAttachPingListener = false;
        entangledRenderLanes = NoLanes;
        workInProgressRootExitStatus = RootExitStatus::InProgress;
        workInProgressRootSkippedLanes = NoLanes;
        workInProgressRootInterleavedUpdatedLanes = NoLanes;
        workInProgressRootRenderPhaseUpdatedLanes = NoLanes;
        workInProgressRootPingedLanes = NoLanes;
        workInProgressDeferredLane = NoLane;
        workInProgressSuspendedRetryLanes = NoLanes;
        workInProgressRootConcurrentErrors.clear();
        workInProgressRootRecoverableErrors.clear();
        workInProgressRootDidIncludeRecursiveRenderUpdate = false;
        didIncludeCommitPhaseUpdate = false;
    }
};

// =============================================================================
// 前向声明
// =============================================================================

class ReactFiberWorkLoop;
class ReactFiberBeginWork;
class ReactFiberCompleteWork;
class ReactFiberCommitWork;

// =============================================================================
// 调度器回调接口
// @source ReactFiberWorkLoop.js Scheduler imports
// =============================================================================

struct SchedulerInterface {
    using CallbackFn = std::function<void()>;
    using ShouldYieldFunc = std::function<bool()>;
    using NowFunc = std::function<double()>;
    
    std::function<std::any(scheduler::PriorityLevel, CallbackFn)> scheduleCallback;
    std::function<void(std::any)> cancelCallback;
    ShouldYieldFunc shouldYield;
    NowFunc now;
    std::function<void()> requestPaint;
};

// =============================================================================
// ReactFiberWorkLoop 类
// 工作循环的主类，管理渲染和提交流程
// @source reactjs/packages/react-reconciler/src/ReactFiberWorkLoop.js
// =============================================================================

class ReactFiberWorkLoop {
public:
    explicit ReactFiberWorkLoop(
        SchedulerInterface scheduler,
        std::shared_ptr<ReactFiberBeginWork> beginWorkHandler = nullptr,
        std::shared_ptr<ReactFiberCompleteWork> completeWorkHandler = nullptr,
        std::shared_ptr<ReactFiberCommitWork> commitWorkHandler = nullptr
    );
    
    ~ReactFiberWorkLoop() = default;
    
    // =========================================================================
    // 核心调度 API
    // =========================================================================
    
    /**
     * 调度更新到 Fiber 节点
     * @source:755-830 scheduleUpdateOnFiber
     */
    void scheduleUpdateOnFiber(
        FiberRootRef root,
        FiberRef fiber,
        Lane lane
    );
    
    /**
     * 同步刷新 root 上的工作
     * @source:975-1050 performSyncWorkOnRoot
     */
    void performSyncWorkOnRoot(FiberRootRef root, Lanes lanes);
    
    /**
     * 并发渲染 root 上的工作
     * @source:1052-1200 performConcurrentWorkOnRoot
     */
    void performConcurrentWorkOnRoot(FiberRootRef root, bool didTimeout);
    
    /**
     * 确保 root 已被调度
     * @source ReactFiberRootScheduler.js
     */
    void ensureRootIsScheduled(FiberRootRef root);
    
    // =========================================================================
    // 渲染阶段
    // =========================================================================
    
    /**
     * 准备新的工作栈
     * @source:2686-2720 prepareFreshStack
     */
    void prepareFreshStack(FiberRootRef root, Lanes lanes);
    
    /**
     * 并发渲染根节点
     * @source:2722-2800 renderRootConcurrent
     */
    RootExitStatus renderRootConcurrent(FiberRootRef root, Lanes lanes);
    
    /**
     * 同步渲染根节点
     * @source:2601-2685 renderRootSync
     */
    RootExitStatus renderRootSync(FiberRootRef root, Lanes lanes, bool exitOnSpawn);
    
    /**
     * 并发工作循环
     * @source:2799-2812 workLoopConcurrent
     */
    void workLoopConcurrent(bool nonIdle);
    
    /**
     * 同步工作循环
     * @source:2594-2599 workLoopSync
     */
    void workLoopSync();
    
    /**
     * 执行单个工作单元
     * @source:2825-2868 performUnitOfWork
     */
    void performUnitOfWork(FiberRef unitOfWork);
    
    /**
     * 完成工作单元
     * @source:3108-3169 completeUnitOfWork
     */
    void completeUnitOfWork(FiberRef unitOfWork);
    
    /**
     * 展开工作单元 (错误处理)
     * @source:3172-3234 unwindUnitOfWork
     */
    void unwindUnitOfWork(FiberRef unitOfWork, bool skipSiblings);
    
    // =========================================================================
    // 提交阶段
    // =========================================================================
    
    /**
     * 提交根节点
     * @source:3250-3600 commitRoot
     */
    void commitRoot(
        FiberRootRef root,
        Lanes recoverableErrors,
        std::any transitions,
        std::optional<std::any> didIncludeRenderPhaseUpdate,
        std::any spawnedLane,
        std::any updatedLanes,
        std::any suspendedRetryLanes
    );
    
    /**
     * 提交根节点实现
     * @source:3610-4100 commitRootImpl
     */
    void commitRootImpl(
        FiberRootRef root,
        Lanes recoverableErrors,
        std::any transitions,
        bool renderPriorityLevel,
        std::any spawnedLane,
        std::any updatedLanes,
        std::any suspendedRetryLanes,
        bool includeWorkInProgressEffects,
        std::any exitStatus
    );
    
    // =========================================================================
    // Getter/Setter
    // =========================================================================
    
    WorkLoopState& getState() { return state_; }
    const WorkLoopState& getState() const { return state_; }
    
    ExecutionContext getExecutionContext() const { 
        return state_.executionContext; 
    }
    
    FiberRef getWorkInProgress() const { 
        return state_.workInProgress; 
    }
    
    FiberRootRef getWorkInProgressRoot() const {
        return state_.workInProgressRoot;
    }
    
    Lanes getEntangledRenderLanes() const {
        return state_.entangledRenderLanes;
    }
    
    // =========================================================================
    // 工具方法
    // =========================================================================
    
    /**
     * 请求事件时间
     * @source:700-710 requestEventTime
     */
    double requestEventTime();
    
    /**
     * 请求更新 lane
     * @source:712-750 requestUpdateLane
     */
    Lane requestUpdateLane(FiberRef fiber);
    
    /**
     * 是否处于渲染阶段
     */
    bool isRendering() const {
        return hasContext(state_.executionContext, ExecutionContext::RenderContext);
    }
    
    /**
     * 是否处于提交阶段
     */
    bool isCommitting() const {
        return hasContext(state_.executionContext, ExecutionContext::CommitContext);
    }
    
    /**
     * 获取当前时间
     */
    double now() const {
        return scheduler_.now ? scheduler_.now() : 0.0;
    }
    
    /**
     * 是否应该让步
     */
    bool shouldYield() const {
        return scheduler_.shouldYield ? scheduler_.shouldYield() : false;
    }
    
private:
    // 工作循环状态
    WorkLoopState state_;
    
    // 调度器接口
    SchedulerInterface scheduler_;
    
    // 处理器
    std::shared_ptr<ReactFiberBeginWork> beginWork_;
    std::shared_ptr<ReactFiberCompleteWork> completeWork_;
    std::shared_ptr<ReactFiberCommitWork> commitWork_;
    
    // =========================================================================
    // 内部辅助方法
    // =========================================================================
    
    /**
     * 标记渲染开始
     */
    void markRenderStarted(Lanes lanes);
    
    /**
     * 标记渲染停止
     */
    void markRenderStopped();
    
    /**
     * 处理错误
     */
    void handleError(FiberRootRef root, std::any thrownValue);
    
    /**
     * 恢复挂起的工作
     */
    void resumeSuspendedWork(FiberRef fiber);
};

// =============================================================================
// 全局工作循环实例
// =============================================================================

/**
 * 获取或创建全局工作循环实例
 */
ReactFiberWorkLoop& getWorkLoop();

/**
 * 设置全局工作循环实例
 */
void setWorkLoop(std::shared_ptr<ReactFiberWorkLoop> workLoop);

// =============================================================================
// 便捷函数
// @source 导出的顶层函数
// =============================================================================

/**
 * 批量更新
 * @source:620-650 batchedUpdates
 */
template<typename T, typename A>
T batchedUpdates(std::function<T(A)> fn, A a) {
    auto& workLoop = getWorkLoop();
    ExecutionContext prevContext = workLoop.getExecutionContext();
    workLoop.getState().executionContext = prevContext | ExecutionContext::BatchedContext;
    try {
        T result = fn(a);
        workLoop.getState().executionContext = prevContext;
        return result;
    } catch (...) {
        workLoop.getState().executionContext = prevContext;
        throw;
    }
}

/**
 * 离散更新
 * @source:654-680 discreteUpdates
 */
template<typename T, typename A>
T discreteUpdates(std::function<T(A)> fn, A a) {
    auto& workLoop = getWorkLoop();
    ExecutionContext prevContext = workLoop.getExecutionContext();
    workLoop.getState().executionContext = prevContext | ExecutionContext::BatchedContext;
    try {
        T result = fn(a);
        workLoop.getState().executionContext = prevContext;
        return result;
    } catch (...) {
        workLoop.getState().executionContext = prevContext;
        throw;
    }
}

/**
 * 同步刷新
 * @source:598-618 flushSync
 */
template<typename T>
T flushSync(std::function<T()> fn) {
    auto& workLoop = getWorkLoop();
    ExecutionContext prevContext = workLoop.getExecutionContext();
    workLoop.getState().executionContext = prevContext | ExecutionContext::BatchedContext;
    try {
        T result = fn();
        // TODO: flushSyncWorkOnAllRoots
        workLoop.getState().executionContext = prevContext;
        return result;
    } catch (...) {
        workLoop.getState().executionContext = prevContext;
        throw;
    }
}

// =============================================================================
// 跳过的更新 Lanes 标记
// @source ReactFiberWorkLoop.js:2283-2288
// =============================================================================

/**
 * 标记跳过的更新 lanes
 * 用于在处理更新队列时记录被跳过的低优先级更新
 */
inline void markSkippedUpdateLanes(Lanes lanes) {
    auto& workLoop = getWorkLoop();
    workLoop.getState().workInProgressRootSkippedLanes = mergeLanes(
        workLoop.getState().workInProgressRootSkippedLanes,
        lanes
    );
}

/**
 * 获取 work-in-progress root 的渲染 lanes
 * @source ReactFiberWorkLoop.js:2280-2282
 */
inline Lanes getWorkInProgressRootRenderLanes() {
    return getWorkLoop().getState().workInProgressRootRenderLanes;
}

/**
 * 检查是否为不安全的类渲染阶段更新
 * @source ReactFiberWorkLoop.js:2275-2278
 */
inline bool isUnsafeClassRenderPhaseUpdate(FiberRef fiber) {
    // 简化实现：检查是否在渲染上下文中
    auto& workLoop = getWorkLoop();
    return hasContext(workLoop.getExecutionContext(), ExecutionContext::RenderContext);
}

} // namespace react::reconciler
