/**
 * React Fiber 节点
 *
 * Fiber 是 React Reconciler 的核心数据结构
 * 每个 Fiber 代表一个组件实例或 DOM 节点
 *
 * @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:83-207
 * @source reactjs/packages/react-reconciler/src/ReactFiber.js:131-214
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <any>
#include <optional>

#include "ReactWorkTags.h"
#include "ReactFiberFlags.h"
#include "ReactFiberLane.h"
#include "ReactTypeOfMode.h"
#include "ReactRootTags.h"

namespace react::reconciler {

using namespace facebook;

// 前向声明
struct Fiber;
struct FiberRoot;
struct Hook;
struct UpdateQueue;
struct Dependencies;

// =============================================================================
// Fiber 引用类型
// =============================================================================

using FiberRef = std::shared_ptr<Fiber>;
using FiberWeakRef = std::weak_ptr<Fiber>;
using FiberRootRef = std::shared_ptr<FiberRoot>;

// =============================================================================
// RefObject 类型
// @source reactjs/packages/shared/ReactTypes.js
// =============================================================================

template<typename T>
struct RefObject {
  T current;
};

// =============================================================================
// Dependencies 类型
// @source reactjs/packages/react-reconciler/src/ReactInternalTypes.js:66-73
// =============================================================================

struct ContextDependency {
  std::any context;
  std::shared_ptr<ContextDependency> next;
  std::any memoizedValue;
};

struct Dependencies {
  // @source:67
  Lanes lanes = NoLanes;
  // @source:68
  std::shared_ptr<ContextDependency> firstContext = nullptr;
};

struct MemoCache {
  std::vector<std::vector<std::any>> data;
  size_t index = 0;
};


struct Fiber : std::enable_shared_from_this<Fiber> {
  WorkTag tag = FunctionComponent;
  jsi::Value key = jsi::Value::undefined();
  jsi::Value elementType = jsi::Value::undefined();
  jsi::Value type = jsi::Value::undefined();

  // reactjs 对齐：stateNode 的实际类型取决于 tag。
  // - HostComponent/HostText/HostSingleton 等：宿主 Instance（见 runtime/ReactDOMInstance.h）
  // - HostRoot：FiberRootRef
  // - HostPortal：portal.containerInfo（通常是宿主 Container）
  // - ClassComponent：组件实例
  // 目前使用 std::any 以允许不同 renderer/测试替换；建议 Host* tag 存放 react::ReactDOMInstance* 派生指针。
  std::any stateNode;

  FiberWeakRef return_;

  // @source:116 Singly Linked List Tree Structure
  FiberRef child = nullptr;

  // @source:117
  FiberRef sibling = nullptr;

  // @source:118
  int index = 0;

  // @source:123 The ref last used to attach this node
  jsi::Value ref = jsi::Value::undefined();

  // @source:126
  std::function<void()> refCleanup = nullptr;

  // @source:128 Input is the data coming into process this fiber (props)
  jsi::Value pendingProps = jsi::Value::undefined();

  // @source:129 The props used to create the output
  jsi::Value memoizedProps = jsi::Value::undefined();

  // @source:132 A queue of state updates and callbacks
  // 使用 std::any 支持不同类型的队列（UpdateQueue 或 FunctionComponentUpdateQueue）
  std::any updateQueue;

  // @source:135 The state used to create the output
  std::any memoizedState;

  // @source:151 Dependencies (contexts, events) for this fiber
  std::shared_ptr<Dependencies> dependencies = nullptr;

  // @source:160 Mode flags
  TypeOfMode mode = NoMode;

  // ===========================================================================
  // Effect 字段
  // ===========================================================================

  // @source:163
  Flags flags = NoFlags;

  // @source:164
  Flags subtreeFlags = NoFlags;

  // @source:165
  std::vector<FiberRef> deletions;

  // ===========================================================================
  // Lane 字段
  // ===========================================================================

  // @source:167
  Lanes lanes = NoLanes;

  // @source:168
  Lanes childLanes = NoLanes;

  // ===========================================================================
  // 双缓冲
  // ===========================================================================

  // @source:173 Alternate fiber for double buffering
  FiberWeakRef alternate;

  // ===========================================================================
  // Profiler 字段 (enableProfilerTimer)
  // @source:177-195
  // ===========================================================================

  // @source:182 Time spent rendering this Fiber
  double actualDuration = 0.0;

  // @source:187 Time at which work began
  double actualStartTime = -1.0;

  // @source:192 Duration of the most recent render time
  double selfBaseDuration = 0.0;

  // @source:196 Sum of base times for all descendants
  double treeBaseDuration = 0.0;

  // ===========================================================================
  // Debug 字段 (DEV only)
  // ===========================================================================

#ifdef DEV
  // @source:200
  std::any _debugInfo;
  // @source:201
  std::any _debugOwner;
  // @source:202
  std::any _debugStack;
  // @source:203
  std::any _debugTask;
  // @source:204
  bool _debugNeedsRemount = false;
  // @source:207
  std::vector<std::string> _debugHookTypes;
#endif

  // ===========================================================================
  // 构造函数
  // ===========================================================================

  Fiber() = default;

  Fiber(WorkTag tag_, TypeOfMode mode_)
    : tag(tag_)
    , mode(mode_) {}

  // 带 Runtime 的构造函数（用于 JSI 值初始化）
  Fiber(
    jsi::Runtime& rt,
    WorkTag tag_,
    const jsi::Value& pendingProps_,
    const jsi::Value& key_,
    TypeOfMode mode_
  ) : tag(tag_)
    , key(rt, key_)
    , pendingProps(rt, pendingProps_)
    , mode(mode_) {}

  // ===========================================================================
  // 辅助方法
  // ===========================================================================

  /**
   * 获取父 Fiber
   */
  FiberRef getReturn() const {
    return return_.lock();
  }

  /**
   * 设置父 Fiber
   */
  void setReturn(FiberRef parent) {
    return_ = parent;
  }

  /**
   * 获取 alternate Fiber
   */
  FiberRef getAlternate() const {
    return alternate.lock();
  }

  /**
   * 设置 alternate Fiber
   */
  void setAlternate(FiberRef alt) {
    alternate = alt;
  }

  /**
   * 检查是否有子节点
   */
  bool hasChild() const {
    return child != nullptr;
  }

  /**
   * 检查是否有兄弟节点
   */
  bool hasSibling() const {
    return sibling != nullptr;
  }

  /**
   * 检查是否有待删除的节点
   */
  bool hasDeletions() const {
    return !deletions.empty();
  }

  /**
   * 添加待删除的子节点
   */
  void addDeletion(FiberRef fiber) {
    deletions.push_back(fiber);
    flags |= ChildDeletion;
  }

  /**
   * 清除待删除列表
   */
  void clearDeletions() {
    deletions.clear();
  }

  /**
   * 检查 key 是否有值
   */
  bool hasKey() const {
    return !key.isUndefined() && !key.isNull();
  }

  /**
   * 获取 key 字符串（如果存在）
   */
  std::optional<std::string> getKeyString(jsi::Runtime& rt) const {
    if (key.isString()) {
      return key.asString(rt).utf8(rt);
    }
    return std::nullopt;
  }

  /**
   * 复制 JSI 字段到另一个 Fiber
   */
  void copyJSIFieldsTo(jsi::Runtime& rt, FiberRef target) const {
    target->key = jsi::Value(rt, key);
    target->elementType = jsi::Value(rt, elementType);
    target->type = jsi::Value(rt, type);
    target->pendingProps = jsi::Value(rt, pendingProps);
    target->memoizedProps = jsi::Value(rt, memoizedProps);
    target->ref = jsi::Value(rt, ref);
  }
};

