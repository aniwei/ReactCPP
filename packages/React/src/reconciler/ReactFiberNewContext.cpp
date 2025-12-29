#include "ReactFiberNewContext.h"

#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

void ReactFiberNewContext::resetContextDependencies() {
  currentlyRenderingFiber_ = nullptr;
  lastContextDependency_ = nullptr;
}

bool ReactFiberNewContext::checkIfContextChanged(DependenciesRef dependencies) {
  if (dependencies == nullptr) {
    return false;
  }

  auto dependency = dependencies->firstContext;
  while (dependency != nullptr) {
    // 比较当前值和 memoized 值
    // 由于类型擦除，需要在具体使用时实现比较
    dependency = dependency->next;
  }

  return false;
}

void ReactFiberNewContext::scheduleContextWorkOnParentPath(const FiberRef& parent, Lanes renderLanes) {
  FiberRef node = parent;
  while (node != nullptr) {
    auto alternate = node->alternate;

    if (!isSubsetOfLanes(node->childLanes, renderLanes)) {
      node->childLanes = mergeLanes(node->childLanes, renderLanes);
      if (!alternate.expired()) {
        auto alt = alternate.lock();
        if (alt != nullptr) {
          alt->childLanes = mergeLanes(alt->childLanes, renderLanes);
        }
      }
    } else if (!alternate.expired()) {
      auto alt = alternate.lock();
      if (alt != nullptr && !isSubsetOfLanes(alt->childLanes, renderLanes)) {
        alt->childLanes = mergeLanes(alt->childLanes, renderLanes);
      } else {
        break;
      }
    } else {
      break;
    }

    if (!node->return_.expired()) {
      node = node->return_.lock();
    } else {
      break;
    }
  }
}

void ReactFiberNewContext::propagateContextChange(const FiberRef& workInProgress, std::any /*context*/, Lanes /*renderLanes*/) {
  // 遍历子树查找消费者
  FiberRef fiber = workInProgress->child;
  if (fiber != nullptr) {
    fiber->return_ = workInProgress;
  }

  while (fiber != nullptr) {
    FiberRef nextFiber = nullptr;

    // 检查这个 fiber 是否依赖 context
    if (fiber->dependencies != nullptr) {
      auto dep = fiber->dependencies->firstContext;

      while (dep != nullptr) {
        // 比较 context 引用
        // 需要类型擦除后的比较
        dep = dep->next;
      }
    }

    // 继续遍历
    if (fiber->child != nullptr) {
      nextFiber = fiber->child;
      nextFiber->return_ = fiber;
    }

    if (nextFiber == nullptr) {
      nextFiber = fiber;
      while (nextFiber != nullptr) {
        if (nextFiber == workInProgress) {
          nextFiber = nullptr;
          break;
        }
        auto sibling = nextFiber->sibling;
        if (sibling != nullptr) {
          sibling->return_ = nextFiber->return_;
          nextFiber = sibling;
          break;
        }
        if (!nextFiber->return_.expired()) {
          nextFiber = nextFiber->return_.lock();
        } else {
          nextFiber = nullptr;
        }
      }
    }

    fiber = nextFiber;
  }
}

void ReactFiberNewContext::prepareToReadContext(const FiberRef& workInProgress, Lanes /*renderLanes*/) {
  currentlyRenderingFiber_ = workInProgress;
  lastContextDependency_ = nullptr;

  if (workInProgress->dependencies != nullptr) {
    workInProgress->dependencies->firstContext = nullptr;
  }
}

FiberRef ReactFiberNewContext::getCurrentlyRenderingFiber() const {
  return currentlyRenderingFiber_;
}

ReactFiberNewContext& getContextModule(react::ReactHostRuntime& hostRuntime) {
  return hostRuntime.getFiberNewContext();
}

} // namespace react::reconciler
