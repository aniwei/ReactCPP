#pragma once

#include "ReactScheduler/Scheduler.h"

namespace react {

// Priority Level Constants
// These match the JavaScript SchedulerPriorities exactly
inline constexpr SchedulerPriority NoPriority = SchedulerPriority::NoPriority;
inline constexpr SchedulerPriority ImmediatePriority = SchedulerPriority::ImmediatePriority;
inline constexpr SchedulerPriority UserBlockingPriority = SchedulerPriority::UserBlockingPriority;
inline constexpr SchedulerPriority NormalPriority = SchedulerPriority::NormalPriority;
inline constexpr SchedulerPriority LowPriority = SchedulerPriority::LowPriority;
inline constexpr SchedulerPriority IdlePriority = SchedulerPriority::IdlePriority;

// Priority Level Utilities
constexpr bool isValidPriority(SchedulerPriority priority) {
  return priority >= SchedulerPriority::NoPriority && priority <= SchedulerPriority::IdlePriority;
}

constexpr bool isHigherPriority(SchedulerPriority a, SchedulerPriority b) {
  return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
}

constexpr bool isLowerPriority(SchedulerPriority a, SchedulerPriority b) {
  return static_cast<uint8_t>(a) > static_cast<uint8_t>(b);
}

constexpr bool isEqualPriority(SchedulerPriority a, SchedulerPriority b) {
  return a == b;
}

// Priority to timeout mapping (in milliseconds)
// These values match the JavaScript Scheduler implementation
constexpr double priorityToTimeout(SchedulerPriority priority) {
  switch (priority) {
    case SchedulerPriority::ImmediatePriority:
      return -1.0; // Never timeout
    case SchedulerPriority::UserBlockingPriority:
      return 250.0;
    case SchedulerPriority::NormalPriority:
      return 5000.0;
    case SchedulerPriority::LowPriority:
      return 10000.0;
    case SchedulerPriority::IdlePriority:
      return 1073741823.0; // maxSigned31BitInt
    case SchedulerPriority::NoPriority:
    default:
      return 5000.0; // Default to normal priority timeout
  }
}

// Priority names for debugging
constexpr const char* priorityName(SchedulerPriority priority) {
  switch (priority) {
    case SchedulerPriority::NoPriority:
      return "NoPriority";
    case SchedulerPriority::ImmediatePriority:
      return "ImmediatePriority";
    case SchedulerPriority::UserBlockingPriority:
      return "UserBlockingPriority";
    case SchedulerPriority::NormalPriority:
      return "NormalPriority";
    case SchedulerPriority::LowPriority:
      return "LowPriority";
    case SchedulerPriority::IdlePriority:
      return "IdlePriority";
    default:
      return "Unknown";
  }
}

} // namespace react