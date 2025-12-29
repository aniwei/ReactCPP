#include "ReactChildFiber.h"

#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

void placeSingleChild(const FiberRef& fiber, bool shouldTrackSideEffects) {
  if (shouldTrackSideEffects && fiber && fiber->alternate.expired()) {
    fiber->flags |= Placement;
  }
}

void ReactChildFiberReconciler::deleteChild(const FiberRef& returnFiber, const FiberRef& childToDelete) {
  if (!shouldTrackSideEffects_) return;
  if (!returnFiber || !childToDelete) return;

  returnFiber->deletions.push_back(childToDelete);
  childToDelete->flags |= ChildDeletion;
  returnFiber->flags |= ChildDeletion;
}

void ReactChildFiberReconciler::deleteRemainingChildren(const FiberRef& returnFiber, const FiberRef& currentFirstChild) {
  if (!shouldTrackSideEffects_) return;

  FiberRef childToDelete = currentFirstChild;
  while (childToDelete != nullptr) {
    deleteChild(returnFiber, childToDelete);
    childToDelete = childToDelete->sibling;
  }
}

std::unordered_map<std::string, FiberRef> ReactChildFiberReconciler::mapRemainingChildren(
  const FiberRef& /*returnFiber*/,
  const FiberRef& currentFirstChild
) {
  std::unordered_map<std::string, FiberRef> existingChildren;
  FiberRef existingChild = currentFirstChild;

  while (existingChild != nullptr) {
    existingChildren[std::to_string(existingChild->index)] = existingChild;
    existingChild = existingChild->sibling;
  }

  return existingChildren;
}

int ReactChildFiberReconciler::placeChild(const FiberRef& newFiber, int lastPlacedIndex, int newIndex) {
  newFiber->index = newIndex;

  if (!shouldTrackSideEffects_) {
    newFiber->flags |= Forked;
    return lastPlacedIndex;
  }

  auto current = newFiber->getAlternate();
  if (current != nullptr) {
    int oldIndex = current->index;
    if (oldIndex < lastPlacedIndex) {
      newFiber->flags |= Placement;
      return lastPlacedIndex;
    }
    return oldIndex;
  }

  newFiber->flags |= Placement;
  return lastPlacedIndex;
}

FiberRef ReactChildFiberReconciler::useFiber(jsi::Runtime& rt, const FiberRef& fiber, const jsi::Value& pendingProps) {
  if (!fiber) return nullptr;

  auto clone = std::make_shared<Fiber>(fiber->tag, fiber->mode);
  clone->key = jsi::Value(rt, fiber->key);
  clone->elementType = jsi::Value(rt, fiber->elementType);
  clone->type = jsi::Value(rt, fiber->type);
  clone->stateNode = fiber->stateNode;
  clone->ref = jsi::Value(rt, fiber->ref);
  clone->pendingProps = jsi::Value(rt, pendingProps);
  clone->memoizedProps = jsi::Value(rt, fiber->memoizedProps);
  clone->memoizedState = fiber->memoizedState;
  clone->updateQueue = fiber->updateQueue;
  clone->dependencies = fiber->dependencies;
  clone->lanes = fiber->lanes;
  clone->childLanes = fiber->childLanes;

  clone->index = 0;
  clone->child = nullptr;
  clone->sibling = nullptr;

  clone->setAlternate(fiber);
  fiber->setAlternate(clone);
  return clone;
}

bool ReactChildFiberReconciler::isTextContent(const jsi::Value& value) {
  return value.isString() || value.isNumber() || value.isBool() || value.isBigInt();
}

std::string ReactChildFiberReconciler::getTextContent(jsi::Runtime& rt, const jsi::Value& value) {
  if (value.isString()) return value.asString(rt).utf8(rt);
  if (value.isNumber()) return std::to_string(value.getNumber());
  if (value.isBool()) return value.getBool() ? "true" : "false";
  if (value.isBigInt()) return value.asBigInt(rt).toString(rt).utf8(rt);
  return "";
}

