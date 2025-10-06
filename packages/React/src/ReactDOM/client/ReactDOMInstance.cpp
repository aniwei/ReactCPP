#include "ReactDOM/client/ReactDOMInstance.h"

namespace react {

void ReactDOMInstance::setKey(std::string key) {
  key_ = std::move(key);
}

const std::string& ReactDOMInstance::getKey() const noexcept {
  return key_;
}

std::shared_ptr<ReactDOMInstance> ReactDOMInstance::getParent() const {
  return parent_.lock();
}

void ReactDOMInstance::setParent(const std::shared_ptr<ReactDOMInstance>& parent) {
  parent_ = parent;
}

void ReactDOMInstance::clearParent() {
  parent_.reset();
}

} // namespace react
