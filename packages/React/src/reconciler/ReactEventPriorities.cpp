#include "ReactEventPriorities.h"

namespace react::reconciler {

EventPriority lanesToEventPriority(Lanes lanes) {
  Lane lane = getHighestPriorityLane(lanes);

  if (!isHigherEventPriority(DiscreteEventPriority, lane)) {
    return DiscreteEventPriority;
  }
  if (!isHigherEventPriority(ContinuousEventPriority, lane)) {
    return ContinuousEventPriority;
  }
  if (includesNonIdleWork(lane)) {
    return DefaultEventPriority;
  }
  return IdleEventPriority;
}

} // namespace react::reconciler
