/**
 * React Fiber Suspense Component
 * 
 * Suspense 组件相关的类型定义和工具函数
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseComponent.js
 */

#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <any>
#include <set>
#include <functional>

#include "ReactFiber.h"
#include "ReactFiberFlags.h"
#include "ReactFiberLane.h"
#include "ReactWorkTags.h"

namespace react::reconciler {

// =============================================================================
// 前向声明
// =============================================================================

struct TreeContext;

// CapturedValue 使用 ErrorCapturedValue 类型（定义在 ReactCapturedValue.h）
// 为避免循环依赖，这里只声明一个通用的 hydration 错误类型
struct HydrationError {
  std::any value;
  std::optional<std::string> stack = std::nullopt;
};

using HydrationErrorRef = std::shared_ptr<HydrationError>;

// =============================================================================
// Wakeable 类型
// @source shared/ReactTypes.js
// =============================================================================

/**
 * Wakeable 是一个类似 Promise 的对象，可以被唤醒
 * 用于 Suspense 机制
 */
struct Wakeable {
  std::function<void(std::function<void()>)> then;
};

using WakeableRef = std::shared_ptr<Wakeable>;

// =============================================================================
// SuspenseInstance 类型
// @source ReactFiberConfig.js
// =============================================================================

using SuspenseInstance = std::any;

// =============================================================================
// SuspenseState 类型
// @source:24-42 SuspenseState
// =============================================================================

/**
 * SuspenseState 表示 Suspense 边界的状态
 * 
 * - null SuspenseState 表示未挂起的正常 Suspense 边界
 * - 非 null SuspenseState 表示因某种原因被阻塞
 *   - 非 null dehydrated 字段表示等待 hydration
 *   - null dehydrated 字段表示因 suspending 被阻塞，正在显示 fallback
 */
struct SuspenseState {
  // @source:33 - 如果边界仍在 dehydrated 状态
  SuspenseInstance dehydrated;
  
  // @source:34 - 树上下文
  std::shared_ptr<TreeContext> treeContext = nullptr;
  
  // @source:38 - 尝试 hydrate dehydrated 边界的 lane
  Lane retryLane = OffscreenLane;
  
  // @source:41 - hydrate 时发生的错误
  std::vector<HydrationErrorRef> hydrationErrors;
};

using SuspenseStateRef = std::shared_ptr<SuspenseState>;

// =============================================================================
// SuspenseListTailMode 类型
// @source shared/ReactTypes.js
// =============================================================================

enum class SuspenseListTailMode {
  Visible,
  Hidden,
  Collapsed
};

// =============================================================================
// SuspenseListRenderState 类型
// @source:44-57 SuspenseListRenderState
// =============================================================================

/**
 * SuspenseList 的渲染状态
 */
struct SuspenseListRenderState {
  // @source:45 - 是否反向
  bool isBackwards = false;
  
  // @source:47 - 当前渲染的 tail 行
  FiberRef rendering = nullptr;
  
  // @source:49 - 开始渲染最近 tail 行的绝对时间
  double renderingStartTime = 0.0;
  
  // @source:51 - 已渲染子节点的最后一个
  FiberRef last = nullptr;
  
  // @source:53 - 列表 tail 上剩余的行
  FiberRef tail = nullptr;
  
  // @source:55 - Tail 插入设置
  SuspenseListTailMode tailMode = SuspenseListTailMode::Visible;
  
  // @source:57 - 跟踪多次遍历期间的 fork 总数
  int treeForkCount = 0;
};

using SuspenseListRenderStateRef = std::shared_ptr<SuspenseListRenderState>;

// =============================================================================
// RetryQueue 类型
// @source:59
// =============================================================================

using RetryQueue = std::set<WakeableRef>;

// =============================================================================
// 工具函数
// =============================================================================

/**
 * 检查 SuspenseInstance 是否处于 pending 状态
 * @source ReactFiberConfig.js
 */
inline bool isSuspenseInstancePending(const SuspenseInstance& instance) {
  // 简化实现 - 实际需要根据 host config 实现
  return false;
}

/**
 * 检查 SuspenseInstance 是否显示 fallback
 * @source ReactFiberConfig.js
 */
inline bool isSuspenseInstanceFallback(const SuspenseInstance& instance) {
  // 简化实现
  return false;
}

/**
 * 查找第一个 suspended 的 Fiber
 * @source:61-98 findFirstSuspended
 */
inline FiberRef findFirstSuspended(FiberRef row) {
  FiberRef node = row;
  
  while (node != nullptr) {
    bool foundSuspended = false;
    
    if (node->tag == SuspenseComponent) {
      // 尝试获取 SuspenseState (可能是直接的或 shared_ptr)
      SuspenseState* statePtr = nullptr;
      
      // 首先尝试 shared_ptr
      auto* sharedState = std::any_cast<std::shared_ptr<SuspenseState>>(&node->memoizedState);
      if (sharedState && *sharedState) {
        statePtr = sharedState->get();
      } else {
        // 尝试直接的 SuspenseState
        statePtr = std::any_cast<SuspenseState>(&node->memoizedState);
      }
      
      if (statePtr != nullptr) {
        auto& dehydrated = statePtr->dehydrated;
        if (!dehydrated.has_value() ||
            isSuspenseInstancePending(dehydrated) ||
            isSuspenseInstanceFallback(dehydrated)) {
          return node;
        }
      }
    } else if (node->tag == SuspenseListComponent) {
      // 检查是否有 revealOrder 属性
      // 简化实现：检查 DidCapture 标志
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
      // 检查完当前节点，继续到 sibling
      node = node->sibling;
      continue;
    }
    
    // 没有找到，继续到 sibling
    if (node->sibling != nullptr) {
      node->sibling->return_ = node->return_;
      node = node->sibling;
      continue;
    }
    
    // 没有 sibling，向上回溯
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
