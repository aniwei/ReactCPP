#include "ReactRuntime/ReactJSXRuntime.h"
#include "TestRuntime.h"

#include <cassert>
#include <cstring>

namespace react::test {

namespace jsi = facebook::jsi;

bool runReactJSXRuntimeTests() {
  using namespace react::jsx;

  TestRuntime runtime;

  auto makeStringValue = [&runtime](const std::string& text) {
    return jsi::Value(runtime, jsi::String::createFromUtf8(runtime, text));
  };

  jsi::Object childProps(runtime);
  childProps.setProperty(runtime, "className", makeStringValue("chip"));
  childProps.setProperty(runtime, "children", makeStringValue("Alpha"));

  auto child = jsx(
      runtime,
      makeStringValue("span"),
      jsi::Value(runtime, childProps),
      std::optional<jsi::Value>(makeStringValue("alpha")));
  assert(child != nullptr);
  assert(child->type.isString());
  assert(child->type.getString(runtime).utf8(runtime) == "span");
  assert(child->key.has_value());
  assert(child->key->isString());
  assert(child->key->getString(runtime).utf8(runtime) == "alpha");
  assert(!child->hasStaticChildren);
  assert(child->props.isObject());
  auto childPropsView = child->props.getObject(runtime);
  auto classNameValue = childPropsView.getProperty(runtime, "className");
  assert(classNameValue.isString());
  assert(classNameValue.getString(runtime).utf8(runtime) == "chip");

  jsi::Object rootProps(runtime);
  rootProps.setProperty(runtime, "id", makeStringValue("root"));
  rootProps.setProperty(runtime, "__source", makeStringValue("ignored"));
  rootProps.setProperty(runtime, "__self", makeStringValue("ignoredSelf"));

  auto childrenArray = runtime.makeArray(1);
  childrenArray.setValueAtIndex(runtime, 0, createJsxHostValue(runtime, child));
  rootProps.setProperty(runtime, "children", jsi::Value(runtime, childrenArray));

  auto root = jsxs(runtime, makeStringValue("div"), jsi::Value(runtime, rootProps));
  assert(root != nullptr);
  assert(root->hasStaticChildren);
  assert(root->props.isObject());
  auto rootPropsView = root->props.getObject(runtime);
  assert(rootPropsView.hasProperty(runtime, "id"));
  assert(!rootPropsView.hasProperty(runtime, "__source"));
  assert(!rootPropsView.hasProperty(runtime, "__self"));

  auto layout = serializeToWasm(runtime, *root);
  assert(layout.rootOffset != 0);
  assert(!layout.buffer.empty());

  const auto* base = layout.buffer.data();
  const auto* rootElement = reinterpret_cast<const WasmReactElement*>(base + layout.rootOffset);
  assert(rootElement->children_count == 1);
  assert(rootElement->props_count == 1);

  const auto* divType = reinterpret_cast<const char*>(base + rootElement->type_name_ptr);
  assert(std::strcmp(divType, "div") == 0);

  const auto* rootPropsPtr = reinterpret_cast<const WasmReactProp*>(base + rootElement->props_ptr);
  assert(rootPropsPtr->value.type == WasmValueType::String);
  const auto* idString = reinterpret_cast<const char*>(base + rootPropsPtr->value.data.ptrValue);
  assert(std::strcmp(idString, "root") == 0);

  const auto* childValue = reinterpret_cast<const WasmReactValue*>(base + rootElement->children_ptr);
  assert(childValue->type == WasmValueType::Element);

  const auto* childElement = reinterpret_cast<const WasmReactElement*>(base + childValue->data.ptrValue);
  assert(childElement->props_count == 1);
  assert(childElement->children_count == 1);

  const auto* classProp = reinterpret_cast<const WasmReactProp*>(base + childElement->props_ptr);
  assert(classProp->value.type == WasmValueType::String);
  const auto* classValue = reinterpret_cast<const char*>(base + classProp->value.data.ptrValue);
  assert(std::strcmp(classValue, "chip") == 0);

  const auto* textChild = reinterpret_cast<const WasmReactValue*>(base + childElement->children_ptr);
  assert(textChild->type == WasmValueType::String);
  const auto* textValue = reinterpret_cast<const char*>(base + textChild->data.ptrValue);
  assert(std::strcmp(textValue, "Alpha") == 0);

  jsi::Object devConfig(runtime);
  devConfig.setProperty(runtime, "className", makeStringValue("chip"));
  devConfig.setProperty(runtime, "children", makeStringValue("Beta"));
  devConfig.setProperty(runtime, "key", makeStringValue("beta"));
  devConfig.setProperty(runtime, "ref", makeStringValue("shouldBeReplaced"));

  SourceLocation location{"App.jsx", 42, 7};
  auto devElement = jsxDEV(
      runtime,
      makeStringValue("span"),
      jsi::Value(runtime, devConfig),
      std::nullopt,
      location,
      std::optional<jsi::Value>(makeStringValue("ref")));
  assert(devElement->key.has_value());
  assert(devElement->key->getString(runtime).utf8(runtime) == "beta");
  assert(devElement->ref.has_value());
  assert(devElement->ref->isString());
  assert(devElement->ref->getString(runtime).utf8(runtime) == "ref");
  assert(devElement->source.has_value());
  assert(devElement->source->fileName == "App.jsx");
  assert(!devElement->hasStaticChildren);
  auto devPropsView = devElement->props.getObject(runtime);
  assert(devPropsView.hasProperty(runtime, "className"));
  assert(!devPropsView.hasProperty(runtime, "key"));
  assert(!devPropsView.hasProperty(runtime, "ref"));

  return true;
}
} // namespace react::test
