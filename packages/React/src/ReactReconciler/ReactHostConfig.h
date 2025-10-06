#pragma once

#include "ReactDOM/client/ReactDOMInstance.h"
#include "ReactRuntime/ReactRuntime.h"

#include "jsi/jsi.h"

#include <memory>
#include <string>

namespace react::hostconfig {

using HostInstance = std::shared_ptr<ReactDOMInstance>;
using HostTextInstance = std::shared_ptr<ReactDOMInstance>;
using HostContainer = std::shared_ptr<ReactDOMInstance>;
using UpdatePayload = facebook::jsi::Value;

HostInstance createInstance(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const std::string& type,
  const facebook::jsi::Object& props);

HostTextInstance createTextInstance(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const std::string& text);

void appendInitialChild(
  ReactRuntime& runtime,
  const HostInstance& parent,
  const HostInstance& child);

void appendChild(
  ReactRuntime& runtime,
  const HostInstance& parent,
  const HostInstance& child);

void appendChildToContainer(
  ReactRuntime& runtime,
  const HostContainer& container,
  const HostInstance& child);

void insertBefore(
  ReactRuntime& runtime,
  const HostInstance& parent,
  const HostInstance& child,
  const HostInstance& beforeChild);

void insertInContainerBefore(
  ReactRuntime& runtime,
  const HostContainer& container,
  const HostInstance& child,
  const HostInstance& beforeChild);

void removeChild(
  ReactRuntime& runtime,
  const HostInstance& parent,
  const HostInstance& child);

void removeChildFromContainer(
  ReactRuntime& runtime,
  const HostContainer& container,
  const HostInstance& child);

bool finalizeInitialChildren(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const HostInstance& instance,
  const std::string& type,
  const facebook::jsi::Object& props);

bool shouldSetTextContent(
  facebook::jsi::Runtime& jsRuntime,
  const std::string& type,
  const facebook::jsi::Object& props);

UpdatePayload prepareUpdate(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const facebook::jsi::Value& prevProps,
  const facebook::jsi::Value& nextProps,
  bool isTextNode);

void commitUpdate(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const HostInstance& instance,
  const facebook::jsi::Value& prevProps,
  const facebook::jsi::Value& nextProps,
  const UpdatePayload& payload);

void commitTextUpdate(
  ReactRuntime& runtime,
  const HostTextInstance& textInstance,
  const std::string& oldText,
  const std::string& newText);

void resetAfterCommit(ReactRuntime& runtime);

} // namespace react::hostconfig
