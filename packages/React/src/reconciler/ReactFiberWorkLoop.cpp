#include "ReactFiberWorkLoop.h"

#include <stdexcept>

#include "ReactFiberBeginWork.h"
#include "ReactFiberCompleteWork.h"

#include "../runtime/ReactHostRuntime.h"

namespace react::reconciler {

ExecutionContext operator|(ExecutionContext a, ExecutionContext b) {
  return static_cast<ExecutionContext>(
    static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
  );
}

ExecutionContext operator&(ExecutionContext a, ExecutionContext b) {
  return static_cast<ExecutionContext>(
    static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
  );
}

ExecutionContext operator~(ExecutionContext a) {
  return static_cast<ExecutionContext>(~static_cast<uint8_t>(a));
}

bool hasContext(ExecutionContext context, ExecutionContext flag) {
  return (static_cast<uint8_t>(context) & static_cast<uint8_t>(flag)) != 0;
}

void WorkLoopState::reset() {
  executionContext = ExecutionContext::NoContext;
  workInProgressRoot = nullptr;
  workInProgress = nullptr;
  workInProgressRootRenderLanes = NoLanes;
  workInProgressSuspendedReason = SuspendedReason::NotSuspended;
  workInProgressThrownValue = std::any{};
  workInProgressRootDidSkipSuspendedSiblings = false;
  workInProgressRootIsPrerendering = false;
  workInProgressRootDidAttachPingListener = false;
  entangledRenderLanes = NoLanes;
  workInProgressRootExitStatus = RootExitStatus::InProgress;
  workInProgressRootSkippedLanes = NoLanes;
  workInProgressRootInterleavedUpdatedLanes = NoLanes;
  workInProgressRootRenderPhaseUpdatedLanes = NoLanes;
  workInProgressRootPingedLanes = NoLanes;
  workInProgressDeferredLane = NoLane;
  workInProgressSuspendedRetryLanes = NoLanes;
  workInProgressRootConcurrentErrors.clear();
  workInProgressRootRecoverableErrors.clear();
  workInProgressRootDidIncludeRecursiveRenderUpdate = false;
  didIncludeCommitPhaseUpdate = false;
}

WorkLoopState& ReactFiberWorkLoop::getState() {
  return state_;
}

const WorkLoopState& ReactFiberWorkLoop::getState() const {
  return state_;
}

ExecutionContext ReactFiberWorkLoop::getExecutionContext() const {
  return state_.executionContext;
}

FiberRef ReactFiberWorkLoop::getWorkInProgress() const {
  return state_.workInProgress;
}

FiberRootRef ReactFiberWorkLoop::getWorkInProgressRoot() const {
  return state_.workInProgressRoot;
}

Lanes ReactFiberWorkLoop::getEntangledRenderLanes() const {
  return state_.entangledRenderLanes;
}

bool ReactFiberWorkLoop::isRendering() const {
  return hasContext(state_.executionContext, ExecutionContext::RenderContext);
}

bool ReactFiberWorkLoop::isCommitting() const {
  return hasContext(state_.executionContext, ExecutionContext::CommitContext);
}

double ReactFiberWorkLoop::now() const {
  return scheduler_.now ? scheduler_.now() : 0.0;
}

bool ReactFiberWorkLoop::shouldYield() const {
  return scheduler_.shouldYield ? scheduler_.shouldYield() : false;
}

ReactFiberWorkLoop::ReactFiberWorkLoop(
  WorkLoopState& state,
  SchedulerInterface scheduler,
  std::shared_ptr<ReactFiberBeginWork> beginWorkHandler,
  std::shared_ptr<ReactFiberCompleteWork> completeWorkHandler,
  std::shared_ptr<ReactFiberCommitWork> commitWorkHandler
) : state_(state),
    scheduler_(std::move(scheduler)),
    beginWork_(std::move(beginWorkHandler)),
    completeWork_(std::move(completeWorkHandler)),
    commitWork_(std::move(commitWorkHandler)) {
  if (!beginWork_) {
    beginWork_ = std::make_shared<ReactFiberBeginWork>();
  }
  if (!completeWork_) {
    completeWork_ = std::make_shared<ReactFiberCompleteWork>();
  }
}

void ReactFiberWorkLoop::markRenderStarted(Lanes lanes) {
  state_.workInProgressRootRenderLanes = lanes;
  state_.workInProgressRootExitStatus = RootExitStatus::InProgress;
}

void ReactFiberWorkLoop::markRenderStopped() {
  state_.executionContext = state_.executionContext & ~ExecutionContext::RenderContext;
}

void ReactFiberWorkLoop::handleError(FiberRootRef /*root*/, std::any thrownValue) {
  state_.workInProgressRootExitStatus = RootExitStatus::Errored;
  state_.workInProgressThrownValue = std::move(thrownValue);
}

void ReactFiberWorkLoop::resumeSuspendedWork(FiberRef /*fiber*/) {
  // TODO: suspense/ping integration
}

double ReactFiberWorkLoop::requestEventTime() {
  return now();
}

Lane ReactFiberWorkLoop::requestUpdateLane(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRef /*fiber*/
) {
  // Simplified: always sync lane.
  return SyncLane;
}

void ReactFiberWorkLoop::scheduleUpdateOnFiber(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRootRef /*root*/,
  FiberRef /*fiber*/,
  Lane /*lane*/
) {
  // TODO: root scheduler integration
}

void ReactFiberWorkLoop::performSyncWorkOnRoot(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRootRef root,
  Lanes lanes
) {
  (void)hostRuntime;
  renderRootSync(jsiRuntime, hostRuntime, std::move(root), lanes, false);
}

void ReactFiberWorkLoop::performConcurrentWorkOnRoot(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRootRef root,
  bool /*didTimeout*/
) {
  // Simplified: treat as concurrent render with current pending lanes.
  renderRootConcurrent(jsiRuntime, hostRuntime, std::move(root), state_.workInProgressRootRenderLanes);
}

void ReactFiberWorkLoop::ensureRootIsScheduled(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRootRef /*root*/
) {
  // TODO: hook up to SchedulerInterface
}

void ReactFiberWorkLoop::prepareFreshStack(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRootRef root,
  Lanes lanes
) {
  state_.workInProgressRoot = std::move(root);
  markRenderStarted(lanes);

  if (state_.workInProgressRoot && state_.workInProgressRoot->current) {
    state_.workInProgress = createWorkInProgress(
      jsiRuntime,
      state_.workInProgressRoot->current,
      state_.workInProgressRoot->current->pendingProps
    );
  } else {
    state_.workInProgress = nullptr;
  }
}

RootExitStatus ReactFiberWorkLoop::renderRootConcurrent(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRootRef root,
  Lanes lanes
) {
  ExecutionContext prevContext = state_.executionContext;
  state_.executionContext = prevContext | ExecutionContext::RenderContext;

  prepareFreshStack(jsiRuntime, hostRuntime, std::move(root), lanes);

  try {
    workLoopConcurrent(jsiRuntime, hostRuntime, true);
  } catch (const std::exception& e) {
    handleError(state_.workInProgressRoot, std::any{std::string(e.what())});
  } catch (...) {
    handleError(state_.workInProgressRoot, std::any{});
  }

  state_.executionContext = prevContext;
  return state_.workInProgress ? RootExitStatus::InProgress : RootExitStatus::Completed;
}

RootExitStatus ReactFiberWorkLoop::renderRootSync(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRootRef root,
  Lanes lanes,
  bool /*exitOnSpawn*/
) {
  ExecutionContext prevContext = state_.executionContext;
  state_.executionContext = prevContext | ExecutionContext::RenderContext;

  prepareFreshStack(jsiRuntime, hostRuntime, std::move(root), lanes);

  try {
    workLoopSync(jsiRuntime, hostRuntime);
    state_.workInProgressRootExitStatus = RootExitStatus::Completed;
  } catch (const std::exception& e) {
    handleError(state_.workInProgressRoot, std::any{std::string(e.what())});
  } catch (...) {
    handleError(state_.workInProgressRoot, std::any{});
  }

  state_.executionContext = prevContext;
  return state_.workInProgressRootExitStatus;
}

void ReactFiberWorkLoop::workLoopConcurrent(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  bool /*nonIdle*/
) {
  while (state_.workInProgress != nullptr && !shouldYield()) {
    performUnitOfWork(jsiRuntime, hostRuntime, state_.workInProgress);
  }
}

void ReactFiberWorkLoop::workLoopSync(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime
) {
  while (state_.workInProgress != nullptr) {
    performUnitOfWork(jsiRuntime, hostRuntime, state_.workInProgress);
  }
}

void ReactFiberWorkLoop::performUnitOfWork(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRef unitOfWork
) {
  if (!unitOfWork) {
    state_.workInProgress = nullptr;
    return;
  }

  FiberRef current = unitOfWork->getAlternate();
  BeginWorkResult next = beginWork_ ? beginWork_->beginWork(
    current,
    unitOfWork,
    state_.workInProgressRootRenderLanes
  ) : nullptr;

  unitOfWork->memoizedProps = std::move(unitOfWork->pendingProps);

  if (next != nullptr) {
    state_.workInProgress = next;
  } else {
    completeUnitOfWork(jsiRuntime, hostRuntime, unitOfWork);
  }
}

void ReactFiberWorkLoop::completeUnitOfWork(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRef unitOfWork
) {
  FiberRef completedWork = std::move(unitOfWork);

  do {
    FiberRef current = completedWork->getAlternate();
    if (completeWork_) {
      (void)completeWork_->completeWork(current, completedWork, state_.workInProgressRootRenderLanes);
    }

    if (completedWork->sibling != nullptr) {
      state_.workInProgress = completedWork->sibling;
      return;
    }

    completedWork = completedWork->getReturn();
    state_.workInProgress = completedWork;
  } while (completedWork != nullptr);

  state_.workInProgress = nullptr;
  state_.workInProgressRootExitStatus = RootExitStatus::Completed;
}

void ReactFiberWorkLoop::unwindUnitOfWork(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRef /*unitOfWork*/,
  bool /*skipSiblings*/
) {
  // TODO: error/suspense unwind support
}

void ReactFiberWorkLoop::commitRoot(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRootRef /*root*/,
  Lanes /*recoverableErrors*/,
  std::any /*transitions*/,
  std::optional<std::any> /*didIncludeRenderPhaseUpdate*/,
  std::any /*spawnedLane*/,
  std::any /*updatedLanes*/,
  std::any /*suspendedRetryLanes*/
) {
  // TODO
}

void ReactFiberWorkLoop::commitRootImpl(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& /*hostRuntime*/,
  FiberRootRef /*root*/,
  Lanes /*recoverableErrors*/,
  std::any /*transitions*/,
  bool /*renderPriorityLevel*/,
  std::any /*spawnedLane*/,
  std::any /*updatedLanes*/,
  std::any /*suspendedRetryLanes*/,
  bool /*includeWorkInProgressEffects*/,
  std::any /*exitStatus*/
) {
  // TODO
}

ReactFiberWorkLoop& getWorkLoop(facebook::jsi::Runtime& /*jsiRuntime*/, react::ReactHostRuntime& hostRuntime) {
  return hostRuntime.getWorkLoop();
}

void setWorkLoop(
  facebook::jsi::Runtime& /*jsiRuntime*/,
  react::ReactHostRuntime& hostRuntime,
  std::shared_ptr<ReactFiberWorkLoop> workLoop
) {
  hostRuntime.setWorkLoop(std::move(workLoop));
}

void markSkippedUpdateLanes(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  Lanes lanes
) {
  auto& workLoop = getWorkLoop(jsiRuntime, hostRuntime);
  workLoop.getState().workInProgressRootSkippedLanes = mergeLanes(
    workLoop.getState().workInProgressRootSkippedLanes,
    lanes);
}

Lanes getWorkInProgressRootRenderLanes(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime
) {
  return getWorkLoop(jsiRuntime, hostRuntime).getState().workInProgressRootRenderLanes;
}

bool isUnsafeClassRenderPhaseUpdate(
  facebook::jsi::Runtime& jsiRuntime,
  react::ReactHostRuntime& hostRuntime,
  FiberRef /*fiber*/
) {
  // 简化实现：检查是否在渲染上下文中
  auto& workLoop = getWorkLoop(jsiRuntime, hostRuntime);
  return hasContext(workLoop.getExecutionContext(), ExecutionContext::RenderContext);
}

} // namespace react::reconciler
