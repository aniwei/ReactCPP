/**
 * React Fiber Commit Work
 * 
 * commitWork 负责将 Fiber 树的变更应用到实际的 Host 环境
 * 包括 DOM 操作、生命周期调用等
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberCommitWork.js
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
#include "ReactHookEffectTags.h"
#include "ReactFiberCommitEffects.h"

#include "../runtime/ReactDOMInstance.h"

namespace react::reconciler {

using HostInstance = ::react::ReactDOMInstance*;
using HostTextInstance = ::react::ReactDOMTextInstance*;
using HostSuspenseInstance = ::react::ReactDOMSuspenseInstance*;
using HostContainer = ::react::ReactDOMContainer*;

// 前向声明
class ReactFiberWorkLoop;

// =============================================================================
// Commit 阶段类型定义
// @source ReactFiberCommitWork.js
// =============================================================================

/**
 * Commit 阶段枚举
 */
enum class CommitPhase : uint8_t {
  BeforeMutation = 0,  // DOM 变更前
  Mutation = 1,        // DOM 变更
  LayoutPhase = 2,     // DOM 变更后 (布局效果)
  PassivePhase = 3     // 被动效果 (useEffect)
};

// =============================================================================
// Update Queue (用于 Class 组件)
// @source reactjs/packages/react-reconciler/src/ReactFiberClassUpdateQueue.js
// =============================================================================

struct ClassUpdateQueue {
  std::any baseState;
  std::shared_ptr<void> firstBaseUpdate;
  std::shared_ptr<void> lastBaseUpdate;
  std::shared_ptr<void> shared;
  std::vector<std::function<void()>> callbacks;
};

// =============================================================================
// HostConfig 接口 (Commit 阶段使用)
// @source reactjs/packages/react-reconciler/src/ReactFiberConfig.js
// =============================================================================

struct CommitHostConfig {
    // =========================================================================
    // Mutation 操作
    // =========================================================================
    
    /**
     * 追加子节点
     */
    std::function<void(HostInstance parentInstance, HostInstance child)> appendChild;
    
    /**
     * 追加子节点到容器
     */
    std::function<void(HostContainer container, HostInstance child)> appendChildToContainer;
    
    /**
     * 在之前插入
     */
    std::function<void(
        HostInstance parentInstance,
        HostInstance child,
        HostInstance beforeChild
    )> insertBefore;
    
    /**
     * 在容器中插入
     */
    std::function<void(
        HostContainer container,
        HostInstance child,
        HostInstance beforeChild
    )> insertInContainerBefore;
    
    /**
     * 移除子节点
     */
    std::function<void(HostInstance parentInstance, HostInstance child)> removeChild;
    
    /**
     * 从容器移除
     */
    std::function<void(HostContainer container, HostInstance child)> removeChildFromContainer;
    
    /**
     * 提交更新
     */
    std::function<void(
        HostInstance instance,
        std::any updatePayload,
        std::string type,
        std::any oldProps,
        std::any newProps,
        FiberRef internalInstanceHandle
    )> commitUpdate;
    
    /**
     * 提交文本更新
     */
    std::function<void(
        HostTextInstance textInstance,
        std::string oldText,
        std::string newText
    )> commitTextUpdate;
    
    /**
     * 重置文本内容
     */
    std::function<void(HostInstance instance)> resetTextContent;
    
    /**
     * 隐藏实例
     */
    std::function<void(HostInstance instance)> hideInstance;
    
    /**
     * 隐藏文本实例
     */
    std::function<void(HostTextInstance textInstance)> hideTextInstance;
    
    /**
     * 取消隐藏实例
     */
    std::function<void(HostInstance instance, std::any props)> unhideInstance;
    
    /**
     * 取消隐藏文本实例
     */
    std::function<void(HostTextInstance textInstance, std::string text)> unhideTextInstance;
    
    /**
     * 清除容器
     */
    std::function<void(HostContainer container)> clearContainer;
    
    // =========================================================================
    // Persistence 操作
    // =========================================================================
    
