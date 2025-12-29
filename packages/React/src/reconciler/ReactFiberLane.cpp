#include "ReactFiberLane.h"

namespace react::reconciler {

const char* getLabelForLane(Lane lane) {
  if (lane & SyncHydrationLane) return "SyncHydrationLane";
  if (lane & SyncLane) return "Sync";
  if (lane & InputContinuousHydrationLane) return "InputContinuousHydration";
  if (lane & InputContinuousLane) return "InputContinuous";
  if (lane & DefaultHydrationLane) return "DefaultHydration";
  if (lane & DefaultLane) return "Default";
  if (lane & TransitionHydrationLane) return "TransitionHydration";
  if (lane & TransitionLanes) return "Transition";
  if (lane & RetryLanes) return "Retry";
  if (lane & SelectiveHydrationLane) return "SelectiveHydration";
  if (lane & IdleHydrationLane) return "IdleHydration";
  if (lane & IdleLane) return "Idle";
  if (lane & OffscreenLane) return "Offscreen";
  if (lane & DeferredLane) return "Deferred";
  return "Unknown";
}

int clz32(uint32_t x) {
  if (x == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clz(x);
#else
  int n = 0;
  if (x <= 0x0000FFFF) {
    n += 16;
    x <<= 16;
  }
  if (x <= 0x00FFFFFF) {
    n += 8;
    x <<= 8;
  }
  if (x <= 0x0FFFFFFF) {
    n += 4;
    x <<= 4;
  }
  if (x <= 0x3FFFFFFF) {
    n += 2;
    x <<= 2;
  }
  if (x <= 0x7FFFFFFF) {
    n += 1;
  }
  return n;
#endif
}

int laneToIndex(Lane lane) {
  return 31 - clz32(lane);
}

Lane indexToLane(int index) {
  return static_cast<Lane>(1u << index);
}

Lanes getNextLanes(Lanes pendingLanes, Lanes suspendedLanes) {
  if (pendingLanes == NoLanes) {
    return NoLanes;
  }

  Lanes nextLanes = NoLanes;

  Lanes nonIdlePendingLanes = pendingLanes & NonIdleLanes;
  if (nonIdlePendingLanes != NoLanes) {
    Lanes nonIdleUnblockedLanes = nonIdlePendingLanes & ~suspendedLanes;
    if (nonIdleUnblockedLanes != NoLanes) {
      nextLanes = getHighestPriorityLanes(nonIdleUnblockedLanes);
    } else {
      nextLanes = getHighestPriorityLanes(nonIdlePendingLanes);
    }
  } else {
    Lanes unblockedLanes = pendingLanes & ~suspendedLanes;
    if (unblockedLanes != NoLanes) {
      nextLanes = getHighestPriorityLanes(unblockedLanes);
    } else {
      nextLanes = getHighestPriorityLanes(pendingLanes);
    }
  }

  return nextLanes;
}

} // namespace react::reconciler
