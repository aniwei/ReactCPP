#include "ReactReconciler/ReactFiberThenable.h"
#include "ReactReconciler/ReactFiberCallUserSpace.h"

#include "shared/ReactFeatureFlags.h"
#include "shared/ReactSharedInternals.h"

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kSuspenseExceptionMessage =
    "Suspense Exception: This is not a real error! It's an implementation detail of `use` to interrupt the current render. You must either rethrow it immediately, or move the `use` call outside of the `try/catch` block. Capturing without rethrowing will lead to unexpected behavior.\n\nTo handle async errors, wrap your component in an error boundary, or call the promise's `.catch` method and pass the result to `use`.";
constexpr const char* kSuspenseActionExceptionMessage =
    "Suspense Exception: This is not a real error! It's an implementation detail of `useActionState` to interrupt the current render. You must either rethrow it immediately, or move the `useActionState` call outside of the `try/catch` block. Capturing without rethrowing will lead to unexpected behavior.\n\nTo handle async errors, wrap your component in an error boundary.";
constexpr const char* kSuspenseyCommitExceptionMessage =
  "Suspense Exception: This is not a real error, and should not leak into userspace. If you're seeing this, it's likely a bug in React.";
constexpr const char* kHooksUnsupportedInAsyncComponentMessage =
  "Hooks are not supported inside an async component. This error is often caused by accidentally adding 'use client' to a module that was originally written for the server.";

} // namespace

