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

#include "../runtime/ReactContainerInfo.h"

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

using TimeoutHandle = int64_t;
constexpr TimeoutHandle NoTimeout = -1;

using SchedulerCallback = std::function<void()>;
using CancelPendingCommit = std::function<void()>;

struct ErrorInfo {
  std::optional<std::string> componentStack;
};

using OnUncaughtError = std::function<void(std::any error, ErrorInfo info)>;
using OnCaughtError = std::function<void(std::any error, ErrorInfo info)>;
using OnRecoverableError = std::function<void(std::any error, ErrorInfo info)>;

// FiberRoot 结构体
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:209-297
struct FiberRoot : std::enable_shared_from_this<FiberRoot> {
  RootTag tag = LegacyRoot;

  ContainerInfo containerInfo;
  std::any pendingChildren;
    
  FiberRef current;
    
  // 调度相关  
  std::map<std::any, std::set<std::any>> pingCache;
  TimeoutHandle timeoutHandle = NoTimeout;

  CancelPendingCommit cancelPendingCommit = nullptr;
  std::any context;
  std::any pendingContext;
  
  
  // 链表和调度
  std::weak_ptr<FiberRoot> next;
  
  std::any callbackNode;
  Lane callbackPriority = NoLane;

  // Lane 状态
  LaneMap<double> expirationTimes;
  LaneMap<std::vector<std::any>> hiddenUpdates;
  Lanes pendingLanes = NoLanes;
  Lanes suspendedLanes = NoLanes;
  Lanes pingedLanes = NoLanes;
  Lanes warmLanes = NoLanes;
  Lanes expiredLanes = NoLanes;
  Lanes indicatorLanes = NoLanes;
  Lanes errorRecoveryDisabledLanes = NoLanes;
  int shellSuspendCounter = 0;
  Lanes entangledLanes = NoLanes;
  LaneMap<Lanes> entanglements;
  
  
  // Cache 相关
  std::any pooledCache;
  Lanes pooledCacheLanes = NoLanes;
  
  std::string identifierPrefix;
  
  // 错误处理回调
  OnUncaughtError onUncaughtError = nullptr;
  OnCaughtError onCaughtError = nullptr;
  OnRecoverableError onRecoverableError = nullptr;
  
  
  // Transition 相关
  std::function<void()> onDefaultTransitionIndicator = nullptr;
  std::function<void()> pendingIndicator = nullptr;
  
  std::any formState;
  std::any transitionTypes;
  
  
  // Profiler 字段  
  double effectDuration = 0.0;
  double passiveEffectDuration = 0.0;
  
  // DevTools 字段
  std::set<FiberRef> memoizedUpdaters;
  LaneMap<std::set<FiberRef>> pendingUpdatersLaneMap;
  
  FiberRoot();
  
  // 辅助方法
  // 获取下一个根节点
  std::shared_ptr<FiberRoot> getNext() const;
  
  // 设置下一个根节点
  void setNext(std::shared_ptr<FiberRoot> nextRoot);
  
  // 标记根节点有待处理的更新
  void markRootUpdated(Lane updateLane, double eventTime);
  
  // 标记根节点挂起
  void markRootSuspended(Lanes suspendedLanes_);
  
  // 标记根节点被 ping
  void markRootPinged(Lanes pingedLanes_);
  
  // 标记根节点完成
  void markRootFinished(Lanes finishedLanes);
  
  // 检查是否有待处理的工作
  bool hasPendingWork() const;
  
  // 获取下一个要处理的 lanes
  Lanes getNextLanesToWork() const;
};

 // 创建 FiberRoot
FiberRootRef createFiberRoot(
  const ContainerInfo& containerInfo,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError);

FiberRootRef createFiberRoot(
  ::react::ReactDOMContainer* container,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError);

FiberRootRef createFiberRoot(
  const std::string& debugName,
  RootTag tag,
  bool isStrictMode,
  const std::string& identifierPrefix,
  OnUncaughtError onUncaughtError,
  OnCaughtError onCaughtError,
  OnRecoverableError onRecoverableError);

} // namespace react::reconciler
