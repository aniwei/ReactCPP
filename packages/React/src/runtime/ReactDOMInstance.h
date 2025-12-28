/**
 * ReactDOMInstance - Host Instance 抽象
 *
 * 严格参考 reactjs HostConfig 类型：
 * - Instance: HostComponent/HostSingleton 等的 stateNode
 * - TextInstance: HostText 的 stateNode
 * - SuspenseInstance:（DOM 下通常是注释节点）Suspense 边界的宿主实例
 * - Container: HostRoot/HostPortal 的 containerInfo
 *
 * 在 JS 版本里这些类型由具体 renderer（react-dom/react-native/react-noop）提供。
 * 在本 C++ 版本里用纯接口做“类型占位/约束”，具体宿主实现负责提供真实对象并管理其生命周期。
 */

#pragma once

namespace react {

// 对应 react-dom 的 Instance（例如 DOM Element）
struct ReactDOMInstance {
  virtual ~ReactDOMInstance() = default;
};

// 对应 react-dom 的 TextInstance（例如 DOM Text）
struct ReactDOMTextInstance : ReactDOMInstance {
  ~ReactDOMTextInstance() override = default;
};

// 对应 react-dom 的 SuspenseInstance（例如 DOM Comment）
struct ReactDOMSuspenseInstance : ReactDOMInstance {
  ~ReactDOMSuspenseInstance() override = default;
};

// 对应 react-dom 的 Container（例如 Element/Document/DocumentFragment）
// 注意：在 reactjs 里 Container 不是 Instance 的子类型，这里也保持独立。
struct ReactDOMContainer {
  virtual ~ReactDOMContainer() = default;
};

} // namespace react