namespace react {

const char* SuspenseException::what() const noexcept {
  return kSuspenseExceptionMessage;
}

const char* SuspenseyCommitException::what() const noexcept {
  return kSuspenseyCommitExceptionMessage;
}

const char* SuspenseActionException::what() const noexcept {
  return kSuspenseActionExceptionMessage;
}

namespace {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

constexpr const char* kStatusProp = "status";
constexpr const char* kValueProp = "value";
constexpr const char* kReasonProp = "reason";
constexpr const char* kThenProp = "then";
constexpr const char* kDisplayNameProp = "displayName";
constexpr const char* kDebugInfoProp = "_debugInfo";
PropNameID propId(Runtime& runtime, const char* name) {
  return PropNameID::forAscii(runtime, name);
}

String stringLiteral(Runtime& runtime, const char* literal) {
  return String::createFromUtf8(runtime, literal);
}

std::optional<std::string> getStringProperty(Runtime& runtime, const Object& object, const char* name) {
  if (!object.hasProperty(runtime, name)) {
    return std::nullopt;
  }
  Value property = object.getProperty(runtime, name);
  if (!property.isString()) {
    return std::nullopt;
  }
  return property.getString(runtime).utf8(runtime);
}

bool isThenableObject(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    return false;
  }
  Object object = value.getObject(runtime);
  if (!object.hasProperty(runtime, kThenProp)) {
    return false;
  }
  Value thenValue = object.getProperty(runtime, kThenProp);
  if (!thenValue.isObject()) {
    return false;
  }
  return thenValue.getObject(runtime).isFunction(runtime);
}

double getPerformanceNow(Runtime& runtime) {
  try {
    Object global = runtime.global();
    if (!global.hasProperty(runtime, "performance")) {
      return 0.0;
    }
    Value performanceValue = global.getProperty(runtime, "performance");
    if (!performanceValue.isObject()) {
      return 0.0;
    }
    Object performance = performanceValue.getObject(runtime);
    if (!performance.hasProperty(runtime, "now")) {
      return 0.0;
    }
    Value nowValue = performance.getProperty(runtime, "now");
    if (!nowValue.isObject()) {
      return 0.0;
    }
    Function nowFunction = nowValue.getObject(runtime).asFunction(runtime);
    Value result = nowFunction.call(runtime, Value::undefined(), nullptr, 0);
    if (!result.isNumber()) {
      return 0.0;
    }
    return result.getNumber();
  } catch (const std::exception&) {
    return 0.0;
  }
}

void markDidUsePromiseIfActing(Runtime& runtime) {
#if !defined(NDEBUG)
  try {
    Object internals = getReactSharedInternals(runtime);
    if (!hasReactSharedInternalsProperty(runtime, internals, ReactSharedInternalsKeys::kActQueue)) {
      return;
    }
    Value queueValue = getReactSharedInternalsProperty(runtime, internals, ReactSharedInternalsKeys::kActQueue);
    if (queueValue.isNull() || queueValue.isUndefined()) {
      return;
    }
    if (hasReactSharedInternalsProperty(runtime, internals, ReactSharedInternalsKeys::kDidUsePromise)) {
      setReactSharedInternalsProperty(runtime, internals, ReactSharedInternalsKeys::kDidUsePromise, Value(true));
    }
  } catch (const std::exception&) {
    // Ignore failures to interact with the internals object in DEV.
  }
#else
  (void)runtime;
#endif
}

void ensureThenableInstrumentation(Runtime& runtime, Object& thenable) {
  const auto statusString = getStringProperty(runtime, thenable, kStatusProp);
  if (statusString.has_value()) {
    return;
  }

  thenable.setProperty(runtime, kStatusProp, stringLiteral(runtime, "pending"));

  auto thenableRef = std::make_shared<Value>(runtime, thenable);

  Function onFulfilled = Function::createFromHostFunction(
      runtime,
      propId(runtime, "__react_onFulfilled"),
      1,
      [thenableRef](Runtime& innerRuntime, const Value&, const Value* args, size_t count) -> Value {
        if (!thenableRef) {
          return Value::undefined();
        }
        Object tracked = thenableRef->getObject(innerRuntime);
        tracked.setProperty(innerRuntime, kStatusProp, stringLiteral(innerRuntime, "fulfilled"));
        if (count > 0) {
          tracked.setProperty(innerRuntime, kValueProp, args[0]);
        } else {
          tracked.setProperty(innerRuntime, kValueProp, Value::undefined());
        }
        return Value::undefined();
      });

  Function onRejected = Function::createFromHostFunction(
      runtime,
      propId(runtime, "__react_onRejected"),
      1,
      [thenableRef](Runtime& innerRuntime, const Value&, const Value* args, size_t count) -> Value {
        if (!thenableRef) {
          return Value::undefined();
        }
        Object tracked = thenableRef->getObject(innerRuntime);
        tracked.setProperty(innerRuntime, kStatusProp, stringLiteral(innerRuntime, "rejected"));
        if (count > 0) {
          tracked.setProperty(innerRuntime, kReasonProp, args[0]);
        } else {
          tracked.setProperty(innerRuntime, kReasonProp, Value::undefined());
        }
        return Value::undefined();
      });

  if (!thenable.hasProperty(runtime, kThenProp)) {
    return;
  }

  Value thenValue = thenable.getProperty(runtime, kThenProp);
  if (!thenValue.isObject()) {
    return;
  }

  Object thenObject = thenValue.getObject(runtime);
  if (!thenObject.isFunction(runtime)) {
    return;
  }

  Function thenFunction = thenObject.asFunction(runtime);
    std::array<Value, 2> args = {Value(runtime, onFulfilled), Value(runtime, onRejected)};
    thenFunction.callWithThis(
        runtime, thenable, static_cast<const Value*>(args.data()), args.size());
}

std::string valueToDisplayName(Runtime& runtime, const Object& thenable) {
  const auto displayName = getStringProperty(runtime, thenable, kDisplayNameProp);
  if (displayName.has_value() && !displayName->empty()) {
    return *displayName;
  }
  return "Promise";
}

void ensureAsyncDebugInfo(Runtime& runtime, Object& thenable) {
  if (!enableAsyncDebugInfo) {
    return;
  }

  if (thenable.hasProperty(runtime, kDebugInfoProp)) {
    Value debugValue = thenable.getProperty(runtime, kDebugInfoProp);
    if (!debugValue.isUndefined()) {
      return;
    }
  }

  double now = getPerformanceNow(runtime);
  const std::string displayName = valueToDisplayName(runtime, thenable);
  Object awaited(runtime);
  awaited.setProperty(runtime, "name", String::createFromUtf8(runtime, displayName));
  awaited.setProperty(runtime, "start", Value(now));
  awaited.setProperty(runtime, "end", Value(now));
  awaited.setProperty(runtime, "value", Value(runtime, thenable));

  Object entry(runtime);
  entry.setProperty(runtime, "awaited", Value(runtime, awaited));

  Array info(runtime, 1);
  info.setValueAtIndex(runtime, 0, Value(runtime, entry));
  thenable.setProperty(runtime, kDebugInfoProp, Value(runtime, info));
}

std::optional<std::string> tryGetStatus(Runtime& runtime, const Object& thenable) {
  return getStringProperty(runtime, thenable, kStatusProp);
}

Value cloneValue(Runtime& runtime, const Value& value) {
  return Value(runtime, value);
}

void warnAboutUncachedPromise(ThenableState& state) {
#if !defined(NDEBUG)
  if (state.didWarnAboutUncachedPromise) {
    return;
  }
  state.didWarnAboutUncachedPromise = true;
  std::cerr
      << "A component was suspended by an uncached promise. Creating promises inside a Client Component or hook is not yet supported, except via a Suspense-compatible library or framework."
      << std::endl;
#else
  (void)state;
#endif
}

std::string valueToMessage(Runtime& runtime, const Value& value) {
  if (value.isString()) {
    return value.getString(runtime).utf8(runtime);
  }
  if (value.isNumber()) {
    return std::to_string(value.getNumber());
  }
  if (value.isBool()) {
    return value.getBool() ? "true" : "false";
  }
  if (value.isObject()) {
    return "[object Object]";
  }
  if (value.isNull()) {
    return "null";
  }
  if (value.isUndefined()) {
    return "undefined";
  }
  return "(unknown)";
}

std::string extractErrorMessage(Runtime& runtime, const Value& reason) {
  if (reason.isObject()) {
    Object reasonObject = reason.getObject(runtime);
    if (reasonObject.hasProperty(runtime, "message")) {
      Value messageValue = reasonObject.getProperty(runtime, "message");
      if (messageValue.isString()) {
        return messageValue.getString(runtime).utf8(runtime);
      }
    }
  }
  return valueToMessage(runtime, reason);
}

class NoopSuspenseyCommitThenable final : public Wakeable {
 public:
  void then(
      std::function<void()> onFulfilled,
      std::function<void()> onRejected) override {
    (void)onFulfilled;
    (void)onRejected;
#if !defined(NDEBUG)
    std::cerr << "NoopSuspenseyCommitThenable received an unexpected listener." << std::endl;
#endif
  }
};

NoopSuspenseyCommitThenable& instance() {
  static NoopSuspenseyCommitThenable singleton;
  return singleton;
}

const SuspenseException& suspenseExceptionInstance() {
  static SuspenseException instance;
  return instance;
}

const SuspenseyCommitException& suspenseyCommitExceptionInstance() {
  static SuspenseyCommitException instance;
  return instance;
}

const SuspenseActionException& suspenseActionExceptionInstance() {
  static SuspenseActionException instance;
  return instance;
}

std::unique_ptr<jsi::Value>& suspendedThenableSlot() {
  static std::unique_ptr<jsi::Value> slot;
  return slot;
}

bool& suspendedNeedsResetDevFlag() {
  static bool flag = false;
  return flag;
}

} // namespace