    /**
     * 替换容器子节点
     */
    std::function<void(HostContainer container, std::any newChildren)> replaceContainerChildren;
    
    // =========================================================================
    // 重置操作
    // =========================================================================
    
    /**
     * 提交后重置
     */
    std::function<void(HostContainer containerInfo)> resetAfterCommit;
    
    /**
     * 准备提交
     */
    std::function<void(HostContainer containerInfo)> prepareForCommit;
    
    // =========================================================================
    // Focus 管理
    // =========================================================================
    
    /**
     * 在 blur 活动实例之后
     */
    std::function<void()> afterActiveInstanceBlur;
    
    /**
     * 在 focus 之前
     */
    std::function<void(FiberRef fiber)> beforeActiveInstanceBlur;
    
    // =========================================================================
    // 能力标志
    // =========================================================================
    
    bool supportsMutation = true;
    bool supportsPersistence = false;
};

// =============================================================================
// ReactFiberCommitWork 类
// @source reactjs/packages/react-reconciler/src/ReactFiberCommitWork.js
// =============================================================================

class ReactFiberCommitWork {
public:
    explicit ReactFiberCommitWork(
        CommitHostConfig hostConfig = {}
    );
    
    ~ReactFiberCommitWork() = default;
    
    // =========================================================================
    // 核心提交阶段 API
    // =========================================================================
    
    /**
     * 提交 BeforeMutation 效果
     * @source:1400-1550 commitBeforeMutationEffects
     */
    bool commitBeforeMutationEffects(
        FiberRootRef root,
        FiberRef finishedWork
    );
    
    /**
     * 提交 Mutation 效果
     * @source:1936-1950 commitMutationEffects
     */
    void commitMutationEffects(
        FiberRootRef root,
        FiberRef finishedWork,
        Lanes committedLanes
    );
    
    /**
     * 提交 Layout 效果
     * @source:2500-2600 commitLayoutEffects
     */
    void commitLayoutEffects(
        FiberRef finishedWork,
        FiberRootRef root,
        Lanes committedLanes
    );
    
    /**
     * 提交 Passive Mount 效果
     * @source:3200-3300 commitPassiveMountEffects
     */
    void commitPassiveMountEffects(
        FiberRootRef root,
        FiberRef finishedWork,
        Lanes committedLanes
    );
    
    /**
     * 提交 Passive Unmount 效果
     * @source:3100-3195 commitPassiveUnmountEffects
     */
    void commitPassiveUnmountEffects(FiberRef finishedWork);
    
    // =========================================================================
    // 单个 Fiber 的提交处理
    // =========================================================================
    
    /**
     * 在单个 Fiber 上提交 BeforeMutation 效果
     * @source:1555-1750 commitBeforeMutationEffectsOnFiber
     */
    void commitBeforeMutationEffectsOnFiber(FiberRef finishedWork);
    
    /**
     * 在单个 Fiber 上提交 Mutation 效果
     * @source:1984-2350 commitMutationEffectsOnFiber
     */
    void commitMutationEffectsOnFiber(
        FiberRef finishedWork,
        FiberRootRef root,
        Lanes lanes
    );
    
    /**
     * 在单个 Fiber 上提交 Layout 效果
     * @source:2605-2850 commitLayoutEffectsOnFiber
     */
    void commitLayoutEffectsOnFiber(
        FiberRef finishedWork,
        FiberRootRef root,
        Lanes committedLanes
    );
    
    // =========================================================================
    // 删除效果
    // =========================================================================
    
    /**
     * 提交删除效果
     * @source:2355-2495 commitDeletionEffects
     */
    void commitDeletionEffects(
        FiberRootRef root,
        FiberRef returnFiber,
        FiberRef deletedFiber
    );
    
    /**
     * 在单个 Fiber 上提交删除效果
     * @source:1055-1250 commitDeletionEffectsOnFiber
     */
    void commitDeletionEffectsOnFiber(
        FiberRootRef root,
        FiberRef returnFiber,
        FiberRef deletedFiber
    );
    
    // =========================================================================
    // Placement 效果
    // =========================================================================
    
