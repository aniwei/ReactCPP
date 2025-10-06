#pragma once

#include "ReactDOM/client/ReactDOMComponent.h"

#include "jsi/jsi.h"

#include <memory>
#include <string>

namespace react {

class HostInterface {
public:
  HostInterface() = default;

  std::shared_ptr<ReactDOMInstance> createHostInstance(
      facebook::jsi::Runtime& runtime,
      const std::string& type,
      const facebook::jsi::Object& props);

  std::shared_ptr<ReactDOMInstance> createHostTextInstance(
      facebook::jsi::Runtime& runtime,
      const std::string& text);

  void appendHostChild(
      std::shared_ptr<ReactDOMInstance> parent,
      std::shared_ptr<ReactDOMInstance> child);

  void insertHostChildBefore(
      std::shared_ptr<ReactDOMInstance> parent,
      std::shared_ptr<ReactDOMInstance> child,
      std::shared_ptr<ReactDOMInstance> beforeChild);

  void removeHostChild(
      std::shared_ptr<ReactDOMInstance> parent,
      std::shared_ptr<ReactDOMInstance> child);

  void commitHostUpdate(
      facebook::jsi::Runtime& runtime,
      std::shared_ptr<ReactDOMInstance> instance,
      const facebook::jsi::Object& oldProps,
      const facebook::jsi::Object& newProps,
      const facebook::jsi::Object& payload);

  void commitHostTextUpdate(
      std::shared_ptr<ReactDOMInstance> instance,
      const std::string& oldText,
      const std::string& newText);

private:
  void detachFromParent(const std::shared_ptr<ReactDOMInstance>& child);
};

} // namespace react
