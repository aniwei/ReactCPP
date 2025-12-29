#include "SchedulerPriorities.h"

namespace react::scheduler {

const char* getPriorityName(PriorityLevel priority) {
  switch (priority) {
    case NoPriority:
      return "NoPriority";
    case ImmediatePriority:
      return "ImmediatePriority";
    case UserBlockingPriority:
      return "UserBlockingPriority";
    case NormalPriority:
      return "NormalPriority";
    case LowPriority:
      return "LowPriority";
    case IdlePriority:
      return "IdlePriority";
    default:
      return "Unknown";
  }
}

} // namespace react::scheduler
