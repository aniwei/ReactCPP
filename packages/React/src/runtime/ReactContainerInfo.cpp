#include "ReactContainerInfo.h"

namespace react {

ContainerInfo::ContainerInfo() = default;

ContainerInfo::ContainerInfo(ReactDOMContainer* c) : container(c) {}

ContainerInfo::ContainerInfo(std::string name)
  : container(nullptr), debugName(std::move(name)) {}

bool ContainerInfo::hasContainer() const {
  return container != nullptr;
}

} // namespace react
