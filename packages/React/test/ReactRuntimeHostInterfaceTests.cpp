#include "ReactDOM/client/ReactDOMComponent.h"
#include "ReactRuntime/ReactHostInterface.h"
#include "ReactRuntime/ReactJSXRuntime.h"
#include "ReactRuntime/ReactRuntime.h"
#include "ReactRuntime/ReactWasmLayout.h"
#include "TestRuntime.h"

#include <cassert>
#include <memory>
#include <vector>

namespace react {
extern uint8_t* __wasm_memory_buffer;
}

namespace react::test {

namespace jsi = facebook::jsi;

namespace {

struct RenderFixture {
  std::vector<uint8_t> buffer;
  uint32_t offset{0};
};

jsi::Value makeStringValue(jsi::Runtime& runtime, const std::string& value) {
  return jsi::Value(runtime, jsi::String::createFromUtf8(runtime, value));
}

RenderFixture buildLayout(
    jsi::Runtime& runtime,
    const std::string& childClassName,
    const std::string& textContent) {
  using namespace react::jsx;

  jsi::Object childProps(runtime);
  childProps.setProperty(runtime, "className", makeStringValue(runtime, childClassName));
  childProps.setProperty(runtime, "children", makeStringValue(runtime, textContent));

  auto childElement = jsx(runtime, makeStringValue(runtime, "span"), jsi::Value(runtime, childProps));

  jsi::Object rootProps(runtime);
  rootProps.setProperty(runtime, "id", makeStringValue(runtime, "root"));
  auto childrenArray = runtime.makeArray(1);
  childrenArray.setValueAtIndex(runtime, 0, createJsxHostValue(runtime, childElement));
  rootProps.setProperty(runtime, "children", jsi::Value(runtime, childrenArray));

  auto rootElement = jsxs(runtime, makeStringValue(runtime, "div"), jsi::Value(runtime, rootProps));
  auto layout = serializeToWasm(runtime, *rootElement);

  RenderFixture fixture;
  fixture.offset = layout.rootOffset;
  fixture.buffer = std::move(layout.buffer);
  return fixture;
}

std::shared_ptr<ReactDOMComponent> asComponent(const std::shared_ptr<ReactDOMInstance>& instance) {
  return std::dynamic_pointer_cast<ReactDOMComponent>(instance);
}

} // namespace

bool runReactRuntimeHostInterfaceTests() {
  TestRuntime runtime;

  auto hostInterface = std::make_shared<HostInterface>();
  ReactRuntime reactRuntime;
  reactRuntime.setHostInterface(hostInterface);
  reactRuntime.bindHostInterface(runtime);

  jsi::Object rootProps(runtime);
  auto rootContainer = std::dynamic_pointer_cast<ReactDOMComponent>(
      hostInterface->createHostInstance(runtime, "__root", rootProps));
  assert(rootContainer);
  assert(rootContainer->children.empty());

  auto initialLayout = buildLayout(runtime, "chip", "Hello");
  assert(!initialLayout.buffer.empty());
  react::__wasm_memory_buffer = initialLayout.buffer.data();
  reactRuntime.renderRootSync(runtime, initialLayout.offset, rootContainer);

  assert(rootContainer->children.size() == 1);
  auto rootChild = asComponent(rootContainer->children.front());
  assert(rootChild);
  assert(rootChild->getType() == "div");
  assert(rootChild->children.size() == 1);
  auto spanNode = asComponent(rootChild->children.front());
  assert(spanNode);
  assert(spanNode->getType() == "span");
  assert(!spanNode->isTextInstance());
  assert(spanNode->children.size() == 1);
  auto textNode = asComponent(spanNode->children.front());
  assert(textNode);
  assert(textNode->isTextInstance());
  assert(textNode->getTextContent() == "Hello");

  const auto& spanProps = spanNode->getProps();
  auto classIt = spanProps.find("className");
  assert(classIt != spanProps.end());
  assert(classIt->second.isString());
  assert(classIt->second.getString(runtime).utf8(runtime) == "chip");

  auto updatedLayout = buildLayout(runtime, "card", "World");
  assert(!updatedLayout.buffer.empty());
  react::__wasm_memory_buffer = updatedLayout.buffer.data();
  reactRuntime.renderRootSync(runtime, updatedLayout.offset, rootContainer);

  assert(rootContainer->children.size() == 1);
  rootChild = asComponent(rootContainer->children.front());
  assert(rootChild);
  assert(rootChild->children.size() == 1);
  spanNode = asComponent(rootChild->children.front());
  assert(spanNode);
  assert(spanNode->children.size() == 1);
  textNode = asComponent(spanNode->children.front());
  assert(textNode);
  assert(textNode->getTextContent() == "World");

  const auto& updatedProps = spanNode->getProps();
  auto updatedIt = updatedProps.find("className");
  assert(updatedIt != updatedProps.end());
  assert(updatedIt->second.getString(runtime).utf8(runtime) == "card");

  reactRuntime.renderRootSync(runtime, 0, rootContainer);
  assert(rootContainer->children.empty());

  return true;
}

} // namespace react::test