inline FiberRef createFiber(
  jsi::Runtime& rt,
  WorkTag tag,
  const jsi::Value& pendingProps,
  const jsi::Value& key,
  TypeOfMode mode
) {
  return std::make_shared<Fiber>(rt, tag, pendingProps, key, mode);
}

/**
 * 创建 Host Root Fiber
 * @source reactjs/packages/react-reconciler/src/ReactFiber.js:532-571
 */
inline FiberRef createHostRootFiber(
  RootTag tag,
  bool isStrictMode,
  bool
) {
  TypeOfMode mode = NoMode;

  if (tag == ConcurrentRoot) {
    mode = ConcurrentMode;
    if (isStrictMode) {
      mode |= StrictLegacyMode | StrictEffectsMode;
    }
  }

  return std::make_shared<Fiber>(HostRoot, mode);
}

inline FiberRef createWorkInProgress(
  jsi::Runtime& rt,
  FiberRef current,
  const jsi::Value& pendingProps
) {
  FiberRef workInProgress = current->getAlternate();

  if (workInProgress == nullptr) {
    // 创建新的 alternate
    workInProgress = createFiber(
      rt,
      current->tag,
      pendingProps,
      current->key,
      current->mode);

    // 复制 JSI 字段
    workInProgress->elementType = jsi::Value(rt, current->elementType);
    workInProgress->type = jsi::Value(rt, current->type);
    workInProgress->stateNode = current->stateNode;

    // 设置双向引用
    workInProgress->setAlternate(current);
    current->setAlternate(workInProgress);
  } else {
    // 复用现有的 alternate
    workInProgress->pendingProps = jsi::Value(rt, pendingProps);
    workInProgress->type = jsi::Value(rt, current->type);

    // 重置 effects
    workInProgress->flags = NoFlags;
    workInProgress->subtreeFlags = NoFlags;
    workInProgress->clearDeletions();
  }

  // 复制其他字段
  workInProgress->child = current->child;
  workInProgress->memoizedProps = jsi::Value(rt, current->memoizedProps);
  workInProgress->memoizedState = current->memoizedState;
  workInProgress->updateQueue = current->updateQueue;
  workInProgress->dependencies = current->dependencies;

  workInProgress->lanes = current->lanes;
  workInProgress->childLanes = current->childLanes;

  // Profiler 字段
  workInProgress->selfBaseDuration = current->selfBaseDuration;
  workInProgress->treeBaseDuration = current->treeBaseDuration;

  return workInProgress;
}

/**
 * 重置工作进行中的 Fiber
 * @source reactjs/packages/react-reconciler/src/ReactFiber.js:373-401
 */
inline void resetWorkInProgress(FiberRef workInProgress, Lanes renderLanes) {
  // 重置 effect 相关字段
  workInProgress->flags &= StaticMask | Placement;
  workInProgress->subtreeFlags = NoFlags;
  workInProgress->clearDeletions();

  workInProgress->lanes = renderLanes;
  workInProgress->childLanes = NoLanes;
}

} // namespace react::reconciler
