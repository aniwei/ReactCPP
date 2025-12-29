#include "ReactFiber.h"

namespace react::reconciler {

Fiber::Fiber(WorkTag tag_, TypeOfMode mode_) : tag(tag_), mode(mode_) {}

Fiber::Fiber(jsi::Runtime& rt, WorkTag tag_, const jsi::Value& pendingProps_, const jsi::Value& key_, TypeOfMode mode_) 
: tag(tag_), 
  key(rt, key_), 
  pendingProps(rt, pendingProps_), 
  mode(mode_) {}

FiberRef Fiber::getReturn() const { return return_.lock(); }
void Fiber::setReturn(FiberRef parent) { return_ = parent; }

FiberRef Fiber::getAlternate() const { return alternate.lock(); }
void Fiber::setAlternate(FiberRef alt) { alternate = alt; }

bool Fiber::hasChild() const { return child != nullptr; }
bool Fiber::hasSibling() const { return sibling != nullptr; }
bool Fiber::hasDeletions() const { return !deletions.empty(); }

void Fiber::addDeletion(FiberRef fiber) {
  deletions.push_back(fiber);
  flags |= ChildDeletion;
}

void Fiber::clearDeletions() { deletions.clear(); }
bool Fiber::hasKey() const { return !key.isUndefined() && !key.isNull(); }

std::optional<std::string> Fiber::getKeyString(jsi::Runtime& rt) const {
  if (key.isString()) {
    return key.asString(rt).utf8(rt);
  }

  return std::nullopt;
}


FiberRef createFiber(
  jsi::Runtime& rt,
  WorkTag tag,
  const jsi::Value& pendingProps,
  const jsi::Value& key,
  TypeOfMode mode) {
  return std::make_shared<Fiber>(rt, tag, pendingProps, key, mode);
}

FiberRef createHostRootFiber(
  RootTag tag, 
  bool isStrictMode, 
  bool) {
  TypeOfMode mode = NoMode;

  if (tag == ConcurrentRoot) {
    mode = ConcurrentMode;
    if (isStrictMode) {
      mode |= StrictLegacyMode | StrictEffectsMode;
    }
  }

  return std::make_shared<Fiber>(HostRoot, mode);
}

FiberRef createWorkInProgress(
  jsi::Runtime& rt, 
  const FiberRef& current, 
  const jsi::Value& pendingProps) {
  FiberRef workInProgress = current->getAlternate();

  if (workInProgress == nullptr) {
    workInProgress = createFiber(rt, current->tag, pendingProps, current->key, current->mode);

    workInProgress->elementType = jsi::Value(rt, current->elementType);
    workInProgress->type = jsi::Value(rt, current->type);
    workInProgress->stateNode = current->stateNode;

    workInProgress->setAlternate(current);
    current->setAlternate(workInProgress);
  } else {
    workInProgress->pendingProps = jsi::Value(rt, pendingProps);
    workInProgress->type = jsi::Value(rt, current->type);

    workInProgress->flags = NoFlags;
    workInProgress->subtreeFlags = NoFlags;
    workInProgress->clearDeletions();
  }

  workInProgress->child = current->child;
  workInProgress->memoizedProps = jsi::Value(rt, current->memoizedProps);
  workInProgress->memoizedState = current->memoizedState;
  workInProgress->updateQueue = current->updateQueue;
  workInProgress->dependencies = current->dependencies;

  workInProgress->lanes = current->lanes;
  workInProgress->childLanes = current->childLanes;

  workInProgress->selfBaseDuration = current->selfBaseDuration;
  workInProgress->treeBaseDuration = current->treeBaseDuration;

  return workInProgress;
}

void resetWorkInProgress(
  const FiberRef& workInProgress, 
  Lanes renderLanes) {
  workInProgress->flags &= StaticMask | Placement;
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->clearDeletions();

  workInProgress->lanes = renderLanes;
  workInProgress->childLanes = NoLanes;
}

} // namespace react::reconciler
