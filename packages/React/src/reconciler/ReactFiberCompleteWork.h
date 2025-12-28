/**
 * React Fiber Complete Work
 * 
 * completeWork 是渲染阶段的收尾工作
 * 负责完成 Fiber 节点的处理，准备提交阶段需要的数据
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberCompleteWork.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>
#include <vector>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"

#include "../runtime/ReactDOMInstance.h"

namespace react::reconciler {

using HostInstance = ::react::ReactDOMInstance*;
using HostTextInstance = ::react::ReactDOMTextInstance*;
using HostSuspenseInstance = ::react::ReactDOMSuspenseInstance*;
using HostContainer = ::react::ReactDOMContainer*;

// 前向声明
class ReactFiberWorkLoop;

// =============================================================================
// CompleteWork 结果类型
// =============================================================================

/**
 * completeWork 返回值
 * nullptr 表示没有新工作产生，否则返回新产生的 Fiber
 */
using CompleteWorkResult = FiberRef;

// =============================================================================
// HostContext 接口
// @source reactjs/packages/react-reconciler/src/ReactFiberHostContext.js
// =============================================================================

struct HostContext {
    std::any rootInstance;
    std::any hostContext;
    std::any namespace_;
};

// =============================================================================
// HostConfig 接口扩展 (completeWork 使用的部分)
// @source reactjs/packages/react-reconciler/src/ReactFiberConfig.js
// =============================================================================

struct CompleteHostConfig {
    // =========================================================================
    // Mutation 模式方法
    // =========================================================================
    
    /**
     * 创建实例
     * @source createInstance
     */
    std::function<HostInstance(
        std::string type,
        std::any props,
        std::any rootContainerInstance,
        std::any hostContext,
        FiberRef internalInstanceHandle
    )> createInstance;
    
    /**
     * 创建文本实例
     * @source createTextInstance
     */
    std::function<HostTextInstance(
        std::string text,
        std::any rootContainerInstance,
        std::any hostContext,
        FiberRef internalInstanceHandle
    )> createTextInstance;
    
    /**
     * 追加初始子节点
     * @source appendInitialChild
     */
    std::function<void(HostInstance parentInstance, HostInstance child)> appendInitialChild;
    
    /**
     * 完成初始子节点配置
     * @source finalizeInitialChildren
     */
    std::function<bool(
        HostInstance instance,
        std::string type,
        std::any props,
        std::any hostContext
    )> finalizeInitialChildren;
    
    /**
     * 准备更新
     * @source prepareUpdate
     */
    std::function<std::any(
        HostInstance instance,
        std::string type,
        std::any oldProps,
        std::any newProps,
        std::any hostContext
    )> prepareUpdate;
    
    // =========================================================================
    // Persistence 模式方法
    // =========================================================================
    
    /**
     * 克隆实例
     * @source cloneInstance
     */
    std::function<HostInstance(
        HostInstance instance,
        std::any type,
        std::any oldProps,
        std::any newProps,
        FiberRef internalInstanceHandle,
        bool keepChildren
    )> cloneInstance;
    
    /**
     * 创建容器子集
     * @source createContainerChildSet
     */
    std::function<std::any(std::any container)> createContainerChildSet;
    
    /**
     * 追加子节点到容器子集
     * @source appendChildToContainerChildSet
     */
    std::function<void(std::any childSet, std::any child)> appendChildToContainerChildSet;
    
    /**
     * 完成容器子节点配置
     * @source finalizeContainerChildren
     */
    std::function<void(std::any container, std::any newChildren)> finalizeContainerChildren;
    
    // =========================================================================
    // 资源和 Singleton 方法
    // =========================================================================
    
    /**
     * 解析 Singleton 实例
     * @source resolveSingletonInstance
     */
    std::function<HostInstance(std::string type)> resolveSingletonInstance;
    
    /**
     * 准备 Portal 挂载
     * @source preparePortalMount
     */
    std::function<void(HostContainer containerInfo)> preparePortalMount;
    
    // =========================================================================
    // 能力标志
    // =========================================================================
    
    bool supportsMutation = true;
    bool supportsPersistence = false;
    bool supportsResources = false;
    bool supportsSingletons = false;
};

// =============================================================================
// ReactFiberCompleteWork 类
// @source reactjs/packages/react-reconciler/src/ReactFiberCompleteWork.js
// =============================================================================

class ReactFiberCompleteWork {
public:
    explicit ReactFiberCompleteWork(
        CompleteHostConfig hostConfig = {}
    );
    
    ~ReactFiberCompleteWork() = default;
    
    // =========================================================================
    // 核心 API
    // =========================================================================
    
