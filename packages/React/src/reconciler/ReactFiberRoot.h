/**
 * React Fiber Root
 * 
 * FiberRoot 是 React 应用的根节点
 * 包含了调度、更新队列等全局状态
 * 
 * @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:209-297
 * @source reactjs/packages/react-reconciler/src/ReactFiberRoot.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <any>
#include <set>
#include <map>

#include "ReactFiber.h"
#include "ReactRootTags.h"
#include "ReactFiberLane.h"

namespace facebook::jsi {
class Runtime;
} // namespace facebook::jsi

namespace react::reconciler {

using namespace facebook;

// 前向声明
struct Fiber;
struct FiberRoot;

// =============================================================================
// Timeout 类型
// =============================================================================

using TimeoutHandle = int64_t;
constexpr TimeoutHandle NoTimeout = -1;

// =============================================================================
// Callback 类型
// =============================================================================

using SchedulerCallback = std::function<void()>;
using CancelPendingCommit = std::function<void()>;

// =============================================================================
// Error Handler 类型
// =============================================================================

struct ErrorInfo {
    std::optional<std::string> componentStack;
};

using OnUncaughtError = std::function<void(std::any error, ErrorInfo info)>;
using OnCaughtError = std::function<void(std::any error, ErrorInfo info)>;
using OnRecoverableError = std::function<void(std::any error, ErrorInfo info)>;

// =============================================================================
// FiberRoot 结构体
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:209-297
// =============================================================================

/**
 * FiberRoot - React 应用的根节点
 */
struct FiberRoot : std::enable_shared_from_this<FiberRoot> {
    // =========================================================================
    // 基本属性
    // @source:211-213
    // =========================================================================
    
    // @source:212 The type of root (legacy, concurrent, etc.)
    RootTag tag = LegacyRoot;
    
    // @source:215 Any additional information from the host associated with this root
    std::any containerInfo;
    
    // @source:217 Used only by persistent updates
    std::any pendingChildren;
    
    // @source:219 The currently active root fiber. This is the mutable root of the tree.
    FiberRef current;
    
    // =========================================================================
    // 调度相关
    // @source:221-228
    // =========================================================================
    
    // @source:221 Ping cache for Suspense
    std::map<std::any, std::set<std::any>> pingCache;
    
    // @source:224 Timeout handle returned by setTimeout
    TimeoutHandle timeoutHandle = NoTimeout;
    
    // @source:228 When a root has a pending commit scheduled
    CancelPendingCommit cancelPendingCommit = nullptr;
    
    // @source:230 Top context object
    std::any context;
    std::any pendingContext;
    
    // =========================================================================
    // 链表和调度
    // @source:235-240
    // =========================================================================
    
    // @source:236 Used to create a linked list of roots with pending work
    std::weak_ptr<FiberRoot> next;
    
    // @source:240 Node returned by Scheduler.scheduleCallback
    std::any callbackNode;
    
    // @source:241
    Lane callbackPriority = NoLane;
    
    // =========================================================================
    // Lane 状态
    // @source:242-254
    // =========================================================================
    
    // @source:242
    LaneMap<double> expirationTimes;
    
    // @source:243
    LaneMap<std::vector<std::any>> hiddenUpdates;
    
    // @source:245
    Lanes pendingLanes = NoLanes;
    
    // @source:246
    Lanes suspendedLanes = NoLanes;
    
    // @source:247
    Lanes pingedLanes = NoLanes;
    
    // @source:248
    Lanes warmLanes = NoLanes;
    
    // @source:249
    Lanes expiredLanes = NoLanes;
    
    // @source:250
    Lanes indicatorLanes = NoLanes;
    
    // @source:251
    Lanes errorRecoveryDisabledLanes = NoLanes;
    
    // @source:252
    int shellSuspendCounter = 0;
    
    // @source:254
    Lanes entangledLanes = NoLanes;
    
    // @source:255
    LaneMap<Lanes> entanglements;
    
    // =========================================================================
    // Cache 相关
    // @source:257-258
    // =========================================================================
    
    // @source:257
    std::any pooledCache;
    
    // @source:258
    Lanes pooledCacheLanes = NoLanes;
    
    // =========================================================================
    // 标识符
    // @source:266
    // =========================================================================
    
    std::string identifierPrefix;
    
    // =========================================================================
    // 错误处理回调
    // @source:268-280
    // =========================================================================
    
