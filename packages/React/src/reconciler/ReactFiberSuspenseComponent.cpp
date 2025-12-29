/**
 * React Fiber Suspense Component
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseComponent.js
 */

#include "ReactFiberSuspenseComponent.h"

namespace react::reconciler {

bool isSuspenseInstancePending(const SuspenseInstance&) {
  // 简化实现 - 实际需要根据 host config 实现
  return false;
}

bool isSuspenseInstanceFallback(const SuspenseInstance&) {
  // 简化实现
  return false;
}

FiberRef findFirstSuspended(FiberRef row) {
  FiberRef node = row;

  while (node != nullptr) {
    if (node->tag == SuspenseComponent) {
      SuspenseState* statePtr = nullptr;

      auto* sharedState = std::any_cast<std::shared_ptr<SuspenseState>>(&node->memoizedState);
      if (sharedState && *sharedState) {
        statePtr = sharedState->get();
      } else {
        statePtr = std::any_cast<SuspenseState>(&node->memoizedState);
      }

      if (statePtr != nullptr) {
        auto& dehydrated = statePtr->dehydrated;
        if (!dehydrated.has_value() || isSuspenseInstancePending(dehydrated) ||
            isSuspenseInstanceFallback(dehydrated)) {
          return node;
        }
      }
    } else if (node->tag == SuspenseListComponent) {
      bool didSuspend = (node->flags & DidCapture) != NoFlags;
      if (didSuspend) {
        return node;
      }
    } else if (node->child != nullptr) {
      node->child->return_ = node;
      node = node->child;
      continue;
    }

    if (node == row) {
      node = node->sibling;
      continue;
    }

    if (node->sibling != nullptr) {
      node->sibling->return_ = node->return_;
      node = node->sibling;
      continue;
    }

    while (node->sibling == nullptr) {
      auto parent = node->return_.lock();
      if (!parent || parent == row) {
        return nullptr;
      }
      node = parent;
    }

    node->sibling->return_ = node->return_;
    node = node->sibling;
  }

  return nullptr;
}

} // namespace react::reconciler
