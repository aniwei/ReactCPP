#pragma once

#include <string>
#include <utility>

#include "ReactDOMInstance.h"

namespace react {

// ContainerInfo - HostRoot/HostPortal 的 containerInfo
// 在 reactjs 中它通常是一个 host Container（类型别名）。
// 在这里用结构体承载，既能保留强类型的宿主容器指针，也能在测试/调试时携带一个可读名字。
struct ContainerInfo {
  ReactDOMContainer* container = nullptr;
  std::string debugName;

  ContainerInfo();
  explicit ContainerInfo(ReactDOMContainer* c);
  explicit ContainerInfo(std::string name);

  bool hasContainer() const;
};

} // namespace react
