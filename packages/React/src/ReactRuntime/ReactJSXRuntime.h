#pragma once

#include "ReactRuntime/ReactWasmLayout.h"
#include "jsi/jsi.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace react::jsx {

namespace jsi = facebook::jsi;

struct ReactElement;

using ReactElementPtr = std::shared_ptr<ReactElement>;

struct SourceLocation {
  std::string fileName;
  int lineNumber{0};
  int columnNumber{0};

  [[nodiscard]] bool isValid() const {
    return !fileName.empty();
  }
};

struct ReactElement {
  jsi::Value type;
  jsi::Value props;
  std::optional<jsi::Value> key;
  std::optional<jsi::Value> ref;
  std::optional<SourceLocation> source;
  bool hasStaticChildren{false};
};

ReactElementPtr jsx(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& props,
    std::optional<jsi::Value> key = std::nullopt,
    std::optional<jsi::Value> ref = std::nullopt);

ReactElementPtr jsxs(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& props,
    std::optional<jsi::Value> key = std::nullopt,
    std::optional<jsi::Value> ref = std::nullopt);

ReactElementPtr jsxDEV(
    jsi::Runtime& runtime,
    const jsi::Value& type,
    const jsi::Value& config,
    std::optional<jsi::Value> maybeKey = std::nullopt,
    SourceLocation source = {},
    std::optional<jsi::Value> ref = std::nullopt);

struct WasmSerializedLayout {
  std::vector<uint8_t> buffer;
  uint32_t rootOffset{0};
};

WasmSerializedLayout serializeToWasm(jsi::Runtime& runtime, const ReactElement& element);

jsi::Value createJsxHostValue(jsi::Runtime& runtime, const ReactElementPtr& element);
ReactElementPtr getReactElementFromValue(jsi::Runtime& runtime, const jsi::Value& value);
bool isReactElementValue(jsi::Runtime& runtime, const jsi::Value& value);

} // namespace react::jsx
