#pragma once

#include <cstdint>
#include <optional>

namespace react {

// Test-only mirror of ReactJS `FiberRoot` fields.
// Purpose: enforce 1:1 field parity with reactjs/packages/react-reconciler/src/ReactInternalTypes.js
// via scripts/check-parity.js.
struct FiberRootNode {
  // BaseFiberRootProperties
  std::uint32_t tag;
  void* containerInfo;
  void* pendingChildren;
  void* current;

  void* pingCache;

  std::uint64_t timeoutHandle;
  void* cancelPendingCommit;
  void* context;
  void* pendingContext;

  FiberRootNode* next;

  void* callbackNode;
  std::uint32_t callbackPriority;
  void* expirationTimes;
  void* hiddenUpdates;

  std::uint32_t pendingLanes;
  std::uint32_t suspendedLanes;
  std::uint32_t pingedLanes;
  std::uint32_t warmLanes;
  std::uint32_t expiredLanes;
  std::uint32_t indicatorLanes;
  std::uint32_t errorRecoveryDisabledLanes;
  std::uint32_t shellSuspendCounter;

  std::uint32_t entangledLanes;
  void* entanglements;

  void* pooledCache;
  std::uint32_t pooledCacheLanes;

  void* identifierPrefix;

  void* onUncaughtError;
  void* onCaughtError;
  void* onRecoverableError;

  void* onDefaultTransitionIndicator;
  void* pendingIndicator;

  void* formState;

  void* transitionTypes;
  void* pendingGestures;
  void* stoppingGestures;
  void* gestureClone;

  // UpdaterTrackingOnlyFiberRootProperties (DEV)
  void* memoizedUpdaters;
  void* pendingUpdatersLaneMap;

  // SuspenseCallbackOnlyFiberRootProperties
  void* hydrationCallbacks;

  // TransitionTracingOnlyFiberRootProperties
  void* transitionCallbacks;
  void* transitionLanes;
  void* incompleteTransitions;

  // ProfilerCommitHooksOnlyFiberRootProperties
  double effectDuration;
  double passiveEffectDuration;
};

} // namespace react
