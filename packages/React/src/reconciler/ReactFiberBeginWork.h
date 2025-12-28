/**
 * React Fiber Begin Work
 * 
 * beginWork 是渲染阶段的入口
 * 负责根据 Fiber 类型启动相应的更新逻辑
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberBeginWork.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "../runtime/ReactDOMInstance.h"
#include "../react/ReactElement.h"

namespace react::reconciler {

// 前向声明
class ReactFiberWorkLoop;

// =============================================================================
// BeginWork 结果类型
// =============================================================================

/**
 * beginWork 返回值
 * nullptr 表示工作完成，否则返回下一个要处理的子 Fiber
 */
using BeginWorkResult = FiberRef;

// =============================================================================
// 更新状态标记
// @source:120-125 ReactFiberBeginWork.js
// =============================================================================

struct BeginWorkContext {
    // 是否收到更新
    bool didReceiveUpdate = false;
};

// =============================================================================
// 子节点协调器接口
// @source reactjs/packages/react-reconciler/src/ReactChildFiber.js
// =============================================================================

struct ChildReconciler {
    /**
     * 协调子节点
     * @source ReactChildFiber.js reconcileChildFibers
     */
    std::function<FiberRef(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        const jsi::Value& newChild,
        Lanes lanes
    )> reconcileChildFibers;
    
    /**
     * 挂载子节点
     * @source ReactChildFiber.js mountChildFibers
     */
    std::function<FiberRef(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        const jsi::Value& newChild,
        Lanes lanes
    )> mountChildFibers;
    
    /**
     * 克隆子节点
     */
    std::function<void(FiberRef current, FiberRef workInProgress)> cloneChildFibers;
};

// =============================================================================
// HostConfig 接口 (由 ReactHostRuntime 提供)
// @source reactjs/packages/react-reconciler/src/ReactFiberConfig.js
// =============================================================================

struct HostConfigInterface {
    // 是否支持 Hydration
    bool supportsHydration = false;
    
    // 是否支持 Mutation
    bool supportsMutation = true;
    
    // 是否支持 Persistence
    bool supportsPersistence = false;
    
    /**
     * 预加载实例
     */
    std::function<bool(::react::ReactDOMInstance* instance)> preloadInstance;
    
    /**
     * 获取 Hydration 状态
     */
    std::function<bool()> getIsHydrating;
};

// =============================================================================
// ReactFiberBeginWork 类
// @source reactjs/packages/react-reconciler/src/ReactFiberBeginWork.js
// =============================================================================

class ReactFiberBeginWork {
public:
    explicit ReactFiberBeginWork(
        ChildReconciler reconciler = {},
        HostConfigInterface hostConfig = {}
    );
    
    ~ReactFiberBeginWork() = default;
    
    // =========================================================================
    // 核心 API
    // =========================================================================
    
