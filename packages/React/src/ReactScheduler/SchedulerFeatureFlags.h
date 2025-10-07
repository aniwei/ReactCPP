#pragma once

namespace react {

// Feature flags for Scheduler behavior
// These correspond to the JavaScript SchedulerFeatureFlags

// Profiling support
inline constexpr bool enableProfiling = false;

// Time slice configuration
inline constexpr double frameYieldMs = 5.0; // 5ms default time slice

// Priority timeout constants (in milliseconds)
inline constexpr double userBlockingPriorityTimeout = 250.0;
inline constexpr double normalPriorityTimeout = 5000.0;
inline constexpr double lowPriorityTimeout = 10000.0;
inline constexpr double maxSigned31BitInt = 1073741823.0;

// Paint request support
inline constexpr bool enableRequestPaint = true;

// Experimental features
inline constexpr bool enableAlwaysYieldScheduler = false;

// MessageChannel support for scheduling
inline constexpr bool enableMessageChannel = true;

// PostTask API support (future implementation)
inline constexpr bool enablePostTask = false;

// Task continuation support
inline constexpr bool enableTaskContinuation = true;

// Debug mode features
inline constexpr bool enableSchedulerDebugging = false;

} // namespace react