Wakeable& noopSuspenseyCommitThenable() {
  return instance();
}

bool isNoopSuspenseyCommitThenable(const Wakeable* wakeable) {
  if (wakeable == nullptr) {
    return false;
  }
  return wakeable == &instance();
}

bool isNoopSuspenseyCommitThenable(const void* value) {
  return isNoopSuspenseyCommitThenable(tryGetWakeable(value));
}

const SuspenseException& suspenseException() {
  return suspenseExceptionInstance();
}

const SuspenseyCommitException& suspenseyCommitException() {
  return suspenseyCommitExceptionInstance();
}

const SuspenseActionException& suspenseActionException() {
  return suspenseActionExceptionInstance();
}

[[noreturn]] void throwSuspenseException() {
  throw suspenseException();
}

[[noreturn]] void throwSuspenseyCommitException() {
  throw suspenseyCommitException();
}

[[noreturn]] void throwSuspenseActionException() {
  throw suspenseActionException();
}

[[noreturn]] void suspendCommit() {
  throwSuspenseyCommitException();
}

ThenableState createThenableState(jsi::Runtime& /*runtime*/) {
  return ThenableState{};
}

bool isThenableResolved(jsi::Runtime& runtime, const jsi::Value& thenableValue) {
  if (!thenableValue.isObject()) {
    return false;
  }
  Object thenable = thenableValue.getObject(runtime);
  const auto status = tryGetStatus(runtime, thenable);
  if (!status.has_value()) {
    return false;
  }
  return *status == "fulfilled" || *status == "rejected";
}

