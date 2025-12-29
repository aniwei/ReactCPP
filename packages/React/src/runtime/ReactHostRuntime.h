/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 * 
 * ReactHostRuntime - 统一宿主运行时抽象
 * 
 * 本文件定义了 React 核心与宿主环境交互的抽象层，包括：
 * - SchedulerHost: 调度器宿主接口
 * - HostConfig: 渲染目标宿主配置接口
 * - ReactHostRuntime: 统一运行时封装
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <exception>

#include "ReactDOMInstance.h"

namespace react {

class ReactSharedInternals;

namespace reconciler {
class ClassUpdateQueueGlobals;
class ReactFiberHooks;
class ReactFiberNewContext;
class ReactChildFiberReconciler;
class ReactFiberWorkLoop;
class WorkLoopState;
struct Fiber;
} // namespace reconciler


// SchedulerHost - 调度器宿主抽象
// SchedulerHost 提供 Scheduler 所需的时间和调度原语
// 由具体宿主环境（浏览器、Native、Wasm）实现
class SchedulerHost {
public:
  virtual ~SchedulerHost() = default;

  // 获取当前时间（毫秒）
  // 对应 JS 的 performance.now()
  virtual double getCurrentTime() = 0;
  
  // 获取时间起点
  // 对应 JS 的 performance.timeOrigin
  virtual double getTimeOrigin() = 0;
  
  // 请求宿主回调
  virtual void requestHostCallback(std::function<bool(double)> callback) = 0;
  
  // 取消宿主回调
  virtual void cancelHostCallback() = 0;
  
  // 请求宿主超时
  // 对应 JS 的 setTimeout
  virtual void requestHostTimeout(std::function<void()> callback, double ms) = 0;
  
  // 取消宿主超时
  virtual void cancelHostTimeout() = 0;
  
  // 让出控制权
  // 判断是否应该让出控制权给宿主
  // 用于时间分片
  virtual bool shouldYieldToHost() = 0;
  
  
  // 请求绘制
  // 提示宿主可以进行绘制
  virtual void requestPaint() = 0;
  
  // 能力检测
  // 是否支持 MessageChannel
  virtual bool supportsMessageChannel();
  
  // 是否支持 isInputPending API
  virtual bool supportsIsInputPending();
  
  // 检查是否有输入挂起
  // 仅在 supportsIsInputPending() 返回 true 时有效
  virtual bool isInputPending();
};


// HostConfig - 宿主配置抽象
class HostConfig {
public:
  virtual ~HostConfig() = default;
  
  using Instance = ReactDOMInstance*;
  using TextInstance = ReactDOMTextInstance*;
  using Container = ReactDOMContainer*;
  using ChildSet = void*;
  using SuspenseInstance = ReactDOMSuspenseInstance*;
  using HydratableInstance = ReactDOMInstance*;
  using PublicInstance = ReactDOMInstance*;
  
  // 创建实例
  virtual Instance createInstance(
    const std::string& type,
    const facebook::jsi::Object& props,
    Container rootContainer,
    facebook::jsi::Runtime& runtime) = 0;
  
  // 创建文本实例
  virtual TextInstance createTextInstance(
    const std::string& text,
    Container rootContainer,
    facebook::jsi::Runtime& runtime) = 0;
  
  // 添加初始子节点（创建阶段）
  virtual void appendInitialChild(Instance parent, Instance child) = 0;
  
  // 添加子节点
  virtual void appendChild(Instance parent, Instance child) = 0;
  
  // 移除子节点
  virtual void removeChild(Instance parent, Instance child) = 0;
  
  // 在指定节点前插入子节点
  virtual void insertBefore(Instance parent, Instance child, Instance beforeChild) = 0;
  
  // 容器操作  
  // 将子节点添加到容器
  virtual void appendChildToContainer(Container container, Instance child) = 0;
  
  // 从容器移除子节点
  virtual void removeChildFromContainer(Container container, Instance child) = 0;
  
  // 在容器中的指定节点前插入子节点
  virtual void insertInContainerBefore(
    Container container, 
    Instance child, 
    Instance beforeChild) = 0;
  
  // 准备更新（计算 diff）
  virtual facebook::jsi::Value prepareUpdate(
    Instance instance,
    const std::string& type,
    const facebook::jsi::Object& oldProps,
    const facebook::jsi::Object& newProps,
    facebook::jsi::Runtime& runtime) = 0;
  
  // 提交更新
  virtual void commitUpdate(
    Instance instance,
    const facebook::jsi::Object& updatePayload,
    const std::string& type,
    const facebook::jsi::Object& oldProps,
    const facebook::jsi::Object& newProps,
    facebook::jsi::Runtime& runtime) = 0;
  
  // 提交文本更新
  virtual void commitTextUpdate(
    TextInstance textInstance,
    const std::string& oldText,
    const std::string& newText) = 0;
  
