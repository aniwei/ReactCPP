/**
 * React Child Fiber
 * 
 * 子节点协调器，负责比较和协调子元素
 * 实现了 React 的 Diff 算法
 * 
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>

#include "ReactFiber.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "../react/ReactElement.h"
#include "../shared/ReactSymbols.h"

namespace react {
class ReactHostRuntime;
} // namespace react

namespace react::reconciler {


class ReactChildFiberReconciler;

// Placement 标志操作
void placeSingleChild(
  const FiberRef& fiber, 
  bool shouldTrackSideEffects);


// ReactChildFiberReconciler 类
// 实现子节点协调逻辑
class ReactChildFiberReconciler {
public:
  explicit ReactChildFiberReconciler(bool shouldTrackSideEffects)
    : shouldTrackSideEffects_(shouldTrackSideEffects) {}
    
  ~ReactChildFiberReconciler() = default;
    
  bool shouldTrackSideEffects() const { return shouldTrackSideEffects_; }
  
  // 删除子节点
  void deleteChild(
    const FiberRef& returnFiber,
    const FiberRef& childToDelete);
    
  //  删除剩余子节点
  void deleteRemainingChildren(
    const FiberRef& returnFiber, 
    const FiberRef& currentFirstChild);
  
  // 构建剩余子节点映射
  std::unordered_map<std::string, FiberRef> mapRemainingChildren(
    const FiberRef& /* returnFiber */,
    const FiberRef& currentFirstChild);
  
  // 放置子节点
  int placeChild(
    const FiberRef& newFiber, 
    int lastPlacedIndex, 
    int newIndex);
  
  // 使用 Fiber
  FiberRef useFiber(
    jsi::Runtime& rt,
    const FiberRef& fiber, 
    const jsi::Value& pendingProps);

  // 检查是否为文本类型
  bool isTextContent(const jsi::Value& value);

  // 获取文本内容
  std::string getTextContent(jsi::Runtime& rt, const jsi::Value& value);

  // 更新操作
  // 更新文本节点
  FiberRef updateTextNode(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const FiberRef& current,
    const jsi::Value& textContent,
    Lanes lanes);
  
  // 更新元素
  FiberRef updateElement(
    const FiberRef& returnFiber,
    const FiberRef& current,
    const react::ReactElement& element,
    Lanes lanes);
  
  // 更新 Portal
  FiberRef updatePortal(
    const FiberRef& returnFiber,
    const FiberRef& current,
    const react::ReactPortal& portal,
    Lanes lanes);
  
  // 更新 Fragment
  FiberRef updateFragment(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const FiberRef& current,
    const jsi::Array& fragment,
    Lanes lanes,
    const jsi::Value& key);
  
  // 创建子 Fiber
  FiberRef createChild(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const jsi::Value& newChild,
    Lanes lanes);
  
  // 更新 Slot (尝试复用)
  FiberRef updateSlot(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const FiberRef& oldFiber,
    const jsi::Value& newChild,
    Lanes lanes);
  
  // 更新来自 Map 的 Slot
  FiberRef updateFromMap(
    std::unordered_map<std::string, FiberRef>& existingChildren,
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    int newIdx,
    const jsi::Value& newChild,
    Lanes lanes);
  
  // 协调单个文本节点
  FiberRef reconcileSingleTextNode(
    const FiberRef& returnFiber,
    const FiberRef& currentFirstChild,
    const jsi::Value& textContent,
    Lanes lanes);
  
  // 协调单个元素
  FiberRef reconcileSingleElement(
    const FiberRef& returnFiber,
    const FiberRef& currentFirstChild,
    const ReactElement& element,
    Lanes lanes);
  
  // 协调单个 Portal
  FiberRef reconcileSinglePortal(
    const FiberRef& returnFiber,
    const FiberRef& currentFirstChild,
    const react::ReactPortal& portal,
    Lanes lanes);
  
  // 协调子数组
  FiberRef reconcileChildrenArray(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const FiberRef& currentFirstChild,
    const jsi::Array& newChildren,
    Lanes lanes);

  // 协调子 Fibers
  FiberRef reconcileChildFibers(
    jsi::Runtime& rt,
    const FiberRef& returnFiber,
    const FiberRef& currentFirstChild,
    const jsi::Value& newChild,
    Lanes lanes);

private:
  bool shouldTrackSideEffects_;
};

ReactChildFiberReconciler createReconcileChildFibers();
ReactChildFiberReconciler createMountChildFibers();

ReactChildFiberReconciler& getReconcileChildFibers(react::ReactHostRuntime& hostRuntime);
ReactChildFiberReconciler& getMountChildFibers(react::ReactHostRuntime& hostRuntime);

FiberRef reconcileChildFibers(
  jsi::Runtime& rt,
  react::ReactHostRuntime& hostRuntime,
  const FiberRef& returnFiber,
  const FiberRef& currentFirstChild,
  const jsi::Value& newChild,
  Lanes lanes);

FiberRef mountChildFibers(
  jsi::Runtime& rt,
  react::ReactHostRuntime& hostRuntime,
  const FiberRef& returnFiber,
  const FiberRef& currentFirstChild,
  const jsi::Value& newChild,
  Lanes lanes);

void cloneChildFibers(
  jsi::Runtime& rt,
  const FiberRef& current, 
  const FiberRef& workInProgress);

void resetChildReconcilerOnUnwind();

} // namespace react::reconciler
