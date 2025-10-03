#pragma once

#include "ReactReconciler/ReactWakeable.h"

namespace react {

Wakeable& noopSuspenseyCommitThenable();
bool isNoopSuspenseyCommitThenable(const Wakeable* wakeable);
bool isNoopSuspenseyCommitThenable(const void* value);

} // namespace react
