#include "ReactHostRuntime.h"

#include <stdexcept>

#include "../reconciler/ReactFiberClassUpdateQueue.h"
#include "../reconciler/ReactFiberHooks.h"
#include "../reconciler/ReactFiberNewContext.h"
#include "../reconciler/ReactChildFiber.h"
#include "../reconciler/ReactFiberWorkLoop.h"
#include "../shared/ReactSharedInternals.h"

namespace react {

ReactHostRuntime::~ReactHostRuntime() = default;

bool SchedulerHost::supportsMessageChannel() {
  return true;
}

bool SchedulerHost::supportsIsInputPending() {
  return false;
}

bool SchedulerHost::isInputPending() {
  return false;
}

bool HostConfig::finalizeInitialChildren(
  Instance,
  const std::string&, 
  const facebook::jsi::Object&, 
  facebook::jsi::Runtime&
) {
  return false;
}

void HostConfig::prepareForCommit(Container) {}

void HostConfig::resetAfterCommit(Container) {}

HostConfig::PublicInstance HostConfig::getPublicInstance(Instance instance) {
  return instance;
}

bool HostConfig::supportsMutation() {
  return true;
}

bool HostConfig::supportsPersistence() {
  return false;
}

bool HostConfig::supportsHydration() {
  return false;
}

bool HostConfig::supportsMicrotasks() {
  return true;
}

void HostConfig::scheduleMicrotask(std::function<void()>) {}

int HostConfig::getNoTimeout() {
  return -1;
}

bool HostConfig::isNoTimeout(int timeout) {
  return timeout == -1;
}

ReactHostRuntime::ReactHostRuntime(
  facebook::jsi::Runtime& jsiRuntime,
  std::unique_ptr<HostConfig> hostConfig,
  std::unique_ptr<SchedulerHost> schedulerHost
) : jsiRuntime_(jsiRuntime),
    hostConfig_(std::move(hostConfig)),
    schedulerHost_(std::move(schedulerHost)),
    workLoopState_(std::make_unique<reconciler::WorkLoopState>()),
    workLoop_(nullptr),
    classUpdateQueueGlobals_(std::make_unique<reconciler::ClassUpdateQueueGlobals>()),
    fiberHooks_(nullptr),
    fiberNewContext_(std::make_unique<reconciler::ReactFiberNewContext>()),
    reconcileChildFibers_(std::make_unique<reconciler::ReactChildFiberReconciler>(true)),
    mountChildFibers_(std::make_unique<reconciler::ReactChildFiberReconciler>(false)),
    sharedInternals_(ReactSharedInternals::create()) {}

facebook::jsi::Runtime& ReactHostRuntime::getJSIRuntime() {
  return jsiRuntime_;
}

const facebook::jsi::Runtime& ReactHostRuntime::getJSIRuntime() const {
  return jsiRuntime_;
}

reconciler::WorkLoopState& ReactHostRuntime::getWorkLoopState() {
  return *workLoopState_;
}

const reconciler::WorkLoopState& ReactHostRuntime::getWorkLoopState() const {
  return *workLoopState_;
}

void ReactHostRuntime::setWorkLoop(std::shared_ptr<reconciler::ReactFiberWorkLoop> workLoop) {
  workLoop_ = std::move(workLoop);
}

reconciler::ReactFiberWorkLoop& ReactHostRuntime::getWorkLoop() {
  if (!workLoop_) {
    throw std::runtime_error("ReactFiberWorkLoop is not set on ReactHostRuntime.");
  }
  return *workLoop_;
}

const reconciler::ReactFiberWorkLoop& ReactHostRuntime::getWorkLoop() const {
  if (!workLoop_) {
    throw std::runtime_error("ReactFiberWorkLoop is not set on ReactHostRuntime.");
  }
  return *workLoop_;
}

HostConfig& ReactHostRuntime::getHostConfig() {
  return *hostConfig_;
}

const HostConfig& ReactHostRuntime::getHostConfig() const {
  return *hostConfig_;
}

SchedulerHost& ReactHostRuntime::getSchedulerHost() {
  return *schedulerHost_;
}

const SchedulerHost& ReactHostRuntime::getSchedulerHost() const {
  return *schedulerHost_;
}

double ReactHostRuntime::now() {
  return schedulerHost_->getCurrentTime();
}

bool ReactHostRuntime::shouldYield() {
  return schedulerHost_->shouldYieldToHost();
}

reconciler::ClassUpdateQueueGlobals& ReactHostRuntime::getClassUpdateQueueGlobals() {
  return *classUpdateQueueGlobals_;
}

const reconciler::ClassUpdateQueueGlobals& ReactHostRuntime::getClassUpdateQueueGlobals() const {
  return *classUpdateQueueGlobals_;
}

void ReactHostRuntime::setFiberHooks(std::shared_ptr<reconciler::ReactFiberHooks> hooks) {
  if (!hooks) {
    throw std::runtime_error("setFiberHooks received nullptr");
  }
  fiberHooks_ = std::move(hooks);
}

reconciler::ReactFiberHooks& ReactHostRuntime::getFiberHooks() {
  if (!fiberHooks_) {
    throw std::runtime_error("ReactFiberHooks is not set on ReactHostRuntime.");
  }
  return *fiberHooks_;
}

const reconciler::ReactFiberHooks& ReactHostRuntime::getFiberHooks() const {
  if (!fiberHooks_) {
    throw std::runtime_error("ReactFiberHooks is not set on ReactHostRuntime.");
  }
  return *fiberHooks_;
}

reconciler::ReactFiberNewContext& ReactHostRuntime::getFiberNewContext() {
  return *fiberNewContext_;
}

const reconciler::ReactFiberNewContext& ReactHostRuntime::getFiberNewContext() const {
  return *fiberNewContext_;
}

reconciler::ReactChildFiberReconciler& ReactHostRuntime::getReconcileChildFibers() {
  return *reconcileChildFibers_;
}

const reconciler::ReactChildFiberReconciler& ReactHostRuntime::getReconcileChildFibers() const {
  return *reconcileChildFibers_;
}

reconciler::ReactChildFiberReconciler& ReactHostRuntime::getMountChildFibers() {
  return *mountChildFibers_;
}

const reconciler::ReactChildFiberReconciler& ReactHostRuntime::getMountChildFibers() const {
  return *mountChildFibers_;
}

ReactSharedInternals& ReactHostRuntime::getSharedInternals() {
  return *sharedInternals_;
}

const ReactSharedInternals& ReactHostRuntime::getSharedInternals() const {
  return *sharedInternals_;
}

void ReactHostRuntime::setCaptureCommitPhaseError(CaptureCommitPhaseErrorFn fn) {
  captureCommitPhaseError_ = std::move(fn);
}

void ReactHostRuntime::clearCaptureCommitPhaseError() {
  captureCommitPhaseError_ = nullptr;
}

const ReactHostRuntime::CaptureCommitPhaseErrorFn* ReactHostRuntime::getCaptureCommitPhaseErrorPtr() const {
  return captureCommitPhaseError_ ? &captureCommitPhaseError_ : nullptr;
}

} // namespace react