    /**
     * 提交 Placement 效果
     * @source:850-950 commitPlacement
     */
    void commitPlacement(FiberRef finishedWork);
    
    /**
     * 提交协调效果 (Placement/Hydrating)
     * @source:750-845 commitReconciliationEffects
     */
    void commitReconciliationEffects(
        FiberRef finishedWork,
        Lanes lanes
    );
    
    // =========================================================================
    // Hook 效果
    // =========================================================================
    
    /**
     * 提交 Hook 效果列表 (挂载)
     * @source:600-680 commitHookEffectListMount
     */
    void commitHookEffectListMount(
        HookFlags flags,
        FiberRef finishedWork
    );
    
    /**
     * 提交 Hook 效果列表 (卸载)
     * @source:520-595 commitHookEffectListUnmount
     */
    void commitHookEffectListUnmount(
        HookFlags flags,
        FiberRef finishedWork,
        FiberRef nearestMountedAncestor
    );
    
    /**
     * 提交 Hook Layout 卸载效果
     * @source:685-745 commitHookLayoutUnmountEffects
     */
    void commitHookLayoutUnmountEffects(
        FiberRef finishedWork,
        FiberRef nearestMountedAncestor,
        HookFlags flags
    );
    
    /**
     * 提交 Hook Passive 卸载效果
     */
    void commitHookPassiveUnmountEffects(
        FiberRef finishedWork,
        FiberRef nearestMountedAncestor,
        HookFlags flags
    );
    
    // =========================================================================
    // Class 组件生命周期
    // =========================================================================
    
    /**
     * 提交 Class 组件 Layout 效果
     * @source:2855-2980 commitClassLayoutLifecycles
     */
    void commitClassLayoutLifecycles(
        FiberRef finishedWork,
        FiberRef current,
        Lanes committedLanes
    );
    
    /**
     * 安全调用 componentWillUnmount
     * @source:460-515 safelyCallComponentWillUnmount
     */
    void safelyCallComponentWillUnmount(
        FiberRef current,
        FiberRef nearestMountedAncestor,
        std::any instance
    );
    
    /**
     * 安全分离 Ref
     * @source:420-455 safelyDetachRef
     */
    void safelyDetachRef(FiberRef current, FiberRef nearestMountedAncestor);
    
    /**
     * 安全附加 Ref
     * @source:380-415 safelyAttachRef
     */
    void safelyAttachRef(FiberRef current, std::any instanceToUse);
    
    // =========================================================================
    // 可见性相关
    // =========================================================================
    
    /**
     * 消失布局效果
     * @source:2985-3095 disappearLayoutEffects
     */
    void disappearLayoutEffects(FiberRef finishedWork);
    
    /**
     * 重新出现布局效果
     * @source:3305-3400 reappearLayoutEffects
     */
    void reappearLayoutEffects(
        FiberRootRef finishedRoot,
        FiberRef current,
        FiberRef finishedWork,
        bool includeWorkInProgressEffects
    );
    
    /**
     * 断开被动效果
     * @source:3405-3500 disconnectPassiveEffect
     */
    void disconnectPassiveEffect(FiberRef finishedWork);
    
    /**
     * 重连被动效果
     * @source:3505-3600 reconnectPassiveEffects
     */
    void reconnectPassiveEffects(
        FiberRootRef finishedRoot,
        FiberRef finishedWork,
        Lanes committedLanes,
        bool includeWorkInProgressEffects
    );
    
    // =========================================================================
    // 工具方法
    // =========================================================================
    
    /**
     * 设置 HostConfig
     */
    void setHostConfig(CommitHostConfig config) {
        hostConfig_ = std::move(config);
    }
    
    /**
     * 获取 Host 父节点
     * @source:955-1050 getHostParentFiber
     */
    FiberRef getHostParentFiber(FiberRef fiber);
    
    /**
     * 获取 Host 兄弟节点
     * @source:1255-1350 getHostSibling
     */
    std::any getHostSibling(FiberRef fiber);
    
