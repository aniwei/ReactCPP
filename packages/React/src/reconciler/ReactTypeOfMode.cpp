#include "ReactTypeOfMode.h"

namespace react::reconciler {

const char* getModeName(TypeOfMode mode) {
  if (mode == NoMode) return "NoMode";
  if (mode & ConcurrentMode) return "ConcurrentMode";
  if (mode & ProfileMode) return "ProfileMode";
  if (mode & StrictLegacyMode) return "StrictLegacyMode";
  if (mode & StrictEffectsMode) return "StrictEffectsMode";
  if (mode & SuspenseyImagesMode) return "SuspenseyImagesMode";

  return "Unknown";
}

} // namespace react::reconciler
