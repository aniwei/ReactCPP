#pragma once

#include "ReactReconciler/ReactFiberConcurrentUpdates.h"
#include "ReactReconciler/ReactFiberFlags.h"

#include "jsi/jsi.h"

#include <cstdint>
#include <memory>

namespace react {

class ReactRuntime;
struct FiberNode;

enum class HookFlags : std::uint8_t {
  None = 0,
  HasEffect = 1 << 0,
  Layout = 1 << 1,
  Insertion = 1 << 2,
  Passive = 1 << 3,
};

inline HookFlags operator|(HookFlags a, HookFlags b) {
  return static_cast<HookFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

inline HookFlags operator&(HookFlags a, HookFlags b) {
  return static_cast<HookFlags>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

inline HookFlags operator~(HookFlags value) {
  return static_cast<HookFlags>(~static_cast<std::uint8_t>(value));
}

inline bool hasHookFlag(HookFlags value, HookFlags flag) {
  return (value & flag) == flag;
}

struct Effect {
  HookFlags tag{HookFlags::None};
  facebook::jsi::Value create{facebook::jsi::Value::undefined()};
  facebook::jsi::Value deps{facebook::jsi::Value::undefined()};
  facebook::jsi::Value inst{facebook::jsi::Value::undefined()};
  Effect* next{nullptr};

  Effect() = default;

  Effect(
      facebook::jsi::Runtime& runtime,
      HookFlags effectTag,
      const facebook::jsi::Value& createValue,
      const facebook::jsi::Value& depsValue,
      const facebook::jsi::Value& instValue)
      : tag(effectTag),
        create(runtime, createValue),
        deps(runtime, depsValue),
        inst(runtime, instValue),
        next(nullptr) {}
};

struct FunctionComponentUpdateQueue {
  Effect* lastEffect{nullptr};
  facebook::jsi::Value events{facebook::jsi::Value::undefined()};
  facebook::jsi::Value stores{facebook::jsi::Value::undefined()};
};

struct HookUpdate : ConcurrentUpdate {
  facebook::jsi::Value action{facebook::jsi::Value::undefined()};
};

struct HookQueue : ConcurrentUpdateQueue {
  ReactRuntime* runtime{nullptr};
  FiberNode* fiber{nullptr};
  std::shared_ptr<facebook::jsi::Function> dispatch{};
  std::unique_ptr<facebook::jsi::Value> reducer{};
  std::unique_ptr<facebook::jsi::Value> lastRenderedState{};
  bool isReducer{false};
};

struct Hook {
  std::unique_ptr<facebook::jsi::Value> memoizedState{};
  std::unique_ptr<facebook::jsi::Value> baseState{};
  HookUpdate* baseQueue{nullptr};
  std::shared_ptr<HookQueue> queue{};
  Hook* next{nullptr};
  Effect* memoizedEffect{nullptr};
};

} // namespace react
