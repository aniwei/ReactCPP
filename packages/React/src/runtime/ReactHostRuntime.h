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

#include "ReactDOMInstance.h"

namespace react {

// =============================================================================
// SchedulerHost - 调度器宿主抽象
// =============================================================================

/**
 * SchedulerHost 提供 Scheduler 所需的时间和调度原语
 * 
 * 由具体宿主环境（浏览器、Native、Wasm）实现
 */
class SchedulerHost {
public:
    virtual ~SchedulerHost() = default;
    
    // =========================================================================
    // 时间相关
    // =========================================================================
    
    /**
     * 获取当前时间（毫秒）
     * 对应 JS 的 performance.now()
     */
    virtual double getCurrentTime() = 0;
    
    /**
     * 获取时间起点
     * 对应 JS 的 performance.timeOrigin
     */
    virtual double getTimeOrigin() = 0;
    
    // =========================================================================
    // 调度原语
    // =========================================================================
    
    /**
     * 请求宿主回调
     * 对应 JS 的 MessageChannel 或 setImmediate
     * 
     * @param callback 回调函数，参数为当前时间，返回是否还有更多工作
     */
    virtual void requestHostCallback(std::function<bool(double)> callback) = 0;
    
    /**
     * 取消宿主回调
     */
    virtual void cancelHostCallback() = 0;
    
    /**
     * 请求宿主超时
     * 对应 JS 的 setTimeout
     * 
     * @param callback 超时回调
     * @param ms 超时时间（毫秒）
     */
    virtual void requestHostTimeout(std::function<void()> callback, double ms) = 0;
    
    /**
     * 取消宿主超时
     */
    virtual void cancelHostTimeout() = 0;
    
    // =========================================================================
    // 让出控制权
    // =========================================================================
    
    /**
     * 判断是否应该让出控制权给宿主
     * 用于时间分片
     */
    virtual bool shouldYieldToHost() = 0;
    
    /**
     * 请求绘制
     * 提示宿主可以进行绘制
     */
    virtual void requestPaint() = 0;
    
    // =========================================================================
    // 能力检测
    // =========================================================================
    
    /**
     * 是否支持 MessageChannel
     */
    virtual bool supportsMessageChannel() { return true; }
    
    /**
     * 是否支持 isInputPending API
     */
    virtual bool supportsIsInputPending() { return false; }
    
    /**
     * 检查是否有输入挂起
     * 仅在 supportsIsInputPending() 返回 true 时有效
     */
    virtual bool isInputPending() { return false; }
};

// =============================================================================
// HostConfig - 宿主配置抽象
// =============================================================================

/**
 * HostConfig 定义 Reconciler 与具体渲染目标的交互接口
 * 
 * 不同的渲染目标（DOM、Native、Test）需要提供不同的实现
 */
class HostConfig {
public:
    virtual ~HostConfig() = default;
    
    // =========================================================================
    // 类型定义
    // =========================================================================
    
    // 实例类型（严格参考 reactjs HostConfig 类型别名）
    // - Instance/TextInstance/SuspenseInstance: 对应 Fiber.stateNode（Host* tag）
    // - Container: 对应 FiberRoot/Portal 的 containerInfo
    // - ChildSet/HydratableInstance/PublicInstance: 对应各 renderer 的宿主句柄
    //
    // 这里用接口指针替代 void*，提供最基本的类型约束；具体宿主实现仍可自行管理内存。
    using Instance = ReactDOMInstance*;
    using TextInstance = ReactDOMTextInstance*;
    using Container = ReactDOMContainer*;
    using ChildSet = void*;
    using SuspenseInstance = ReactDOMSuspenseInstance*;
    using HydratableInstance = ReactDOMInstance*;
    using PublicInstance = ReactDOMInstance*;
    
    // =========================================================================
    // 实例创建
    // =========================================================================
    