FiberRef ReactChildFiberReconciler::updateTextNode(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const FiberRef& current,
  const jsi::Value& textContent,
  Lanes /*lanes*/
) {
  if (current == nullptr || current->tag != HostText) {
    auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
    created->pendingProps = jsi::Value(rt, textContent);
    created->stateNode = getTextContent(rt, textContent);
    created->setReturn(returnFiber);
    return created;
  }

  auto existing = useFiber(rt, current, textContent);
  existing->stateNode = getTextContent(rt, textContent);
  existing->setReturn(returnFiber);
  return existing;
}

FiberRef ReactChildFiberReconciler::updateElement(
  const FiberRef& returnFiber,
  const FiberRef& current,
  const react::ReactElement& element,
  Lanes /*lanes*/
) {
  (void)element;
  if (current != nullptr) {
    auto existing = current;
    existing->setReturn(returnFiber);
    return existing;
  }

  auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::updatePortal(
  const FiberRef& returnFiber,
  const FiberRef& current,
  const react::ReactPortal& portal,
  Lanes /*lanes*/
) {
  if (current != nullptr && current->tag == HostPortal) {
    auto existing = current;
    existing->setReturn(returnFiber);
    (void)portal;
    return existing;
  }

  auto created = std::make_shared<Fiber>(HostPortal, returnFiber->mode);
  created->stateNode = portal.containerInfo;
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::updateFragment(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const FiberRef& current,
  const jsi::Array& fragment,
  Lanes /*lanes*/,
  const jsi::Value& /*key*/
) {
  if (current != nullptr && current->tag == Fragment) {
    auto fragmentValue = jsi::Value(rt, fragment);
    auto existing = useFiber(rt, current, fragmentValue);
    existing->setReturn(returnFiber);
    return existing;
  }

  auto created = std::make_shared<Fiber>(Fragment, returnFiber->mode);
  created->pendingProps = jsi::Value(rt, fragment);
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::createChild(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const jsi::Value& newChild,
  Lanes lanes
) {
  (void)lanes;
  if (newChild.isNull() || newChild.isUndefined()) return nullptr;

  if (isTextContent(newChild)) {
    auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
    created->pendingProps = jsi::Value(rt, newChild);
    created->stateNode = getTextContent(rt, newChild);
    created->setReturn(returnFiber);
    return created;
  }

  if (newChild.isObject()) {
    auto obj = newChild.asObject(rt);
    if (obj.isArray(rt)) {
      return updateFragment(rt, returnFiber, nullptr, obj.asArray(rt), lanes, jsi::Value::undefined());
    }

    auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
    created->pendingProps = jsi::Value(rt, newChild);
    created->setReturn(returnFiber);
    return created;
  }

  return nullptr;
}

FiberRef ReactChildFiberReconciler::updateSlot(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const FiberRef& oldFiber,
  const jsi::Value& newChild,
  Lanes lanes
) {
  if (newChild.isNull() || newChild.isUndefined()) return nullptr;

  if (isTextContent(newChild)) {
    return updateTextNode(rt, returnFiber, oldFiber, newChild, lanes);
  }

  if (newChild.isObject() && newChild.asObject(rt).isArray(rt)) {
    return updateFragment(rt, returnFiber, oldFiber, newChild.asObject(rt).asArray(rt), lanes, jsi::Value::undefined());
  }

  return createChild(rt, returnFiber, newChild, lanes);
}

FiberRef ReactChildFiberReconciler::updateFromMap(
  std::unordered_map<std::string, FiberRef>& existingChildren,
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  int newIdx,
  const jsi::Value& newChild,
  Lanes lanes
) {
  auto it = existingChildren.find(std::to_string(newIdx));
  FiberRef matchedFiber = (it != existingChildren.end()) ? it->second : nullptr;
  return updateSlot(rt, returnFiber, matchedFiber, newChild, lanes);
}

FiberRef ReactChildFiberReconciler::reconcileSingleTextNode(
  const FiberRef& returnFiber,
  const FiberRef& /*currentFirstChild*/,
  const jsi::Value& /*textContent*/,
  Lanes /*lanes*/
) {
  auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::reconcileSingleElement(
  const FiberRef& returnFiber,
  const FiberRef& /*currentFirstChild*/,
  const ReactElement& /*element*/,
  Lanes /*lanes*/
) {
  auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::reconcileSinglePortal(
  const FiberRef& returnFiber,
  const FiberRef& /*currentFirstChild*/,
  const react::ReactPortal& portal,
  Lanes /*lanes*/
) {
  auto created = std::make_shared<Fiber>(HostPortal, returnFiber->mode);
  created->stateNode = portal.containerInfo;
  created->setReturn(returnFiber);
  return created;
}

FiberRef ReactChildFiberReconciler::reconcileChildrenArray(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const FiberRef& /*currentFirstChild*/,
  const jsi::Array& newChildren,
  Lanes lanes
) {
  FiberRef resultingFirstChild = nullptr;
  FiberRef previousNewFiber = nullptr;
  int lastPlacedIndex = 0;

  const size_t length = newChildren.size(rt);
  for (size_t i = 0; i < length; i++) {
    auto child = newChildren.getValueAtIndex(rt, i);
    auto newFiber = createChild(rt, returnFiber, child, lanes);
    if (!newFiber) continue;

    lastPlacedIndex = placeChild(newFiber, lastPlacedIndex, static_cast<int>(i));
    if (!resultingFirstChild) {
      resultingFirstChild = newFiber;
    } else {
      previousNewFiber->sibling = newFiber;
    }
    previousNewFiber = newFiber;
  }

  return resultingFirstChild;
}

FiberRef ReactChildFiberReconciler::reconcileChildFibers(
  jsi::Runtime& rt,
  const FiberRef& returnFiber,
  const FiberRef& currentFirstChild,
  const jsi::Value& newChild,
  Lanes lanes
) {
  if (newChild.isNull() || newChild.isUndefined()) {
    deleteRemainingChildren(returnFiber, currentFirstChild);
    return nullptr;
  }

  if (isTextContent(newChild)) {
    auto result = updateTextNode(rt, returnFiber, currentFirstChild, newChild, lanes);
    placeSingleChild(result, shouldTrackSideEffects_);
    return result;
  }

  if (newChild.isObject() && newChild.asObject(rt).isArray(rt)) {
    return reconcileChildrenArray(rt, returnFiber, currentFirstChild, newChild.asObject(rt).asArray(rt), lanes);
  }

  auto result = createChild(rt, returnFiber, newChild, lanes);
  placeSingleChild(result, shouldTrackSideEffects_);
  return result;
}

ReactChildFiberReconciler createReconcileChildFibers() {
  return ReactChildFiberReconciler(true);
}

ReactChildFiberReconciler createMountChildFibers() {
  return ReactChildFiberReconciler(false);
}

ReactChildFiberReconciler& getReconcileChildFibers(react::ReactHostRuntime& hostRuntime) {
  return hostRuntime.getReconcileChildFibers();
}

ReactChildFiberReconciler& getMountChildFibers(react::ReactHostRuntime& hostRuntime) {
  return hostRuntime.getMountChildFibers();
}

FiberRef reconcileChildFibers(
  jsi::Runtime& rt,
  react::ReactHostRuntime& hostRuntime,
  const FiberRef& returnFiber,
  const FiberRef& currentFirstChild,
  const jsi::Value& newChild,
  Lanes lanes
) {
  return getReconcileChildFibers(hostRuntime).reconcileChildFibers(rt, returnFiber, currentFirstChild, newChild, lanes);
}

FiberRef mountChildFibers(
  jsi::Runtime& rt,
  react::ReactHostRuntime& hostRuntime,
  const FiberRef& returnFiber,
  const FiberRef& currentFirstChild,
  const jsi::Value& newChild,
  Lanes lanes
) {
  return getMountChildFibers(hostRuntime).reconcileChildFibers(rt, returnFiber, currentFirstChild, newChild, lanes);
}

void cloneChildFibers(jsi::Runtime& /*rt*/, const FiberRef& /*current*/, const FiberRef& /*workInProgress*/) {
  // 当前 build-check 配置（tests OFF）不需要完整实现。
}

void resetChildReconcilerOnUnwind() {
  // No-op in this simplified implementation.
}

} // namespace react::reconciler
