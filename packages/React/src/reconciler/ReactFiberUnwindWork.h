/**
 * React Fiber Unwind Work
 * 
 * 处理渲染中断时的 Fiber 栈展开
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberUnwindWork.js
 */

#pragma once

#include <memory>

#include "ReactFiber.h"
#include "ReactFiberRoot.h"
#include "ReactFiberLane.h"
#include "ReactFiberFlags.h"
#include "ReactWorkTags.h"
#include "ReactTypeOfMode.h"
#include "ReactFiberSuspenseContext.h"
#include "ReactFiberSuspenseComponent.h"

namespace react::reconciler {

// =============================================================================
// 上下文 Pop 函数声明 (简化版)
// 这些函数在各自的上下文模块中实现
// =============================================================================

// 从 ReactFiberHostContext
inline void popHostContainer(FiberRef workInProgress) {
  // 简化实现
}

inline void popHostContext(FiberRef workInProgress) {
  // 简化实现
}

// 从 ReactFiberTreeContext  
inline void popTreeContext(FiberRef workInProgress) {
  // 简化实现
}

// 从 ReactFiberLegacyContext
inline void popLegacyContext(FiberRef workInProgress) {
  // 简化实现
}

inline void popTopLevelLegacyContextObject(FiberRef workInProgress) {
  // 简化实现
}

inline bool isLegacyContextProvider(const std::any& component) {
  // 简化实现
  return false;
}

// jsi::Value 重载版本
inline bool isLegacyContextProvider(const jsi::Value& component) {
  // 简化实现
  return false;
}

// 从 ReactFiberNewContext
inline void popProvider(const std::any& context, FiberRef workInProgress) {
  // 简化实现
}

// jsi::Value 重载版本
inline void popProvider(const jsi::Value& context, FiberRef workInProgress) {
  // 简化实现
}

// 从 ReactFiberCacheComponent
inline void popCacheProvider(FiberRef workInProgress, const std::any& cache) {
  // 简化实现
}

// 从 ReactFiberHiddenContext
inline void popHiddenContext(FiberRef workInProgress) {
  // 简化实现
}

// 从 ReactFiberTransition
inline void popTransition(FiberRef workInProgress, FiberRef current) {
  // 简化实现
}

inline void popRootTransition(FiberRef workInProgress, FiberRootRef root, Lanes renderLanes) {
  // 简化实现
}

// 从 ReactFiberHydrationContext
inline void resetHydrationState() {
  // 简化实现
}

// 从 ReactProfilerTimer
inline void transferActualDuration(FiberRef workInProgress) {
  // 简化实现
}

// =============================================================================
// unwindWork
// @source:61-152 unwindWork
// =============================================================================

/**
 * 展开工作 - 当遇到错误或 Suspense 时向上遍历栈
 * 
 * 返回应该重新渲染的 Fiber（如果有），否则返回 null
 */
inline FiberRef unwindWork(
  FiberRef current,
  FiberRef workInProgress,
  Lanes renderLanes
) {
  popTreeContext(workInProgress);
  
  switch (workInProgress->tag) {
    case ClassComponent: {
      // 检查是否是遗留上下文提供者
      if (isLegacyContextProvider(workInProgress->type)) {
        popLegacyContext(workInProgress);
      }
      
      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;
        
        // 如果启用 profiler，转移实际持续时间
        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }
        
        return workInProgress;
      }
      return nullptr;
    }
    
    case HostRoot: {
      auto* rootPtr = std::any_cast<FiberRootRef>(&workInProgress->stateNode);
      if (rootPtr) {
        FiberRootRef root = *rootPtr;
        
        // Pop 各种上下文
        popRootTransition(workInProgress, root, renderLanes);
        popHostContainer(workInProgress);
        popTopLevelLegacyContextObject(workInProgress);
        
        Flags flags = workInProgress->flags;
        if ((flags & ShouldCapture) != NoFlags && (flags & DidCapture) == NoFlags) {
          // 渲染期间有未被 suspense 边界捕获的错误
          // 在根上做第二遍来卸载子节点
          workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;
          return workInProgress;
        }
      }
      return nullptr;
    }
    
    case HostComponent:
    case HostHoistable:
    case HostSingleton: {
      popHostContext(workInProgress);
      return nullptr;
    }
    
