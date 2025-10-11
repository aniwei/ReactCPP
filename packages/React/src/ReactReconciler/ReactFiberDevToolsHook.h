#pragma once

#include <cstddef>

namespace react {

struct FiberNode;

inline void markComponentRenderStarted(FiberNode&) {}
inline void markComponentRenderStopped() {}
inline void setIsStrictModeForDevtools(bool) {}

} // namespace react
