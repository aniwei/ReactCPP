#pragma once

#include "ReactDOM/client/ReactDOMInstance.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactWorkTags.h"

#include "jsi/jsi.h"

#include <cstdint>
#include <memory>
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
      flags(NoFlags),
      subtreeFlags(NoFlags),
      alternate(nullptr),
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

  FiberFlags flags;
  FiberFlags subtreeFlags;
  std::vector<std::shared_ptr<FiberNode>> deletions;

  std::shared_ptr<FiberNode> alternate;
  facebook::jsi::Value updatePayload;
};

} // namespace react