    case SuspenseComponent: {
      popSuspenseHandler(workInProgress);
      
      // 检查是否有 dehydrated 状态
      auto* statePtr = std::any_cast<std::shared_ptr<SuspenseState>>(&workInProgress->memoizedState);
      if (statePtr && *statePtr) {
        auto& suspenseState = *statePtr;
        if (suspenseState->dehydrated.has_value()) {
          auto alternate = workInProgress->alternate.lock();
          if (!alternate) {
            // 新挂载的 dehydrated 组件抛出异常 - 这是 React 的 bug
          }
          resetHydrationState();
        }
      }
      
      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;
        
        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }
        
        return workInProgress;
      }
      return nullptr;
    }
    
    case SuspenseListComponent: {
      popSuspenseListContext(workInProgress);
      // SuspenseList 不实际捕获任何东西
      return nullptr;
    }
    
    case HostPortal: {
      popHostContainer(workInProgress);
      return nullptr;
    }
    
    case ContextProvider: {
      popProvider(workInProgress->type, workInProgress);
      return nullptr;
    }
    
    case OffscreenComponent:
    case LegacyHiddenComponent: {
      popSuspenseHandler(workInProgress);
      popHiddenContext(workInProgress);
      popTransition(workInProgress, current);
      
      Flags flags = workInProgress->flags;
      if (flags & ShouldCapture) {
        workInProgress->flags = (flags & ~ShouldCapture) | DidCapture;
        
        if ((workInProgress->mode & ProfileMode) != NoMode) {
          transferActualDuration(workInProgress);
        }
        
        return workInProgress;
      }
      return nullptr;
    }
    
    case CacheComponent: {
      // 简化实现
      return nullptr;
    }
    
    default:
      return nullptr;
  }
}

// =============================================================================
// unwindInterruptedWork
// @source:228-311 unwindInterruptedWork
// =============================================================================

/**
 * 展开被中断的工作
 * 
 * 当渲染被中断时（如有更高优先级的更新），清理部分完成的工作
 */
inline void unwindInterruptedWork(
  FiberRef current,
  FiberRef interruptedWork,
  Lanes renderLanes
) {
  popTreeContext(interruptedWork);
  
  switch (interruptedWork->tag) {
    case ClassComponent: {
      // 检查是否有子上下文类型
      // 简化：直接 pop
      break;
    }
    
    case HostRoot: {
      auto* rootPtr = std::any_cast<FiberRootRef>(&interruptedWork->stateNode);
      if (rootPtr) {
        FiberRootRef root = *rootPtr;
        popRootTransition(interruptedWork, root, renderLanes);
        popHostContainer(interruptedWork);
        popTopLevelLegacyContextObject(interruptedWork);
      }
      break;
    }
    
    case HostComponent:
    case HostHoistable:
    case HostSingleton: {
      popHostContext(interruptedWork);
      break;
    }
    
    case HostPortal: {
      popHostContainer(interruptedWork);
      break;
    }
    
    case SuspenseComponent: {
      popSuspenseHandler(interruptedWork);
      break;
    }
    
    case SuspenseListComponent: {
      popSuspenseListContext(interruptedWork);
      break;
    }
    
    case ContextProvider: {
      popProvider(interruptedWork->type, interruptedWork);
      break;
    }
    
    case OffscreenComponent:
    case LegacyHiddenComponent: {
      popSuspenseHandler(interruptedWork);
      popHiddenContext(interruptedWork);
      popTransition(interruptedWork, current);
      break;
    }
    
    case CacheComponent: {
      // 简化实现
      break;
    }
    
    default:
      break;
  }
}

// =============================================================================
// 完整的错误展开流程
// =============================================================================

/**
 * 完成展开阶段
 * 
 * 从 sourceFiber 向上遍历，展开每个 fiber 直到找到能处理的边界
 */
inline FiberRef completeUnitOfUnwind(
  FiberRef unitOfWork,
  FiberRef thrownValue,
  Lanes renderLanes
) {
  FiberRef current = unitOfWork->alternate.lock();
  
  // 尝试展开当前工作
  FiberRef next = unwindWork(current, unitOfWork, renderLanes);
  
  if (next) {
    // 找到了一个可以处理异常的边界
    // 清除 Incomplete 标志，因为我们要重新渲染这个边界
    next->flags &= ~Incomplete;
    return next;
  }
  
  // 继续向上遍历
  FiberRef returnFiber = unitOfWork->return_.lock();
  if (returnFiber) {
    // 标记父节点为 incomplete
    returnFiber->flags |= Incomplete;
    returnFiber->subtreeFlags = NoFlags;
    returnFiber->deletions.clear();
  }
  
  return nullptr;
}

} // namespace react::reconciler