    OnUncaughtError onUncaughtError = nullptr;
    OnCaughtError onCaughtError = nullptr;
    OnRecoverableError onRecoverableError = nullptr;
    
    // =========================================================================
    // Transition 相关
    // @source:282-290
    // =========================================================================
    
    std::function<void()> onDefaultTransitionIndicator = nullptr;
    std::function<void()> pendingIndicator = nullptr;
    
    std::any formState;
    std::any transitionTypes;
    
    // =========================================================================
    // Profiler 字段
    // @source:301-303
    // =========================================================================
    
    double effectDuration = 0.0;
    double passiveEffectDuration = 0.0;
    
    // =========================================================================
    // DevTools 字段
    // @source:300
    // =========================================================================
    
    std::set<FiberRef> memoizedUpdaters;
    LaneMap<std::set<FiberRef>> pendingUpdatersLaneMap;
    
    // =========================================================================
    // 构造函数
    // =========================================================================
    
    FiberRoot() {
      // 初始化 LaneMap
      expirationTimes.fill(-1.0);
      entanglements.fill(NoLanes);
    }
    
    // =========================================================================
    // 辅助方法
    // =========================================================================
    
    /**
     * 获取下一个根节点
     */
    std::shared_ptr<FiberRoot> getNext() const {
        return next.lock();
    }
    
    /**
     * 设置下一个根节点
     */
    void setNext(std::shared_ptr<FiberRoot> nextRoot) {
        next = nextRoot;
    }
    
    /**
     * 标记根节点有待处理的更新
     */
    void markRootUpdated(Lane updateLane, double eventTime) {
        pendingLanes |= updateLane;
        
        // 设置过期时间
        int index = laneToIndex(updateLane);
        if (index >= 0 && index < TotalLanes) {
            expirationTimes[index] = eventTime;
        }
    }
    
    /**
     * 标记根节点挂起
     */
    void markRootSuspended(Lanes suspendedLanes_) {
        suspendedLanes |= suspendedLanes_;
        pingedLanes &= ~suspendedLanes_;
    }
    
    /**
     * 标记根节点被 ping
     */
    void markRootPinged(Lanes pingedLanes_) {
        pingedLanes |= pendingLanes & pingedLanes_;
    }
    
    /**
     * 标记根节点完成
     */
    void markRootFinished(Lanes finishedLanes) {
        Lanes remainingLanes = pendingLanes & ~finishedLanes;
        
        pendingLanes = remainingLanes;
        suspendedLanes = NoLanes;
        pingedLanes = NoLanes;
        expiredLanes &= remainingLanes;
        warmLanes &= remainingLanes;
        
        // 清理完成的 lanes 的过期时间
        Lanes lanes = finishedLanes;
        while (lanes > 0) {
            int index = laneToIndex(getHighestPriorityLane(lanes));
            if (index >= 0 && index < TotalLanes) {
                expirationTimes[index] = -1.0;
                entanglements[index] = NoLanes;
            }
            lanes &= ~(1 << index);
        }
    }
    
    /**
     * 检查是否有待处理的工作
     */
    bool hasPendingWork() const {
        return pendingLanes != NoLanes;
    }
    
    /**
     * 获取下一个要处理的 lanes
     */
    Lanes getNextLanesToWork() const {
        return getNextLanes(pendingLanes, suspendedLanes);
    }
};

// =============================================================================
// FiberRoot 创建函数
// @source reactjs/packages/react-reconciler/src/ReactFiberRoot.js
// =============================================================================

/**
 * 创建 FiberRoot
 */
inline FiberRootRef createFiberRoot(
    const std::any& containerInfo,
    RootTag tag,
    bool isStrictMode,
    const std::string& identifierPrefix,
    OnUncaughtError onUncaughtError,
    OnCaughtError onCaughtError,
    OnRecoverableError onRecoverableError
) {
    auto root = std::make_shared<FiberRoot>();
    
    root->tag = tag;
    root->containerInfo = containerInfo;
    root->identifierPrefix = identifierPrefix;
    root->onUncaughtError = onUncaughtError;
    root->onCaughtError = onCaughtError;
    root->onRecoverableError = onRecoverableError;
    
    // 创建根 Fiber
    FiberRef uninitializedFiber = createHostRootFiber(tag, isStrictMode, false);
    root->current = uninitializedFiber;
    uninitializedFiber->stateNode = root;
    
    return root;
}

} // namespace react::reconciler
