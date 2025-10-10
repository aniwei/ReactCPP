#pragma once

namespace react {

bool isCurrentUpdateNested();
void markNestedUpdateScheduled();
void resetNestedUpdateFlag();
void syncNestedUpdateFlag();

} // namespace react
