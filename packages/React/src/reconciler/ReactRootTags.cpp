/**
 * React Root Tags
 *
 * @source reactjs/packages/react-reconciler/src/ReactRootTags.js
 */

#include "ReactRootTags.h"

namespace react::reconciler {

const char* getRootTagName(RootTag tag) {
  switch (tag) {
    case LegacyRoot: return "LegacyRoot";
    case ConcurrentRoot: return "ConcurrentRoot";
    default: return "Unknown";
  }
}

} // namespace react::reconciler