    /**
     * 判断是否需要应该触发 afterActiveInstanceBlur
     */
    bool shouldFireAfterActiveInstanceBlur() const {
        return shouldFireAfterActiveInstanceBlur_;
    }
    
    /**
     * 重置 afterActiveInstanceBlur 标志
     */
    void resetAfterActiveInstanceBlur() {
        shouldFireAfterActiveInstanceBlur_ = false;
    }

private:
    // Host 配置
    CommitHostConfig hostConfig_;
    
    // 内部状态
    Lanes inProgressLanes_ = NoLanes;
    FiberRootRef inProgressRoot_ = nullptr;
    
    // Offscreen 子树状态
    bool offscreenSubtreeIsHidden_ = false;
    bool offscreenSubtreeWasHidden_ = false;
    
    // blur 标志
    bool shouldFireAfterActiveInstanceBlur_ = false;
    
    // =========================================================================
    // 内部辅助方法
    // =========================================================================
    
    /**
     * 递归遍历 Mutation 效果
     * @source:1955-1980 recursivelyTraverseMutationEffects
     */
    void recursivelyTraverseMutationEffects(
        FiberRootRef root,
        FiberRef parentFiber,
        Lanes lanes
    );
    
    /**
     * 递归遍历 Layout 效果
     * @source:2600-2605 recursivelyTraverseLayoutEffects
     */
    void recursivelyTraverseLayoutEffects(
        FiberRootRef root,
        FiberRef parentFiber,
        Lanes lanes
    );
    
    /**
     * 递归遍历删除效果
     */
    void recursivelyTraverseDeletionEffects(
        FiberRootRef root,
        FiberRef returnFiber,
        FiberRef deletedFiber
    );
    
    /**
     * 递归遍历 Passive Mount 效果
     */
    void recursivelyTraversePassiveMountEffects(
        FiberRootRef root,
        FiberRef parentFiber,
        Lanes committedLanes
    );
    
    /**
     * 递归遍历 Passive Unmount 效果
     */
    void recursivelyTraversePassiveUnmountEffects(FiberRef parentFiber);
    
    /**
     * 插入或追加 Placement 节点
     * @source:970-1050 insertOrAppendPlacementNode
     */
    void insertOrAppendPlacementNode(
        FiberRef node,
        std::any before,
        std::any parent
    );
    
    /**
     * 插入或追加 Placement 节点到容器
     * @source:1055-1135 insertOrAppendPlacementNodeIntoContainer
     */
    void insertOrAppendPlacementNodeIntoContainer(
        FiberRef node,
        std::any before,
        std::any container
    );
    
    /**
     * 判断 Fiber 是否为 Host Parent
     */
    bool isHostParent(FiberRef fiber);
    
    /**
     * 提交工作
     */
    void commitWork(
        FiberRef current,
        FiberRef finishedWork
    );
    
    /**
     * 重置 Text 内容
     */
    void resetTextContent(FiberRef fiber);
    
    /**
     * 提交 HostComponent 挂载
     */
    void commitHostComponentMount(FiberRef finishedWork);
};

// =============================================================================
// 便捷函数 - DEV 模式下的效果调用
// =============================================================================

#ifdef __DEV__

/**
 * 在 DEV 模式下调用 Layout Effect Mount
 */
void invokeLayoutEffectMountInDEV(FiberRef fiber);

/**
 * 在 DEV 模式下调用 Passive Effect Mount
 */
void invokePassiveEffectMountInDEV(FiberRef fiber);

/**
 * 在 DEV 模式下调用 Layout Effect Unmount
 */
void invokeLayoutEffectUnmountInDEV(FiberRef fiber);

/**
 * 在 DEV 模式下调用 Passive Effect Unmount
 */
void invokePassiveEffectUnmountInDEV(FiberRef fiber);

#endif

// =============================================================================
// Suspense 提交相关
// =============================================================================

/**
 * 累积 Suspensey Commit
 * @source:3605-3700 accumulateSuspenseyCommit
 */
void accumulateSuspenseyCommit(FiberRef finishedWork);

} // namespace react::reconciler