  // 完成初始子节点的创建
  virtual bool finalizeInitialChildren(
    Instance instance,
    const std::string& type,
    const facebook::jsi::Object& props,
    facebook::jsi::Runtime& runtime);
  
  // 准备提交
  virtual void prepareForCommit(Container container);
  
  // 重置提交后状态
  virtual void resetAfterCommit(Container container);
  
  // 获取公共实例
  virtual PublicInstance getPublicInstance(Instance instance);
  
  // 是否支持 Mutation 模式
  virtual bool supportsMutation();
  
  // 是否支持 Persistence 模式
  virtual bool supportsPersistence();
  
  // 是否支持 Hydration
  virtual bool supportsHydration();
  
  // 是否支持微任务
  virtual bool supportsMicrotasks();
  
  // 调度微任务
  virtual void scheduleMicrotask(std::function<void()> task);
  
  // 超时配置
  // 获取无超时值
  virtual int getNoTimeout();
  
  // 是否是无超时值
  virtual bool isNoTimeout(int timeout);
};


// ReactHostRuntime - 统一宿主运行时
class ReactHostRuntime {
public:
  using CaptureCommitPhaseErrorFn = std::function<void(
    std::shared_ptr<reconciler::Fiber> fiber,
    std::shared_ptr<reconciler::Fiber> nearestMountedAncestor,
    std::exception_ptr error
  )>;

  ReactHostRuntime(
    facebook::jsi::Runtime& jsiRuntime,
    std::unique_ptr<HostConfig> hostConfig,
    std::unique_ptr<SchedulerHost> schedulerHost);
  
  ~ReactHostRuntime();
  
  // 禁止拷贝
  ReactHostRuntime(const ReactHostRuntime&) = delete;
  ReactHostRuntime& operator=(const ReactHostRuntime&) = delete;
  
  // 允许移动
  ReactHostRuntime(ReactHostRuntime&&) = default;
  ReactHostRuntime& operator=(ReactHostRuntime&&) = delete;

  facebook::jsi::Runtime& getJSIRuntime();
  const facebook::jsi::Runtime& getJSIRuntime() const;

  reconciler::WorkLoopState& getWorkLoopState();
  const reconciler::WorkLoopState& getWorkLoopState() const;

  void setWorkLoop(std::shared_ptr<reconciler::ReactFiberWorkLoop> workLoop);
  reconciler::ReactFiberWorkLoop& getWorkLoop();
  const reconciler::ReactFiberWorkLoop& getWorkLoop() const;
  
  HostConfig& getHostConfig();
  const HostConfig& getHostConfig() const;
  
  SchedulerHost& getSchedulerHost();
  const SchedulerHost& getSchedulerHost() const;
  
  // 获取当前时间
  double now();
  
  // 判断是否应该让出
  bool shouldYield();

  reconciler::ClassUpdateQueueGlobals& getClassUpdateQueueGlobals();
  const reconciler::ClassUpdateQueueGlobals& getClassUpdateQueueGlobals() const;

  void setFiberHooks(std::shared_ptr<reconciler::ReactFiberHooks> hooks);
  reconciler::ReactFiberHooks& getFiberHooks();
  const reconciler::ReactFiberHooks& getFiberHooks() const;

  reconciler::ReactFiberNewContext& getFiberNewContext();
  const reconciler::ReactFiberNewContext& getFiberNewContext() const;

  reconciler::ReactChildFiberReconciler& getReconcileChildFibers();
  const reconciler::ReactChildFiberReconciler& getReconcileChildFibers() const;
  reconciler::ReactChildFiberReconciler& getMountChildFibers();
  const reconciler::ReactChildFiberReconciler& getMountChildFibers() const;

  ReactSharedInternals& getSharedInternals();
  const ReactSharedInternals& getSharedInternals() const;

  void setCaptureCommitPhaseError(CaptureCommitPhaseErrorFn fn);
  void clearCaptureCommitPhaseError();
  const CaptureCommitPhaseErrorFn* getCaptureCommitPhaseErrorPtr() const;
  
private:
  facebook::jsi::Runtime& jsiRuntime_;
  std::unique_ptr<HostConfig> hostConfig_;
  std::unique_ptr<SchedulerHost> schedulerHost_;

  std::unique_ptr<reconciler::WorkLoopState> workLoopState_;
  std::shared_ptr<reconciler::ReactFiberWorkLoop> workLoop_;

  std::unique_ptr<reconciler::ClassUpdateQueueGlobals> classUpdateQueueGlobals_;
  std::shared_ptr<reconciler::ReactFiberHooks> fiberHooks_;

  std::unique_ptr<reconciler::ReactFiberNewContext> fiberNewContext_;
  std::unique_ptr<reconciler::ReactChildFiberReconciler> reconcileChildFibers_;
  std::unique_ptr<reconciler::ReactChildFiberReconciler> mountChildFibers_;

  std::unique_ptr<ReactSharedInternals> sharedInternals_;

  CaptureCommitPhaseErrorFn captureCommitPhaseError_;
};

} // namespace react
