/**
 * React Fiber Hooks Implementation
 * 
 * Hooks 的完整实现
 * 包括 useState, useReducer, useEffect, useMemo, useCallback, useRef 等 
 */

#pragma once

#include "ReactFiberHooks.h"
#include "../shared/objectIs.h"

namespace react::reconciler {


// Effect Instance (保存 destroy 函数)
struct EffectInstance {
  std::function<void()> destroy;
  EffectInstance() : destroy(nullptr) {}
};


// Hook 常量
constexpr int RE_RENDER_LIMIT = 25;

// ReactFiberHooksImpl 实现类
class ReactFiberHooksImpl : public ReactFiberHooks {
public:
  ReactFiberHooksImpl();
    
  /**
   * 使用 Hooks 渲染组件
   * @source:500-620 renderWithHooks
   */
  std::any renderWithHooks(
    FiberRef current,
    FiberRef workInProgress,
    std::any Component,
    std::any props,
    std::any secondArg,
    Lanes nextRenderLanes
  ) override;
    
  // 结束 Hooks 渲染
  void finishRenderingHooks(FiberRef current, FiberRef workInProgress) override;
    
  // 重新渲染
  std::any renderWithHooksAgain(
    FiberRef workInProgress,
    std::any Component,
    std::any props,
    std::any secondArg
  ) override;
    
  // Bailout Hooks
  void bailoutHooks(
    FiberRef current,
    FiberRef workInProgress,
    Lanes lanes
  ) override;
    
  bool checkDidRenderIdHook() override;
  void resetHooksAfterThrow() override;
  void resetHooksOnUnwind(FiberRef workInProgress) override;
  Dispatcher& getCurrentDispatcher() override;
    
private:
  // 当前 Dispatcher
  Dispatcher* currentDispatcher_ = nullptr;
  
  // 渲染 Lanes
  Lanes renderLanes_ = NoLanes;
  
  // ID 计数器
  int localIdCounter_ = 0;
  
  // 全局客户端 ID 计数器
  static int globalClientIdCounter_;
    
  // 挂载工作中的 Hook
  HookRef mountWorkInProgressHook() override;
    
  // 更新工作中的 Hook
  HookRef updateWorkInProgressHook() override;
    
  FunctionComponentUpdateQueueRef createFunctionComponentUpdateQueue() override;
    
  // Mount 实现
  // mountState
  std::pair<std::any, std::function<void(std::any)>> mountState(std::any initialState) override;
    
  // mountReducer
  std::pair<std::any, std::function<void(std::any)>> mountReducer(
    std::function<std::any(std::any, std::any)> reducer,
    std::any initialArg,
    std::optional<std::function<std::any(std::any)>> init
  ) override;
    
  // mountEffect
  void mountEffect(
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
    ) override;
    
  // mountLayoutEffect
  void mountLayoutEffect(
    std::function<std::function<void()>()> create,
    std::optional<std::vector<std::any>> deps
    ) override;
    
    /**
     * mountMemo
     * @source:2920-2945 mountMemo
     */
    std::any mountMemo(
      std::function<std::any()> create,
      std::optional<std::vector<std::any>> deps) override;
    
    /**
     * mountCallback
     * @source:2900-2915 mountCallback
     */
    std::any mountCallback(
      std::any callback, 
      std::vector<std::any> deps) override;
    
    /**
     * mountRef
     * @source:2610-2620 mountRef
     */
    std::shared_ptr<std::any> mountRef(std::any initialValue) override;
    
    // =========================================================================
    // Update 实现
    // =========================================================================
    
    /**
     * updateState
     * @source:1937-1945 updateState
     */
    std::pair<std::any, std::function<void(std::any)>> updateState(std::any) override;
    
    /**
     * updateReducer
     * @source:1302-1530 updateReducer
     */
    std::pair<std::any, std::function<void(std::any)>> updateReducer(
        std::function<std::any(std::any, std::any)> reducer,
        std::any,
        std::optional<std::function<std::any(std::any)>>
    ) override;
    
