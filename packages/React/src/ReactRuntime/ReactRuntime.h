#pragma once

#include "ReactReconciler/ReactFiberAsyncAction.h"
#include "ReactReconciler/ReactFiberRootSchedulerState.h"
#include "ReactReconciler/ReactFiberWorkLoopState.h"
#include "scheduler/Scheduler.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace facebook {
namespace jsi {
class Runtime;
class Object;
class Value;
} // namespace jsi
} // namespace facebook

namespace react {

class HostInterface;
class ReactDOMInstance;
struct FiberRoot;
struct FiberNode;
struct Hook;

enum class IsomorphicIndicatorRegistrationState : std::uint8_t {
  Uninitialized = 0,
  Registered = 1,
  Disabled = 2,
};

struct AsyncActionState {
  Lane currentEntangledActionLane{NoLane};
  AsyncActionThenablePtr currentEntangledActionThenable{};
  IsomorphicIndicatorRegistrationState indicatorRegistrationState{
    IsomorphicIndicatorRegistrationState::Uninitialized};
  std::function<std::function<void()>()> isomorphicDefaultTransitionIndicator{};
  std::function<void()> pendingIsomorphicIndicator{};
  std::size_t pendingEntangledRoots{0};
  bool needsIsomorphicIndicator{false};
  FiberRoot* indicatorRegistrationRoot{nullptr};
  const std::type_info* indicatorRegistrationType{nullptr};
  const void* indicatorRegistrationToken{nullptr};
};

struct HookRuntimeState {
  FiberNode* currentlyRenderingFiber{nullptr};
  Hook* currentHook{nullptr};
  Hook* workInProgressHook{nullptr};
  Hook* firstWorkInProgressHook{nullptr};
  Hook* lastCurrentHook{nullptr};
  Lanes renderLanes{NoLanes};
  std::unique_ptr<facebook::jsi::Value> previousDispatcher{};
};

class ReactRuntime {
public:
  ReactRuntime();

  WorkLoopState& workLoopState();
  const WorkLoopState& workLoopState() const;
  RootSchedulerState& rootSchedulerState();
  const RootSchedulerState& rootSchedulerState() const;
  AsyncActionState& asyncActionState();
  const AsyncActionState& asyncActionState() const;
  HookRuntimeState& hookState();
  const HookRuntimeState& hookState() const;

  void resetWorkLoop();
  void resetRootScheduler();
  void resetHooks();

  void setHostInterface(std::shared_ptr<HostInterface> hostInterface);
  void bindHostInterface(facebook::jsi::Runtime& runtime);
  void reset();

  void setShouldAttemptEagerTransitionCallback(std::function<bool()> callback);
  [[nodiscard]] bool shouldAttemptEagerTransition() const;
  void setHydrationErrorCallback(std::function<void(const HydrationErrorInfo&)> callback);
  void notifyHydrationError(const HydrationErrorInfo& info);
  void renderRootSync(
    facebook::jsi::Runtime& runtime,
    std::uint32_t rootElementOffset,
    std::shared_ptr<ReactDOMInstance> rootContainer);
  void hydrateRoot(
    facebook::jsi::Runtime& runtime,
    std::uint32_t rootElementOffset,
    std::shared_ptr<ReactDOMInstance> rootContainer);

  void unregisterRootContainer(const ReactDOMInstance* rootContainer);

  [[nodiscard]] std::size_t getRegisteredRootCount() const;

  TaskHandle scheduleTask(
    SchedulerPriority priority,
    Task task,
    const TaskOptions& options = {});

  void cancelTask(TaskHandle handle);

  SchedulerPriority getCurrentPriorityLevel() const;

  SchedulerPriority runWithPriority(
    SchedulerPriority priority,
    const std::function<void()>& fn);

  bool shouldYield() const;

  [[nodiscard]] double now() const;

  std::shared_ptr<ReactDOMInstance> createInstance(
    facebook::jsi::Runtime& runtime,
    const std::string& type,
    const facebook::jsi::Object& props);

  std::shared_ptr<ReactDOMInstance> createTextInstance(
    facebook::jsi::Runtime& runtime,
    const std::string& text);

  void appendChild(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child);

  void removeChild(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child);

  void insertBefore(
    std::shared_ptr<ReactDOMInstance> parent,
    std::shared_ptr<ReactDOMInstance> child,
    std::shared_ptr<ReactDOMInstance> beforeChild);

  void commitUpdate(
    facebook::jsi::Runtime& runtime,
    std::shared_ptr<ReactDOMInstance> instance,
    const facebook::jsi::Object& oldProps,
    const facebook::jsi::Object& newProps,
    const facebook::jsi::Object& payload);

  void commitTextUpdate(
    std::shared_ptr<ReactDOMInstance> instance,
    const std::string& oldText,
    const std::string& newText);

  void flushAllTasksForTest();

  std::vector<HydrationErrorInfo> drainHydrationErrors();

private:
  std::shared_ptr<HostInterface> ensureHostInterface();
  void dispatchHydrationError(const HydrationErrorInfo& info);
  void registerRootContainer(const std::shared_ptr<ReactDOMInstance>& rootContainer);

  struct ScheduledTask {
    TaskHandle handle;
    SchedulerPriority priority;
    Task task;
    double readyTime{0.0};
    double timeoutTime{0.0};
    bool cancelled{false};
  };

  std::shared_ptr<HostInterface> hostInterface_{};
  std::function<void(const HydrationErrorInfo&)> hydrationErrorCallback_{};
  WorkLoopState workLoopState_{};
  RootSchedulerState rootSchedulerState_{};
  AsyncActionState asyncActionState_{};
  HookRuntimeState hookState_{};
  SchedulerPriority currentPriority_{SchedulerPriority::NormalPriority};
  std::uint64_t nextTaskId_{1};
  std::function<bool()> shouldAttemptEagerTransitionCallback_{};
  std::unordered_map<const ReactDOMInstance*, std::weak_ptr<ReactDOMInstance>> registeredRoots_{};
  std::vector<ScheduledTask> taskQueue_{};
};

namespace ReactRuntimeTestHelper {
std::size_t getRegisteredRootCount(const ReactRuntime& runtime);
bool computeHostComponentUpdatePayload(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const facebook::jsi::Value& prevProps,
  const facebook::jsi::Value& nextProps,
  facebook::jsi::Value& outPayload);
bool computeHostTextUpdatePayload(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const facebook::jsi::Value& prevText,
  const facebook::jsi::Value& nextText,
  facebook::jsi::Value& outPayload);
std::shared_ptr<FiberNode> cloneFiberForReuse(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const std::shared_ptr<FiberNode>& current,
  const facebook::jsi::Value& nextProps,
  const facebook::jsi::Value& nextState);
void commitMutationEffects(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const std::shared_ptr<FiberNode>& root);
void reconcileChildren(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  const std::shared_ptr<FiberNode>& parent,
  const std::shared_ptr<FiberNode>& currentFirstChild,
  const facebook::jsi::Value& newChildren);
}

} // namespace react
