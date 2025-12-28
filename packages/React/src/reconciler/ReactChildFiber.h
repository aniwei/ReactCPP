/**
 * React Child Fiber
 * 
 * 子节点协调器，负责比较和协调子元素
 * 实现了 React 的 Diff 算法
 * 
 * @source reactjs/packages/react-reconciler/src/ReactChildFiber.js
 */

#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>
#include <optional>
#include <any>
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

namespace react::reconciler {

class ReactChildFiberReconciler;

// Placement 标志操作
// @source:230-280 ReactChildFiber.js
inline void placeSingleChild(FiberRef fiber, bool shouldTrackSideEffects) {
  if (shouldTrackSideEffects && fiber && fiber->alternate.expired()) {
    fiber->flags |= Placement;
  }
}

// =============================================================================
// ReactChildFiberReconciler 类
// 实现子节点协调逻辑
// @source reactjs/packages/react-reconciler/src/ReactChildFiber.js
// =============================================================================

class ReactChildFiberReconciler {
public:
  /**
   * 构造函数
   * @param shouldTrackSideEffects 是否跟踪副作用 (更新时为 true，挂载时为 false)
   */
  explicit ReactChildFiberReconciler(bool shouldTrackSideEffects)
    : shouldTrackSideEffects_(shouldTrackSideEffects) {}
    
  ~ReactChildFiberReconciler() = default;
    
    /**
     * 是否应该跟踪副作用
     */
    bool shouldTrackSideEffects() const {
      return shouldTrackSideEffects_;
    }

    // =========================================================================
    // 删除操作
    // =========================================================================
    
    /**
     * 删除子节点
     * @source:370-395 deleteChild
     */
    void deleteChild(FiberRef returnFiber, FiberRef childToDelete) {
        if (!shouldTrackSideEffects_) {
            return;
        }
        
        if (returnFiber && childToDelete) {
            returnFiber->deletions.push_back(childToDelete);
            childToDelete->flags |= ChildDeletion;
            returnFiber->flags |= ChildDeletion;
        }
    }
    
    /**
     * 删除剩余子节点
     * @source:400-415 deleteRemainingChildren
     */
    void deleteRemainingChildren(FiberRef returnFiber, FiberRef currentFirstChild) {
      if (!shouldTrackSideEffects_) {
          return;
      }
      
      FiberRef childToDelete = currentFirstChild;
      while (childToDelete != nullptr) {
          deleteChild(returnFiber, childToDelete);
          childToDelete = childToDelete->sibling;
      }
    }
    
    // =========================================================================
    // 映射操作
    // =========================================================================
    
    /**
     * 将现有子节点映射到 Map
     * @source:420-445 mapRemainingChildren
     */
    std::unordered_map<std::string, FiberRef> mapRemainingChildren(
      FiberRef /* returnFiber */,
      FiberRef currentFirstChild
    ) {
      std::unordered_map<std::string, FiberRef> existingChildren;
      
      FiberRef existingChild = currentFirstChild;
      while (existingChild != nullptr) {
        // 使用 index 作为默认 key
        existingChildren[std::to_string(existingChild->index)] = existingChild;
        existingChild = existingChild->sibling;
      }
      
      return existingChildren;
    }
    
    int placeChild(
      const FiberRef& newFiber, 
      int lastPlacedIndex, 
      int newIndex) {
      newFiber->index = newIndex;
        
      if (!shouldTrackSideEffects_) {
        newFiber->flags |= Forked;
        return lastPlacedIndex;
      }
        
        auto current = newFiber->getAlternate();
        if (current != nullptr) {
            int oldIndex = current->index;
            if (oldIndex < lastPlacedIndex) {
                // 这是一个移动
                newFiber->flags |= Placement;
                return lastPlacedIndex;
            } else {
                // 保持原位置
                return oldIndex;
            }
        } else {
            // 这是一个插入
            newFiber->flags |= Placement;
            return lastPlacedIndex;
        }
    }
    