    /**
     * completeWork 主入口
     * @source:1064-2058 completeWork
     * 
     * @param current 当前树的 Fiber 节点
     * @param workInProgress 工作中的 Fiber 节点
     * @param renderLanes 当前渲染的 Lanes
     * @return 新产生的 Fiber，或 nullptr
     */
    CompleteWorkResult completeWork(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    // =========================================================================
    // 按组件类型的完成处理
    // =========================================================================
    
    /**
     * 完成 Function Component
     * @source:1079-1090 (switch case)
     */
    CompleteWorkResult completeFunctionComponent(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Class Component
     * @source:1091-1102 (switch case)
     */
    CompleteWorkResult completeClassComponent(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Host Root
     * @source:1103-1180 (switch case)
     */
    CompleteWorkResult completeHostRoot(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 完成 Host Component
     * @source:1181-1350 (switch case)
     */
    CompleteWorkResult completeHostComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 完成 Host Text
     * @source:1351-1420 (switch case)
     */
    CompleteWorkResult completeHostText(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Suspense Component
     * @source:1421-1600 (switch case)
     */
    CompleteWorkResult completeSuspenseComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 完成 Host Portal
     * @source:1601-1650 (switch case)
     */
    CompleteWorkResult completeHostPortal(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Context Provider
     * @source:1651-1680 (switch case)
     */
    CompleteWorkResult completeContextProvider(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Context Consumer
     * @source:1681-1700 (switch case)
     */
    CompleteWorkResult completeContextConsumer(
        FiberRef current,
        FiberRef workInProgress
    );
    
    /**
     * 完成 Offscreen Component
     * @source:1701-1850 (switch case)
     */
    CompleteWorkResult completeOffscreenComponent(
        FiberRef current,
        FiberRef workInProgress,
        Lanes renderLanes
    );
    
    /**
     * 完成 Cache Component
     * @source:1851-1900 (switch case)
     */
    CompleteWorkResult completeCacheComponent(
        FiberRef current,
        FiberRef workInProgress
    );
    
    // =========================================================================
    // 工具方法
    // =========================================================================
    
    /**
     * 冒泡属性 (传递 flags 和 subtreeFlags 到父节点)
     * @source:215-290 bubbleProperties
     */
    void bubbleProperties(FiberRef completedWork);
    
    /**
     * 标记更新
     * @source:180-185 markUpdate
     */
    void markUpdate(FiberRef workInProgress);
    
    /**
     * 标记引用
     * @source:190-210 markRef
     */
    void markRef(FiberRef workInProgress);
    
    /**
     * 设置 HostConfig
     */
    void setHostConfig(CompleteHostConfig config) {
        hostConfig_ = std::move(config);
    }
    
    /**
     * 设置 Host Context
     */
    void setHostContext(HostContext context) {
        hostContext_ = std::move(context);
    }
    
    /**
     * 获取 Host Context
     */
    const HostContext& getHostContext() const {
        return hostContext_;
    }

private:
    // Host 配置
    CompleteHostConfig hostConfig_;
    
    // Host 上下文
    HostContext hostContext_;
    
    // =========================================================================
    // 内部辅助方法
    // =========================================================================
    
    /**
     * 追加所有子节点
     * @source:295-380 appendAllChildren
     */
    void appendAllChildren(
        std::any parent,
        FiberRef workInProgress,
        bool needsVisibilityToggle,
        bool isHidden
    );
    
    /**
     * 更新 Host Container
     * @source:385-420 updateHostContainer
     */
    void updateHostContainer(FiberRef current, FiberRef workInProgress);
    
    /**
     * 更新 Host Component (Mutation 模式)
     * @source:425-520 updateHostComponent (Mutation)
     */
    void updateHostComponentMutation(
        FiberRef current,
        FiberRef workInProgress,
        std::any type,
        std::any newProps,
        std::any oldProps
    );
    
    /**
     * 更新 Host Component (Persistence 模式)
     * @source:525-620 updateHostComponent (Persistence)
     */
    void updateHostComponentPersistence(
        FiberRef current,
        FiberRef workInProgress,
        std::any type,
        std::any newProps,
        std::any oldProps
    );
    
    /**
     * 更新 Host Text (Mutation 模式)
     * @source:625-680 updateHostText (Mutation)
     */
    void updateHostTextMutation(
        FiberRef current,
        FiberRef workInProgress,
        std::string oldText,
        std::string newText
    );
    
    /**
     * 更新 Host Text (Persistence 模式)
     * @source:685-740 updateHostText (Persistence)
     */
    void updateHostTextPersistence(
        FiberRef current,
        FiberRef workInProgress,
        std::string oldText,
        std::string newText
    );
    
    /**
     * 预加载实例并挂起如果需要
     * @source:745-800 preloadInstanceAndSuspendIfNeeded
     */
    void preloadInstanceAndSuspendIfNeeded(
        FiberRef workInProgress,
        std::any type,
        std::any props
    );
    
    /**
     * 获取根 Host 容器
     */
    std::any getRootHostContainer();
    
    /**
     * 弹出 Host Context
     */
    void popHostContext(FiberRef fiber);
    
    /**
     * 获取 Host Context
     */
    std::any getContextForHost(FiberRef fiber);
};

// =============================================================================
// Suspense 相关辅助函数
// =============================================================================

/**
 * 查找第一个挂起的组件
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseComponent.js
 */
FiberRef findFirstSuspended(FiberRef row);

/**
 * 检查 Suspense 边界是否隐藏
 */
bool isSuspenseBoundaryBeingHidden(FiberRef current, FiberRef finishedWork);

// =============================================================================
// Hydration 相关辅助函数
// =============================================================================

/**
 * 弹出 Hydration 状态
 * @source reactjs/packages/react-reconciler/src/ReactFiberHydrationContext.js
 */
bool popHydrationState(FiberRef fiber);

/**
 * 完成 Hydrated 子节点
 */
void finalizeHydratedChildren(HostInstance instance, std::any type, std::any props);

/**
 * 发出待处理的 Hydration 警告
 */
void emitPendingHydrationWarnings();

} // namespace react::reconciler
