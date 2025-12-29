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
#include <variant>

#include "ReactWorkTags.h"
#include "ReactFiberFlags.h"
#include "ReactFiberLane.h"
#include "ReactTypeOfMode.h"
#include "ReactRootTags.h"

#include "../runtime/ReactContainerInfo.h"
#include "../runtime/ReactDOMInstance.h"

namespace react::reconciler {

using namespace facebook;

// 前向声明
struct Fiber;
struct FiberRoot;
struct Hook;
struct UpdateQueue;
struct FunctionComponentUpdateQueue;
struct Dependencies;


// Fiber 引用类型


using FiberRef = std::shared_ptr<Fiber>;
using FiberWeakRef = std::weak_ptr<Fiber>;
using FiberRootRef = std::shared_ptr<FiberRoot>;

// ReactJS 中 stateNode/updateQueue 都是随 tag 变化的 union。
// 这里用 std::variant 表达，替代 std::any。
using StateNode = std::variant<
  std::monostate,
  FiberRootRef,           // HostRoot
  react::ContainerInfo,   // HostPortal
  ::react::ReactDOMInstance*, // HostComponent (Instance)
  std::string             // HostText（当前实现用 string 保存文本）
>;

using FiberUpdateQueue = std::variant<
  std::monostate,
  std::shared_ptr<FunctionComponentUpdateQueue>, // FunctionComponent hooks
  std::shared_ptr<UpdateQueue>                   // ClassComponent update queue
>;


// RefObject 类型
template<typename T>
struct RefObject {
  T current;
};


// Dependencies 类型
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
  jsi::Value type = jsi::Value::undefined();
  jsi::Value elementType = jsi::Value::undefined();

  // ReactJS 中 stateNode 是一个与 tag 相关的 union（HostRoot=FiberRoot, HostPortal=Portal, HostComponent=Instance, etc）。
  StateNode stateNode;
  
  // 更新队列类型在 ReactJS 中随 fiber tag 变化（FunctionComponent hooks / ClassComponent update queue 等）。
  FiberUpdateQueue updateQueue;

  FiberWeakRef return_;
  FiberWeakRef alternate;

  FiberRef child = nullptr;
  FiberRef sibling = nullptr;

  std::function<void()> refCleanup = nullptr;
  
  jsi::Value ref = jsi::Value::undefined();
  jsi::Value pendingProps = jsi::Value::undefined();
  jsi::Value memoizedProps = jsi::Value::undefined();

  // ReactJS 中 memoizedState 也是随 tag 变化的 union（hooks 链表 / suspense state / class state 等）。
  // 当前实现仍在迁移期，先用 std::any 承载内部结构。
  std::any memoizedState;
  std::shared_ptr<Dependencies> dependencies = nullptr;
  std::vector<FiberRef> deletions;
  
  int index = 0;
  TypeOfMode mode = NoMode;
  Flags flags = NoFlags;
  Flags subtreeFlags = NoFlags;
  Lanes lanes = NoLanes;
  Lanes childLanes = NoLanes;

  double actualDuration = 0.0;
  double actualStartTime = -1.0;
  double selfBaseDuration = 0.0;
  double treeBaseDuration = 0.0;

#ifdef DEV
  std::any _debugInfo;
  std::any _debugOwner;
  std::any _debugStack;
  std::any _debugTask;
  bool _debugNeedsRemount = false;
  std::vector<std::string> _debugHookTypes;
#endif

  Fiber() = default;
  Fiber(WorkTag tag_, TypeOfMode mode_);
  Fiber(
    jsi::Runtime& rt,
    WorkTag tag_,
    const jsi::Value& pendingProps_,
    const jsi::Value& key_,
    TypeOfMode mode_);

  FiberRef getReturn() const;
  void setReturn(FiberRef parent);
  FiberRef getAlternate() const;
  void setAlternate(FiberRef alt);

  bool hasChild() const;
  bool hasSibling() const;
  bool hasDeletions() const;
  void addDeletion(FiberRef fiber);
  void clearDeletions();
  bool hasKey() const;

  std::optional<std::string> getKeyString(jsi::Runtime& rt) const;
};

FiberRef createFiber(
  jsi::Runtime& rt,
  WorkTag tag,
  const jsi::Value& pendingProps,
  const jsi::Value& key,
  TypeOfMode mode);

FiberRef createHostRootFiber(
  RootTag tag,
  bool isStrictMode,
  bool strictEffectsMode);

FiberRef createWorkInProgress(
  jsi::Runtime& rt,
  const FiberRef& current,
  const jsi::Value& pendingProps);

void resetWorkInProgress(
  const FiberRef& workInProgress,
  Lanes renderLanes);

} // namespace react::reconciler
