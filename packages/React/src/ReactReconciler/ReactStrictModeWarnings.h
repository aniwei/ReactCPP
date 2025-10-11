#pragma once

namespace react {

struct FiberNode;

inline void recordLegacyContextWarning(FiberNode&, FiberNode*) {}

} // namespace react
