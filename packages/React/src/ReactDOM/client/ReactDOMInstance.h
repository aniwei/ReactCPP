#pragma once

#include <memory>
#include <string>

namespace react {

class ReactDOMInstance : public std::enable_shared_from_this<ReactDOMInstance> {
public:
  virtual ~ReactDOMInstance() = default;

  void setKey(std::string key);
  [[nodiscard]] const std::string& getKey() const noexcept;

  [[nodiscard]] std::shared_ptr<ReactDOMInstance> getParent() const;
  void setParent(const std::shared_ptr<ReactDOMInstance>& parent);
  void clearParent();

  [[nodiscard]] virtual bool isTextInstance() const = 0;
  [[nodiscard]] virtual std::string debugDescription() const = 0;

protected:
  ReactDOMInstance() = default;

private:
  std::weak_ptr<ReactDOMInstance> parent_;
  std::string key_{};
};

} // namespace react
