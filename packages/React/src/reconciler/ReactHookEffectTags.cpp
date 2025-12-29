/**
 * React Hook Effect Tags
 *
 * @source reactjs/packages/react-reconciler/src/ReactHookEffectTags.js
 */

#include "ReactHookEffectTags.h"

namespace react::reconciler {

const char* getHookEffectName(HookFlags flags) {
  if (flags & HookLayout) return "useLayoutEffect";
  if (flags & HookInsertion) return "useInsertionEffect";
  if (flags & HookPassive) return "useEffect";
  return "unknown";
}

} // namespace react::reconciler
