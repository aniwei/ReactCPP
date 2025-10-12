#include "ReactRuntime/ReactRuntimeContext.h"

#include "ReactRuntime/ReactRuntime.h"

#include "jsi/jsi.h"

namespace react {
namespace {
thread_local ReactRuntime* gCurrentRuntime = nullptr;
thread_local facebook::jsi::Runtime* gCurrentJsiRuntime = nullptr;
} // namespace

void setCurrentReactRuntime(ReactRuntime* runtime) noexcept {
  gCurrentRuntime = runtime;
}

void setCurrentJsiRuntime(facebook::jsi::Runtime* runtime) noexcept {
  gCurrentJsiRuntime = runtime;
}

ReactRuntime* getCurrentReactRuntime() noexcept {
  return gCurrentRuntime;
}

facebook::jsi::Runtime* getCurrentJsiRuntime() noexcept {
  return gCurrentJsiRuntime;
}

ReactRuntime& requireReactRuntime() {
  if (gCurrentRuntime == nullptr) {
    throw std::logic_error("ReactRuntime context is not attached");
  }
  return *gCurrentRuntime;
}

facebook::jsi::Runtime& requireJsiRuntime() {
  if (gCurrentJsiRuntime == nullptr) {
    throw std::logic_error("JSI runtime context is not attached");
  }
  return *gCurrentJsiRuntime;
}

RuntimeContextGuard::RuntimeContextGuard(ReactRuntime* runtime, facebook::jsi::Runtime* jsRuntime) noexcept
    : previousRuntime_(gCurrentRuntime), previousJsiRuntime_(gCurrentJsiRuntime) {
  if (runtime != nullptr) {
    gCurrentRuntime = runtime;
  }
  if (jsRuntime != nullptr) {
    gCurrentJsiRuntime = jsRuntime;
  }
}

RuntimeContextGuard::~RuntimeContextGuard() {
  gCurrentRuntime = previousRuntime_;
  gCurrentJsiRuntime = previousJsiRuntime_;
}

} // namespace react