    /**
     * updateEffect
     * @source:2705-2730 updateEffect
     */
    void updateEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override;
    
    /**
     * updateLayoutEffect
     * @source:2785-2810 updateLayoutEffect
     */
    void updateLayoutEffect(
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    ) override;
    
    /**
     * updateMemo
     * @source:2950-2985 updateMemo
     */
    std::any updateMemo(
        std::function<std::any()> create,
        std::optional<std::vector<std::any>> deps
    ) override;
    
    /**
     * updateCallback
     * @source:2895-2915 updateCallback
     */
    std::any updateCallback(std::any callback, std::vector<std::any> deps) override;
    
    /**
     * updateRef
     * @source:2625-2630 updateRef
     */
    std::shared_ptr<std::any> updateRef(std::any) override;
    
    // =========================================================================
    // Effect 辅助函数
    // =========================================================================
    
    /**
     * mountEffectImpl
     * @source:2630-2660 mountEffectImpl
     */
    void mountEffectImpl(
        Flags fiberFlags,
        HookFlags hookFlags,
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * updateEffectImpl
     * @source:2665-2710 updateEffectImpl
     */
    void updateEffectImpl(
        Flags fiberFlags,
        HookFlags hookFlags,
        std::function<std::function<void()>()> create,
        std::optional<std::vector<std::any>> deps
    );
    
    /**
     * pushEffect
     * @source:2560-2600 pushEffect
     */
    HookEffectRef pushEffect(
        HookFlags tag,
        std::function<std::function<void()>()> create,
        std::any inst,
        std::optional<std::vector<std::any>> deps
    ) override;
    
    // =========================================================================
    // 依赖比较
    // =========================================================================
    
    /**
     * 比较 Hook 输入是否相等
     * @source:455-500 areHookInputsEqual
     */
    bool areHookInputsEqual(
        const std::vector<std::any>& nextDeps,
        const std::vector<std::any>& prevDeps
    ) override;
    
    // =========================================================================
    // Dispatch 函数
    // =========================================================================
    
    void dispatchAction(
        FiberRef fiber,
        std::shared_ptr<void> queue,
        std::any action
    ) override;
    
    /**
     * dispatchSetState
     * @source:3350-3450 dispatchSetState
     */
    void dispatchSetState(
        FiberRef fiber,
        std::shared_ptr<void> queuePtr,
        std::any action
    ) override;
    
    /**
     * dispatchReducerAction
     * @source:3450-3520 dispatchReducerAction
     */
    void dispatchReducerAction(
        FiberRef fiber,
        std::shared_ptr<void> queuePtr,
        std::any action
    ) override;
    
    // =========================================================================
    // 辅助函数
    // =========================================================================
    
    /**
     * 请求更新 Lane
     */
    Lane requestUpdateLane();
    
    /**
     * 调度 Fiber 更新 (需要连接到 WorkLoop)
     */
    void scheduleUpdateOnFiber(FiberRef fiber, Lane lane);
    
    // =========================================================================
    // Dispatcher 初始化
    // =========================================================================
    
    void initializeDispatchers() override;
    
    // =========================================================================
    // 其他 Hooks 实现
    // =========================================================================
    
    /**
     * mountTransition
     * @source:3300-3320 mountTransition
     */
    std::pair<bool, std::function<void(std::function<void()>)>> mountTransition();
    
    /**
     * updateTransition
     */
    std::pair<bool, std::function<void(std::function<void()>)>> updateTransition();
    
    /**
     * mountDeferredValue
     * @source:2990-3020 mountDeferredValue
     */
    std::any mountDeferredValue(std::any value, std::optional<std::any> initialValue);
    
    /**
     * updateDeferredValue
     */
    std::any updateDeferredValue(std::any value);
    
    /**
     * mountId
     * @source:3470-3510 mountId
     */
    std::string mountId();
    
    /**
     * updateId
     */
    std::string updateId();
    
    /**
     * 无效 Hook 调用错误
     */
    static void throwInvalidHookError();
};

} // namespace react::reconciler