    FiberRef useFiber(FiberRef fiber, std::any pendingProps) {
        (void)pendingProps; // 标记为已使用
      auto clone = std::make_shared<Fiber>(fiber->tag, fiber->mode);
      clone->stateNode = fiber->stateNode;
      // 注意：elementType 是 jsi::Value，不能直接复制
      // 在简化实现中我们跳过 JSI 值的复制
      clone->index = 0;
      clone->sibling = nullptr;
      clone->setAlternate(fiber);
      fiber->setAlternate(clone);
      clone->memoizedState = fiber->memoizedState;
      // memoizedProps 也是 jsi::Value，跳过复制
      clone->lanes = fiber->lanes;
      clone->childLanes = fiber->childLanes;
      return clone;
    }
    
    std::optional<react::ReactElement> coerceToElement(const std::any& value) {
      try {
          return std::any_cast<react::ReactElement>(value);
      } catch (const std::bad_any_cast&) {
          return std::nullopt;
      }
    }
    
    /**
     * 强制类型转换为 ReactPortal
     */
    std::optional<react::ReactPortal> coerceToPortal(const std::any& value) {
        try {
            return std::any_cast<react::ReactPortal>(value);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
    
    /**
     * 检查是否为文本类型
     */
    bool isTextContent(const std::any& value) {
        if (!value.has_value()) {
            return false;
        }
        
        try { std::any_cast<std::string>(value); return true; }
        catch (const std::bad_any_cast&) {}
        
        try { std::any_cast<const char*>(value); return true; }
        catch (const std::bad_any_cast&) {}
        
        try { std::any_cast<int>(value); return true; }
        catch (const std::bad_any_cast&) {}
        
        try { std::any_cast<double>(value); return true; }
        catch (const std::bad_any_cast&) {}
        
        return false;
    }
    
    /**
     * 获取文本内容
     */
    std::string getTextContent(const std::any& value) {
        try { return std::any_cast<std::string>(value); }
        catch (const std::bad_any_cast&) {}
        
        try { return std::string(std::any_cast<const char*>(value)); }
        catch (const std::bad_any_cast&) {}
        
        try { return std::to_string(std::any_cast<int>(value)); }
        catch (const std::bad_any_cast&) {}
        
        try { return std::to_string(std::any_cast<double>(value)); }
        catch (const std::bad_any_cast&) {}
        
        return "";
    }
    
    /**
     * 获取元素的 key
     */
    std::optional<std::string> getElementKey(const std::any& element) {
        if (auto el = coerceToElement(element)) {
            return el->key;
        }
        
        if (auto portal = coerceToPortal(element)) {
            return portal->key;
        }
        
        return std::nullopt;
    }
    
    // =========================================================================
    // 更新操作
    // =========================================================================
    
    /**
     * 更新文本节点
     * @source:480-500 updateTextNode
     */
    FiberRef updateTextNode(
        FiberRef returnFiber,
        FiberRef current,
        const std::string& /* textContent */,
        Lanes /* lanes */
    ) {
        if (current == nullptr || current->tag != HostText) {
            auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
            created->setReturn(returnFiber);
            return created;
        } else {
            auto existing = useFiber(current, std::any{});
            existing->setReturn(returnFiber);
            return existing;
        }
    }
    
    /**
     * 更新元素
     * @source:505-595 updateElement
     */
    FiberRef updateElement(
        FiberRef returnFiber,
        FiberRef current,
        const react::ReactElement& element,
        Lanes /* lanes */
    ) {
        if (current != nullptr) {
            auto existing = useFiber(current, std::any{element.props});
            existing->setReturn(returnFiber);
            return existing;
        }
        
        auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
        created->stateNode = react::getElementTypeString(element);
        created->setReturn(returnFiber);
        return created;
    }
    
    /**
     * 更新 Portal
     * @source:600-660 updatePortal
     */
    FiberRef updatePortal(
        FiberRef returnFiber,
        FiberRef current,
        const react::ReactPortal& portal,
        Lanes /* lanes */
    ) {
        if (current != nullptr && current->tag == HostPortal) {
            auto existing = useFiber(current, portal.children);
            existing->setReturn(returnFiber);
            return existing;
        }
        
        auto created = std::make_shared<Fiber>(HostPortal, returnFiber->mode);
        created->stateNode = portal.containerInfo;
        created->setReturn(returnFiber);
        return created;
    }
    
    /**
     * 更新 Fragment
     * @source:665-720 updateFragment
     */
    FiberRef updateFragment(
        FiberRef returnFiber,
        FiberRef current,
        const std::vector<std::any>& fragment,
        Lanes /* lanes */,
        std::optional<std::string> /* key */
    ) {
        if (current != nullptr && current->tag == Fragment) {
            auto existing = useFiber(current, fragment);
            existing->setReturn(returnFiber);
            return existing;
        }
        
        auto created = std::make_shared<Fiber>(Fragment, returnFiber->mode);
        created->stateNode = fragment;
        created->setReturn(returnFiber);
        return created;
    }
    
    // =========================================================================
    // 创建操作
    // =========================================================================
    
    /**
     * 创建子 Fiber
     * @source:620-755 createChild
     */
    FiberRef createChild(
        FiberRef returnFiber,
        std::any newChild,
        Lanes /* lanes */
    ) {
        if (!newChild.has_value()) {
            return nullptr;
        }
        
        // 文本内容
        if (isTextContent(newChild)) {
            auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
            created->stateNode = getTextContent(newChild);
            created->setReturn(returnFiber);
            return created;
        }
        
        // ReactElement
        if (auto element = coerceToElement(newChild)) {
            auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
            created->stateNode = react::getElementTypeString(*element);
            created->setReturn(returnFiber);
            return created;
        }
        
        // ReactPortal
        if (auto portal = coerceToPortal(newChild)) {
            auto created = std::make_shared<Fiber>(HostPortal, returnFiber->mode);
            created->stateNode = portal->containerInfo;
            created->setReturn(returnFiber);
            return created;
        }
        
        // 数组作为 Fragment
        try {
            auto& children = std::any_cast<const std::vector<std::any>&>(newChild);
            auto created = std::make_shared<Fiber>(Fragment, returnFiber->mode);
            created->stateNode = children;
            created->setReturn(returnFiber);
            return created;
        } catch (const std::bad_any_cast&) {}
        
        return nullptr;
    }
    
    /**
     * 更新 Slot (尝试复用)
     * @source:760-875 updateSlot
     */
    FiberRef updateSlot(
        FiberRef returnFiber,
        FiberRef oldFiber,
        std::any newChild,
        Lanes lanes
    ) {
        // 文本内容
        if (isTextContent(newChild)) {
            return updateTextNode(returnFiber, oldFiber, getTextContent(newChild), lanes);
        }
        
        // ReactElement
        if (auto element = coerceToElement(newChild)) {
            return updateElement(returnFiber, oldFiber, *element, lanes);
        }
        
        // ReactPortal
        if (auto portal = coerceToPortal(newChild)) {
            return updatePortal(returnFiber, oldFiber, *portal, lanes);
        }
        
        // 数组/Fragment
        try {
            auto& children = std::any_cast<const std::vector<std::any>&>(newChild);
            return updateFragment(returnFiber, oldFiber, children, lanes, std::nullopt);
        } catch (const std::bad_any_cast&) {}
        
        return nullptr;
    }
    
    /**
     * 更新来自 Map 的 Slot
     * @source:880-980 updateFromMap
     */
    FiberRef updateFromMap(
        std::unordered_map<std::string, FiberRef>& existingChildren,
        FiberRef returnFiber,
        int newIdx,
        std::any newChild,
        Lanes lanes
    ) {
        // 文本内容
        if (isTextContent(newChild)) {
            auto it = existingChildren.find(std::to_string(newIdx));
            FiberRef matchedFiber = (it != existingChildren.end()) ? it->second : nullptr;
            return updateTextNode(returnFiber, matchedFiber, getTextContent(newChild), lanes);
        }
        
        // ReactElement
        if (auto element = coerceToElement(newChild)) {
            std::string lookupKey = element->hasKey() ? element->key.value() : std::to_string(newIdx);
            auto it = existingChildren.find(lookupKey);
            FiberRef matchedFiber = (it != existingChildren.end()) ? it->second : nullptr;
            return updateElement(returnFiber, matchedFiber, *element, lanes);
        }
        
        // ReactPortal
        if (auto portal = coerceToPortal(newChild)) {
            std::string lookupKey = portal->key.has_value() ? portal->key.value() : std::to_string(newIdx);
            auto it = existingChildren.find(lookupKey);
            FiberRef matchedFiber = (it != existingChildren.end()) ? it->second : nullptr;
            return updatePortal(returnFiber, matchedFiber, *portal, lanes);
        }
        
        return nullptr;
    }
    
    /**
     * 协调单个文本节点
     * @source:1555-1575 reconcileSingleTextNode
     */
    FiberRef reconcileSingleTextNode(
      FiberRef returnFiber,
      FiberRef currentFirstChild,
      const std::string& /* textContent */,
      Lanes /* lanes */
    ) {
      // 检查第一个子节点是否为文本节点
      if (currentFirstChild != nullptr && currentFirstChild->tag == HostText) {
        deleteRemainingChildren(returnFiber, currentFirstChild->sibling);
        auto existing = useFiber(currentFirstChild, std::any{});
        existing->setReturn(returnFiber);
        return existing;
      }
      
      // 删除现有子节点并创建新文本节点
      deleteRemainingChildren(returnFiber, currentFirstChild);
      auto created = std::make_shared<Fiber>(HostText, returnFiber->mode);
      created->setReturn(returnFiber);
      return created;
    }
    
    /**
     * 协调单个元素
     * @source:1580-1680 reconcileSingleElement
     */
    FiberRef reconcileSingleElement(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        const react::ReactElement& element,
        Lanes /* lanes */
    ) {
        FiberRef child = currentFirstChild;
        
        while (child != nullptr) {
            // 简化：删除所有现有子节点并创建新的
            deleteChild(returnFiber, child);
            child = child->sibling;
        }
        
        // 创建新的 Fiber
        auto created = std::make_shared<Fiber>(FunctionComponent, returnFiber->mode);
        created->stateNode = react::getElementTypeString(element);
        created->setReturn(returnFiber);
        return created;
    }
    
    /**
     * 协调单个 Portal
     * @source:1690-1760 reconcileSinglePortal
     */
    FiberRef reconcileSinglePortal(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        const react::ReactPortal& portal,
        Lanes /* lanes */
    ) {
        FiberRef child = currentFirstChild;
        
        while (child != nullptr) {
            if (child->tag == HostPortal) {
                deleteRemainingChildren(returnFiber, child->sibling);
                auto existing = useFiber(child, portal.children);
                existing->setReturn(returnFiber);
                return existing;
            } else {
                deleteChild(returnFiber, child);
            }
            child = child->sibling;
        }
        
        // 创建新的 Portal Fiber
        auto created = std::make_shared<Fiber>(HostPortal, returnFiber->mode);
        created->stateNode = portal.containerInfo;
        created->setReturn(returnFiber);
        return created;
    }
    
    // =========================================================================
    // 数组协调
    // =========================================================================
    
    /**
     * 协调子数组
     * @source:1100-1250 reconcileChildrenArray
     */
    FiberRef reconcileChildrenArray(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        const std::vector<std::any>& newChildren,
        Lanes lanes
    ) {
        FiberRef resultingFirstChild = nullptr;
        FiberRef previousNewFiber = nullptr;
        FiberRef oldFiber = currentFirstChild;
        int lastPlacedIndex = 0;
        size_t newIdx = 0;
        FiberRef nextOldFiber = nullptr;
        
        // 第一阶段：按顺序比较
        for (; oldFiber != nullptr && newIdx < newChildren.size(); newIdx++) {
            if (static_cast<size_t>(oldFiber->index) > newIdx) {
                nextOldFiber = oldFiber;
                oldFiber = nullptr;
            } else {
                nextOldFiber = oldFiber->sibling;
            }
            
            auto newFiber = updateSlot(returnFiber, oldFiber, newChildren[newIdx], lanes);
            
            if (newFiber == nullptr) {
                if (oldFiber == nullptr) {
                    oldFiber = nextOldFiber;
                }
                break;
            }
            
            if (shouldTrackSideEffects_) {
                if (oldFiber && newFiber->alternate.expired()) {
                    deleteChild(returnFiber, oldFiber);
                }
            }
            
            lastPlacedIndex = placeChild(newFiber, lastPlacedIndex, static_cast<int>(newIdx));
            
            if (previousNewFiber == nullptr) {
                resultingFirstChild = newFiber;
            } else {
                previousNewFiber->sibling = newFiber;
            }
            previousNewFiber = newFiber;
            oldFiber = nextOldFiber;
        }
        
        // 如果新子节点处理完毕，删除剩余旧节点
        if (newIdx == newChildren.size()) {
            deleteRemainingChildren(returnFiber, oldFiber);
            return resultingFirstChild;
        }
        
        // 如果旧节点处理完毕，插入剩余新节点
        if (oldFiber == nullptr) {
            for (; newIdx < newChildren.size(); newIdx++) {
                auto newFiber = createChild(returnFiber, newChildren[newIdx], lanes);
                if (newFiber == nullptr) {
                    continue;
                }
                lastPlacedIndex = placeChild(newFiber, lastPlacedIndex, static_cast<int>(newIdx));
                if (previousNewFiber == nullptr) {
                    resultingFirstChild = newFiber;
                } else {
                    previousNewFiber->sibling = newFiber;
                }
                previousNewFiber = newFiber;
            }
            return resultingFirstChild;
        }
        
        // 第二阶段：使用 Map 进行复杂对比
        auto existingChildren = mapRemainingChildren(returnFiber, oldFiber);
        
        for (; newIdx < newChildren.size(); newIdx++) {
            auto newFiber = updateFromMap(
                existingChildren, returnFiber, static_cast<int>(newIdx),
                newChildren[newIdx], lanes);
            
            if (newFiber != nullptr) {
                if (shouldTrackSideEffects_) {
                    if (!newFiber->alternate.expired()) {
                        auto key = getElementKey(newChildren[newIdx]);
                        existingChildren.erase(
                            key.has_value() ? key.value() : std::to_string(newIdx));
                    }
                }
                lastPlacedIndex = placeChild(newFiber, lastPlacedIndex, static_cast<int>(newIdx));
                if (previousNewFiber == nullptr) {
                    resultingFirstChild = newFiber;
                } else {
                    previousNewFiber->sibling = newFiber;
                }
                previousNewFiber = newFiber;
            }
        }
        
        // 删除未使用的旧节点
        if (shouldTrackSideEffects_) {
            for (auto& [_, fiber] : existingChildren) {
                deleteChild(returnFiber, fiber);
            }
        }
        
        return resultingFirstChild;
    }
    
    // =========================================================================
    // 核心协调 API
    // =========================================================================
    
    /**
     * 协调子 Fibers
     * @source:1840-2100 reconcileChildFibersImpl + reconcileChildFibers
     */
    FiberRef reconcileChildFibers(
        FiberRef returnFiber,
        FiberRef currentFirstChild,
        std::any newChild,
        Lanes lanes
    ) {
        // 处理空子节点
        if (!newChild.has_value()) {
            deleteRemainingChildren(returnFiber, currentFirstChild);
            return nullptr;
        }
        
        // 尝试转换为 ReactElement
        if (auto element = coerceToElement(newChild)) {
            auto result = reconcileSingleElement(
                returnFiber, currentFirstChild, *element, lanes);
            placeSingleChild(result, shouldTrackSideEffects_);
            return result;
        }
        
        // 尝试转换为 ReactPortal
        if (auto portal = coerceToPortal(newChild)) {
            auto result = reconcileSinglePortal(
                returnFiber, currentFirstChild, *portal, lanes);
            placeSingleChild(result, shouldTrackSideEffects_);
            return result;
        }
        
        // 检查是否为文本内容
        if (isTextContent(newChild)) {
            auto result = reconcileSingleTextNode(
                returnFiber, currentFirstChild, getTextContent(newChild), lanes);
            placeSingleChild(result, shouldTrackSideEffects_);
            return result;
        }
        
        // 尝试转换为数组
        try {
            auto& children = std::any_cast<std::vector<std::any>&>(newChild);
            return reconcileChildrenArray(returnFiber, currentFirstChild, children, lanes);
        } catch (const std::bad_any_cast&) {}
        
        try {
            auto& children = std::any_cast<const std::vector<std::any>&>(newChild);
            std::vector<std::any> childrenCopy = children;
            return reconcileChildrenArray(returnFiber, currentFirstChild, childrenCopy, lanes);
        } catch (const std::bad_any_cast&) {}
        
        // 其他情况删除所有子节点
        deleteRemainingChildren(returnFiber, currentFirstChild);
        return nullptr;
    }

private:
  bool shouldTrackSideEffects_;
};


inline ReactChildFiberReconciler createReconcileChildFibers() {
  return ReactChildFiberReconciler(true);
}

/**
 * 创建用于挂载的 ChildReconciler
 * shouldTrackSideEffects = false
 */
inline ReactChildFiberReconciler createMountChildFibers() {
    return ReactChildFiberReconciler(false);
}

// =============================================================================
// 全局 reconciler 实例
// =============================================================================

/**
 * 获取协调 reconciler (用于更新)
 */
inline ReactChildFiberReconciler& getReconcileChildFibers() {
    static ReactChildFiberReconciler instance(true);
    return instance;
}

/**
 * 获取挂载 reconciler (用于首次渲染)
 */
inline ReactChildFiberReconciler& getMountChildFibers() {
    static ReactChildFiberReconciler instance(false);
    return instance;
}

// =============================================================================
// 便捷函数
// =============================================================================

/**
 * 协调子 Fibers
 */
inline FiberRef reconcileChildFibers(
    FiberRef returnFiber,
    FiberRef currentFirstChild,
    std::any newChild,
    Lanes lanes
) {
    return getReconcileChildFibers().reconcileChildFibers(
        returnFiber, currentFirstChild, newChild, lanes
    );
}

/**
 * 挂载子 Fibers
 */
inline FiberRef mountChildFibers(
    FiberRef returnFiber,
    FiberRef currentFirstChild,
    std::any newChild,
    Lanes lanes
) {
    return getMountChildFibers().reconcileChildFibers(
        returnFiber, currentFirstChild, newChild, lanes
    );
}

/**
 * 克隆子 Fibers
 * @source:1755-1795 cloneChildFibers
 */
inline void cloneChildFibers(FiberRef current, FiberRef workInProgress) {
    if (current != nullptr && workInProgress->child != current->child) {
        throw std::runtime_error("Resuming work not yet implemented.");
    }
    
    if (workInProgress->child == nullptr) {
        return;
    }
    
    FiberRef currentChild = workInProgress->child;
    auto& reconciler = getMountChildFibers();
    
    // 克隆第一个子节点 - 使用空 std::any 代替 jsi::Value
    auto newChild = reconciler.useFiber(currentChild, std::any{});
    workInProgress->child = newChild;
    newChild->setReturn(workInProgress);
    
    // 克隆其余兄弟节点
    while (currentChild->sibling != nullptr) {
        currentChild = currentChild->sibling;
        auto newSibling = reconciler.useFiber(currentChild, std::any{});
        newChild->sibling = newSibling;
        newSibling->setReturn(workInProgress);
        newChild = newSibling;
    }
    
    newChild->sibling = nullptr;
}

/**
 * 重置子 reconciler (发生错误时展开)
 * @source:115 resetChildReconcilerOnUnwind
 */
inline void resetChildReconcilerOnUnwind() {
    // 清除任何待处理的 thenable 状态
    // 在这个简化实现中，我们没有 thenable 状态跟踪
}

} // namespace react::reconciler
