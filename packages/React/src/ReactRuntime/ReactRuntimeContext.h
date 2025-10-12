#pragma once

#include <stdexcept>

namespace facebook {
namespace jsi {
class Runtime;
} // namespace jsi
} // namespace facebook

namespace react {

class ReactRuntime;

void setCurrentReactRuntime(ReactRuntime* runtime) noexcept;
void setCurrentJsiRuntime(facebook::jsi::Runtime* runtime) noexcept;
ReactRuntime* getCurrentReactRuntime() noexcept;
facebook::jsi::Runtime* getCurrentJsiRuntime() noexcept;
ReactRuntime& requireReactRuntime();
facebook::jsi::Runtime& requireJsiRuntime();

class RuntimeContextGuard {
public:
  RuntimeContextGuard(ReactRuntime* runtime, facebook::jsi::Runtime* jsRuntime) noexcept;
  RuntimeContextGuard(const RuntimeContextGuard&) = delete;
  RuntimeContextGuard& operator=(const RuntimeContextGuard&) = delete;
  ~RuntimeContextGuard();

private:
  ReactRuntime* previousRuntime_{nullptr};
  facebook::jsi::Runtime* previousJsiRuntime_{nullptr};
};

} // namespace react
