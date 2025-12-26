#pragma once

#include "ReactDOM/client/ReactDOMInstance.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactWorkTags.h"

#include "jsi/jsi.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace react {

struct FiberNode {
  FiberNode(
    WorkTag tagValue,
    facebook::jsi::Value pendingPropsValue,
    facebook::jsi::Value keyValue)
    : tag(tagValue),
      key(std::move(keyValue)),
      elementType(facebook::jsi::Value::undefined()),
      type(facebook::jsi::Value::undefined()),
      stateNode(nullptr),
      returnFiber(nullptr),
      child(nullptr),
      sibling(nullptr),
      index(0),
      ref(facebook::jsi::Value::undefined()),
      refCleanup(facebook::jsi::Value::undefined()),
      pendingProps(std::move(pendingPropsValue)),
      memoizedProps(facebook::jsi::Value::undefined()),
      updateQueue(nullptr),
      memoizedState(facebook::jsi::Value::undefined()),
      dependencies(nullptr),
      mode(0),
      flags(NoFlags),
      subtreeFlags(NoFlags),
      lanes(0),
      childLanes(0),
      alternate(nullptr),
      actualDuration(std::nullopt),
      actualStartTime(std::nullopt),
      selfBaseDuration(std::nullopt),
      treeBaseDuration(std::nullopt),
      _debugInfo(facebook::jsi::Value::undefined()),
      _debugOwner(facebook::jsi::Value::undefined()),
      _debugStack(facebook::jsi::Value::undefined()),
      _debugTask(facebook::jsi::Value::undefined()),
      _debugNeedsRemount(false),
      _debugHookTypes(facebook::jsi::Value::undefined()),
      updatePayload(facebook::jsi::Value::undefined()) {}

  WorkTag tag;
  facebook::jsi::Value key;
  facebook::jsi::Value elementType;
  facebook::jsi::Value type;
  std::shared_ptr<ReactDOMInstance> stateNode;

  std::shared_ptr<FiberNode> returnFiber;
  std::shared_ptr<FiberNode> child;
  std::shared_ptr<FiberNode> sibling;
  std::uint32_t index;

  facebook::jsi::Value ref;
  facebook::jsi::Value refCleanup;

  facebook::jsi::Value pendingProps;
  facebook::jsi::Value memoizedProps;
  void* updateQueue;
  facebook::jsi::Value memoizedState;

  // Matches ReactJS `dependencies: Dependencies | null`
  void* dependencies;

  // Matches ReactJS `mode: TypeOfMode`
  std::uint32_t mode;

  FiberFlags flags;
  FiberFlags subtreeFlags;
  std::vector<std::shared_ptr<FiberNode>> deletions;

  // Matches ReactJS `lanes: Lanes` / `childLanes: Lanes`
  std::uint32_t lanes;
  std::uint32_t childLanes;

  std::shared_ptr<FiberNode> alternate;

  // Profiler fields (optional in ReactJS types)
  std::optional<double> actualDuration;
  std::optional<double> actualStartTime;
  std::optional<double> selfBaseDuration;
  std::optional<double> treeBaseDuration;

  // DEV-only fields in ReactJS types (kept unconditionally here to satisfy 1:1 field parity)
  facebook::jsi::Value _debugInfo;
  facebook::jsi::Value _debugOwner;
  facebook::jsi::Value _debugStack;
  facebook::jsi::Value _debugTask;
  bool _debugNeedsRemount;
  facebook::jsi::Value _debugHookTypes;

  facebook::jsi::Value updatePayload;
};

} // namespace react