    /**
     * 创建实例
     * 
     * @param type 元素类型（如 "div", "span"）
     * @param props 属性对象
     * @param rootContainer 根容器
     * @param hostContext 宿主上下文
     * @param runtime JSI 运行时
     * @return 创建的实例
     */
    virtual Instance createInstance(
        const std::string& type,
        const facebook::jsi::Object& props,
        Container rootContainer,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    /**
     * 创建文本实例
     * 
     * @param text 文本内容
     * @param rootContainer 根容器
     * @param runtime JSI 运行时
     * @return 创建的文本实例
     */
    virtual TextInstance createTextInstance(
        const std::string& text,
        Container rootContainer,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    // =========================================================================
    // 子节点操作
    // =========================================================================
    
    /**
     * 添加初始子节点（创建阶段）
     */
    virtual void appendInitialChild(Instance parent, Instance child) = 0;
    
    /**
     * 添加子节点
     */
    virtual void appendChild(Instance parent, Instance child) = 0;
    
    /**
     * 移除子节点
     */
    virtual void removeChild(Instance parent, Instance child) = 0;
    
    /**
     * 在指定节点前插入子节点
     */
    virtual void insertBefore(Instance parent, Instance child, Instance beforeChild) = 0;
    
    // =========================================================================
    // 容器操作
    // =========================================================================
    
    /**
     * 将子节点添加到容器
     */
    virtual void appendChildToContainer(Container container, Instance child) = 0;
    
    /**
     * 从容器移除子节点
     */
    virtual void removeChildFromContainer(Container container, Instance child) = 0;
    
    /**
     * 在容器中的指定节点前插入子节点
     */
    virtual void insertInContainerBefore(
        Container container, 
        Instance child, 
        Instance beforeChild
    ) = 0;
    
    // =========================================================================
    // 属性更新
    // =========================================================================
    
    /**
     * 准备更新（计算 diff）
     * 
     * @return 更新负载，如果无需更新则返回 null
     */
    virtual facebook::jsi::Value prepareUpdate(
        Instance instance,
        const std::string& type,
        const facebook::jsi::Object& oldProps,
        const facebook::jsi::Object& newProps,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    /**
     * 提交更新
     */
    virtual void commitUpdate(
        Instance instance,
        const facebook::jsi::Object& updatePayload,
        const std::string& type,
        const facebook::jsi::Object& oldProps,
        const facebook::jsi::Object& newProps,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    /**
     * 提交文本更新
     */
    virtual void commitTextUpdate(
        TextInstance textInstance,
        const std::string& oldText,
        const std::string& newText
    ) = 0;
    
    // =========================================================================
    // 完成与准备
    // =========================================================================
    
    /**
     * 完成初始子节点的创建
     * 
     * @return 返回 true 如果需要在 commit 阶段进行额外工作
     */
    virtual bool finalizeInitialChildren(
        Instance instance,
        const std::string& type,
        const facebook::jsi::Object& props,
        facebook::jsi::Runtime& runtime
    ) { return false; }
    
    /**
     * 准备提交
     */
    virtual void prepareForCommit(Container container) {}
    
    /**
     * 重置提交后状态
     */
    virtual void resetAfterCommit(Container container) {}
    
    // =========================================================================
    // 焦点与选择
    // =========================================================================
    
    /**
     * 获取公共实例
     */
    virtual PublicInstance getPublicInstance(Instance instance) { return instance; }
    
    // =========================================================================
    // 能力检测
    // =========================================================================
    
    /**
     * 是否支持 Mutation 模式
     */
    virtual bool supportsMutation() { return true; }
    
    /**
     * 是否支持 Persistence 模式
     */
    virtual bool supportsPersistence() { return false; }
    
    /**
     * 是否支持 Hydration
     */
    virtual bool supportsHydration() { return false; }
    
    /**
     * 是否支持微任务
     */
    virtual bool supportsMicrotasks() { return true; }
    
    /**
     * 调度微任务
     */
    virtual void scheduleMicrotask(std::function<void()> task) {}
    
    // =========================================================================
    // 超时配置
    // =========================================================================
    
    /**
     * 获取无超时值
     */
    virtual int getNoTimeout() { return -1; }
    
    /**
     * 是否是无超时值
     */
    virtual bool isNoTimeout(int timeout) { return timeout == -1; }
};

// =============================================================================
// ReactHostRuntime - 统一宿主运行时
// =============================================================================

/**
 * ReactHostRuntime 将 JSI Runtime、HostConfig 和 SchedulerHost 统一管理
 */
class ReactHostRuntime {
public:
    ReactHostRuntime(
        facebook::jsi::Runtime& jsiRuntime,
        std::unique_ptr<HostConfig> hostConfig,
        std::unique_ptr<SchedulerHost> schedulerHost
    ) : jsiRuntime_(jsiRuntime),
        hostConfig_(std::move(hostConfig)),
        schedulerHost_(std::move(schedulerHost)) {}
    
    ~ReactHostRuntime() = default;
    
    // 禁止拷贝
    ReactHostRuntime(const ReactHostRuntime&) = delete;
    ReactHostRuntime& operator=(const ReactHostRuntime&) = delete;
    
    // 允许移动
    ReactHostRuntime(ReactHostRuntime&&) = default;
    ReactHostRuntime& operator=(ReactHostRuntime&&) = default;
    
    // =========================================================================
    // 访问器
    // =========================================================================
    
    facebook::jsi::Runtime& getJSIRuntime() { return jsiRuntime_; }
    const facebook::jsi::Runtime& getJSIRuntime() const { return jsiRuntime_; }
    
    HostConfig& getHostConfig() { return *hostConfig_; }
    const HostConfig& getHostConfig() const { return *hostConfig_; }
    
    SchedulerHost& getSchedulerHost() { return *schedulerHost_; }
    const SchedulerHost& getSchedulerHost() const { return *schedulerHost_; }
    
    // =========================================================================
    // 便捷方法
    // =========================================================================
    
    /**
     * 获取当前时间
     */
    double now() { return schedulerHost_->getCurrentTime(); }
    
    /**
     * 判断是否应该让出
     */
    bool shouldYield() { return schedulerHost_->shouldYieldToHost(); }
    
private:
    facebook::jsi::Runtime& jsiRuntime_;
    std::unique_ptr<HostConfig> hostConfig_;
    std::unique_ptr<SchedulerHost> schedulerHost_;
};

} // namespace react