    /**
     * beginWork 主入口
     * @source:4080-4365 beginWork
     * 
     * @param current 当前树的 Fiber 节点 (首次渲染时为 null)
     * @param workInProgress 工作中的 Fiber 节点
     * @param renderLanes 当前渲染的 Lanes
     * @return 下一个要处理的 Fiber，或 nullptr 表示完成
     */
    BeginWorkResult beginWork(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 重放函数组件
     * @source:2850-2920 replayFunctionComponent
     */
    BeginWorkResult replayFunctionComponent(
      FiberRef current,
      FiberRef workInProgress,
      const jsi::Value& pendingProps,
      const jsi::Value& Component,
      const jsi::Value& context,
      Lanes renderLanes
    );
    
    // =========================================================================
    // 组件类型处理器
    // =========================================================================
    
    /**
     * 更新函数组件
     * @source:930-1040 updateFunctionComponent
     */
    BeginWorkResult updateFunctionComponent(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& nextProps,
        Lanes renderLanes
    );
    
    /**
     * 更新类组件
     * @source:1045-1180 updateClassComponent
     */
    BeginWorkResult updateClassComponent(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& nextProps,
        Lanes renderLanes
    );
    
    /**
     * 更新 Host Root
     * @source:1555-1680 updateHostRoot
     */
    BeginWorkResult updateHostRoot(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Host Component (原生元素)
     * @source:1685-1780 updateHostComponent
     */
    BeginWorkResult updateHostComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Host Text
     * @source:1785-1820 updateHostText
     */
    BeginWorkResult updateHostText(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 更新 Suspense 组件
     * @source:2180-2450 updateSuspenseComponent
     */
    BeginWorkResult updateSuspenseComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 挂载 Lazy 组件
     * @source:610-720 mountLazyComponent
     */
    BeginWorkResult mountLazyComponent(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& elementType,
        Lanes renderLanes
    );
    
    /**
     * 更新 Fragment
     * @source:1825-1870 updateFragment
     */
    BeginWorkResult updateFragment(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Portal
     * @source:1455-1550 updatePortalComponent
     */
    BeginWorkResult updatePortalComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Context Provider
     * @source:3420-3520 updateContextProvider
     */
    BeginWorkResult updateContextProvider(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Context Consumer
     * @source:3525-3620 updateContextConsumer
     */
    BeginWorkResult updateContextConsumer(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Forward Ref
     * @source:725-825 updateForwardRef
     */
    BeginWorkResult updateForwardRef(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& nextProps,
        Lanes renderLanes
    );
    
    /**
     * 更新 Memo 组件
     * @source:830-925 updateMemoComponent
     */
    BeginWorkResult updateMemoComponent(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& nextProps,
        Lanes renderLanes
    );
    
    /**
     * 更新 Simple Memo 组件
     * @source:3740-3850 updateSimpleMemoComponent
     */
    BeginWorkResult updateSimpleMemoComponent(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& nextProps,
        Lanes renderLanes
    );
    
    /**
     * 更新 Offscreen 组件
     * @source:2750-2845 updateOffscreenComponent
     */
    BeginWorkResult updateOffscreenComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Profiler
     * @source:3625-3735 updateProfiler
     */
    BeginWorkResult updateProfiler(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 更新 Mode
     */
    BeginWorkResult updateMode(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    // =========================================================================
    // Bailout 逻辑
    // =========================================================================
    
    /**
     * 尝试提前 bailout
     * @source:3855-4075 attemptEarlyBailoutIfNoScheduledUpdate
     */
    BeginWorkResult attemptEarlyBailoutIfNoScheduledUpdate(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * bailout 已完成的工作
     * @source:3750-3850 bailoutOnAlreadyFinishedWork
     */
    BeginWorkResult bailoutOnAlreadyFinishedWork(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 检查是否有计划的更新或 context 变化
     * @source:3650-3700 checkScheduledUpdateOrContext
     */
    bool checkScheduledUpdateOrContext(
        FiberRef current,
        Lanes renderLanes
    );
    
    // =========================================================================
    // 工具方法
    // =========================================================================
    
    /**
     * 推送 Host Root context
     */
    void pushHostRootContext(FiberRef workInProgress);
    
    /**
     * 推送 Host Container
     */
    void pushHostContainer(FiberRef fiber, std::any container);
    
    /**
     * 设置 didReceiveUpdate 标记
     */
    void markWorkInProgressReceivedUpdate() {
        context_.didReceiveUpdate = true;
    }
    
    /**
     * 获取 didReceiveUpdate 状态
     */
    bool didReceiveUpdate() const {
        return context_.didReceiveUpdate;
    }
    
    /**
     * 重置 context
     */
    void resetContext() {
        context_.didReceiveUpdate = false;
    }
    
    /**
     * 设置子节点协调器
     */
    void setReconciler(ChildReconciler reconciler) {
        reconciler_ = std::move(reconciler);
    }
    
    /**
     * 设置 HostConfig
     */
    void setHostConfig(HostConfigInterface config) {
        hostConfig_ = std::move(config);
    }

private:
    // 上下文状态
    BeginWorkContext context_;
    
    // 子节点协调器
    ChildReconciler reconciler_;
    
    // Host 配置
    HostConfigInterface hostConfig_;
    
    // =========================================================================
    // 内部辅助方法
    // =========================================================================
    
    /**
     * 协调子节点
     */
    void reconcileChildren(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& nextChildren,
        Lanes renderLanes
    );
    
    /**
     * 强制挂起 Suspense
     */
    void forceSuspenseFallback(FiberRef workInProgress);
    
    /**
     * 处理渲染中抛出的异常
     */
    void handleRenderThrownValue(
        FiberRef workInProgress,
        std::any thrownValue
    );
    
    /**
     * 渲染组件并获取 children
     */
    jsi::Value renderWithHooks(
        FiberRef current,
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& props,
        const jsi::Value& context,
        Lanes renderLanes
    );
    
    /**
     * 检查是否应该更新
     */
    bool shouldComponentUpdate(
        FiberRef workInProgress,
        const jsi::Value& Component,
        const jsi::Value& oldProps,
        const jsi::Value& newProps
    );
};

// =============================================================================
// 创建默认的 ChildReconciler
// @source reactjs/packages/react-reconciler/src/ReactChildFiber.js
// =============================================================================

/**
 * 创建子节点协调器
 * @param shouldTrackSideEffects 是否跟踪副作用
 */
ChildReconciler createChildReconciler(bool shouldTrackSideEffects);

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * 从类型和属性创建 Fiber
 * @source:290-380 createFiberFromTypeAndProps
 */
FiberRef createFiberFromTypeAndProps(
    const jsi::Value& type,
    const jsi::Value& key,
    const jsi::Value& pendingProps,
    FiberRef owner,
    TypeOfMode mode,
    Lanes lanes
);

/**
 * 从 Element 创建 Fiber
 * @source:405-500 createFiberFromElement
 */
FiberRef createFiberFromElement(
    const react::ReactElement& element,
    TypeOfMode mode,
    Lanes lanes
);

/**
 * 从 Fragment 创建 Fiber
 * @source:505-550 createFiberFromFragment
 */
FiberRef createFiberFromFragment(
    const jsi::Value& elements,
    TypeOfMode mode,
    Lanes lanes,
    const jsi::Value& key
);

/**
 * 从 Portal 创建 Fiber
 * @source:555-610 createFiberFromPortal
 */
FiberRef createFiberFromPortal(
    const react::ReactPortal& portal,
    TypeOfMode mode,
    Lanes lanes
);

/**
 * 从 Text 创建 Fiber
 */
FiberRef createFiberFromText(
    const std::string& content,
    TypeOfMode mode,
    Lanes lanes
);

} // namespace react::reconciler
