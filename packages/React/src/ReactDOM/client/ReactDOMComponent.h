#pragma once

#include "ReactDOM/client/ReactDOMInstance.h"

#include "jsi/jsi.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace react {

class ReactDOMComponent final : public ReactDOMInstance {
public:
  ReactDOMComponent(
      std::string type,
      facebook::jsi::Runtime& runtime,
      const facebook::jsi::Object& props,
      bool isTextInstance = false,
      std::string textContent = {}
  );

  [[nodiscard]] bool isTextInstance() const override;
  [[nodiscard]] const std::string& getType() const noexcept;
  [[nodiscard]] const std::unordered_map<std::string, facebook::jsi::Value>& getProps() const noexcept;
  [[nodiscard]] const std::string& getTextContent() const noexcept;

  void setProps(facebook::jsi::Runtime& runtime, const facebook::jsi::Object& props);
  void setTextContent(std::string text);

  [[nodiscard]] std::string debugDescription() const override;

  std::vector<std::shared_ptr<ReactDOMInstance>> children;

private:
  void rebuildPropsMap(facebook::jsi::Runtime& runtime, const facebook::jsi::Object& props);

  std::string type_;
  bool isTextInstance_{false};
  std::unordered_map<std::string, facebook::jsi::Value> props_;
  std::string textContent_{};
};

using ReactDOMComponentPtr = std::shared_ptr<ReactDOMComponent>;

} // namespace react
