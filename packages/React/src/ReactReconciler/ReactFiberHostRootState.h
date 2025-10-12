#pragma once

#include <memory>

namespace facebook {
namespace jsi {
class Value;
} // namespace jsi
} // namespace facebook

namespace react {

struct HostRootMemoizedState {
  std::unique_ptr<facebook::jsi::Value> element{};
  bool isDehydrated{false};
  void* cache{nullptr};
};

} // namespace react