jsi::Value trackUsedThenable(
    jsi::Runtime& runtime,
    ThenableState& state,
    const jsi::Value& thenableValue,
    std::size_t index) {
  markDidUsePromiseIfActing(runtime);

  if (!thenableValue.isObject()) {
    throw std::runtime_error("Expected a thenable object.");
  }

  Value incomingClone(runtime, thenableValue);
  Object incomingThenable = incomingClone.getObject(runtime);

  if (index >= state.thenables.size()) {
    state.thenables.resize(index + 1);
  }

  std::unique_ptr<Value>& slot = state.thenables[index];
  bool reusePrevious = false;

  if (slot == nullptr) {
    slot = std::make_unique<Value>(runtime, incomingThenable);
  } else if (!Value::strictEquals(runtime, incomingClone, *slot)) {
    warnAboutUncachedPromise(state);

    if (incomingThenable.hasProperty(runtime, kThenProp)) {
      std::array<Value, 2> noopArgs = {
          Value(runtime, Function::createFromHostFunction(
                              runtime,
                              propId(runtime, "__react_uncachedThenableNoop"),
                              1,
                              [](Runtime&, const Value&, const Value*, size_t) -> Value {
                                return Value::undefined();
                              })),
          Value(runtime, Function::createFromHostFunction(
                              runtime,
                              propId(runtime, "__react_uncachedThenableNoopRejected"),
                              1,
                              [](Runtime&, const Value&, const Value*, size_t) -> Value {
                                return Value::undefined();
                              }))};

      Value thenValue = incomingThenable.getProperty(runtime, kThenProp);
      if (thenValue.isObject()) {
        Object thenObject = thenValue.getObject(runtime);
        if (thenObject.isFunction(runtime)) {
          Function thenFunction = thenObject.asFunction(runtime);
      thenFunction.callWithThis(
        runtime,
        incomingThenable,
        static_cast<const Value*>(noopArgs.data()),
        noopArgs.size());
        }
      }
    }

    reusePrevious = true;
  }

  Value& trackedValue = *slot;
  if (!reusePrevious) {
    trackedValue = Value(runtime, incomingThenable);
  }

  Object trackedThenable = trackedValue.getObject(runtime);
  ensureThenableInstrumentation(runtime, trackedThenable);
  ensureAsyncDebugInfo(runtime, trackedThenable);

  const auto status = tryGetStatus(runtime, trackedThenable);
  if (status.has_value()) {
    if (*status == "fulfilled") {
      if (trackedThenable.hasProperty(runtime, kValueProp)) {
        Value fulfilled = trackedThenable.getProperty(runtime, kValueProp);
        return cloneValue(runtime, fulfilled);
      }
      return Value::undefined();
    }
    if (*status == "rejected") {
      Value reason = trackedThenable.hasProperty(runtime, kReasonProp)
          ? trackedThenable.getProperty(runtime, kReasonProp)
          : Value::undefined();
      checkIfUseWrappedInAsyncCatch(runtime, reason);
      throw std::runtime_error(extractErrorMessage(runtime, reason));
    }
  }

  setSuspendedThenable(trackedValue, runtime);
  throwSuspenseException();
}

void setSuspendedThenable(const jsi::Value& thenable, jsi::Runtime& runtime) {
  suspendedThenableSlot() = std::make_unique<jsi::Value>(runtime, thenable);
  suspendedNeedsResetDevFlag() = true;
}

jsi::Value getSuspendedThenable(jsi::Runtime& runtime) {
  auto& slot = suspendedThenableSlot();
  if (slot == nullptr) {
    throw std::runtime_error("Expected a suspended thenable, but none was recorded");
  }
  jsi::Value clone(runtime, *slot);
  slot.reset();
  suspendedNeedsResetDevFlag() = false;
  return clone;
}

bool hasSuspendedThenable() {
  return suspendedThenableSlot() != nullptr;
}

bool checkIfUseWrappedInTryCatch() {
  bool& flag = suspendedNeedsResetDevFlag();
  bool previous = flag;
  flag = false;
  return previous;
}

void checkIfUseWrappedInAsyncCatch(jsi::Runtime& runtime, const jsi::Value& rejectedReason) {
  const std::string message = extractErrorMessage(runtime, rejectedReason);
  if (message == kSuspenseExceptionMessage || message == kSuspenseActionExceptionMessage) {
    throw std::runtime_error(kHooksUnsupportedInAsyncComponentMessage);
  }
}

jsi::Value resolveLazy(jsi::Runtime& runtime, const jsi::Value& lazyValue) {
  if (!lazyValue.isObject()) {
    return Value(runtime, lazyValue);
  }

  Object lazyObject = lazyValue.getObject(runtime);
  if (!lazyObject.hasProperty(runtime, "_init") || !lazyObject.hasProperty(runtime, "_payload")) {
    return Value(runtime, lazyValue);
  }

  Value initValue = lazyObject.getProperty(runtime, "_init");
  if (!initValue.isObject()) {
    return Value(runtime, lazyValue);
  }

  Object initObject = initValue.getObject(runtime);
  if (!initObject.isFunction(runtime)) {
    return Value(runtime, lazyValue);
  }

  Function initFunction = initObject.asFunction(runtime);
  Value payload = lazyObject.getProperty(runtime, "_payload");

  try {
#if !defined(NDEBUG)
    return callLazyInitInDEV(runtime, initFunction, payload);
#else
    Value thisValue = Value::undefined();
    Value args[] = {Value(runtime, payload)};
    return initFunction.call(runtime, thisValue, args, 1);
#endif
  } catch (const SuspenseException&) {
    throw;
  } catch (facebook::jsi::JSError& error) {
    Value thrownValue(runtime, error.value());
    if (isThenableObject(runtime, thrownValue)) {
      setSuspendedThenable(thrownValue, runtime);
      throwSuspenseException();
    }
    throw;
  }
}

} // namespace react
