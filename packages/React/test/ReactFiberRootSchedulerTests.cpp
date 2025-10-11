#include "ReactReconciler/ReactFiberRootScheduler.h"
#include "ReactReconciler/ReactFiberLane.h"
#include "ReactRuntime/ReactRuntime.h"
#include "shared/ReactSharedInternals.h"
#include "TestRuntime.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace react::test {

namespace {

void initializeReactInternals(facebook::jsi::Runtime& jsRuntime, facebook::jsi::Object& internalsOut) {
  auto& rt = jsRuntime;
  facebook::jsi::Object reactModule = facebook::jsi::Object(rt);
  internalsOut = facebook::jsi::Object(rt);
  const std::string exportName(ReactSharedInternalsKeys::kExportName);
  reactModule.setProperty(
    rt,
    exportName.c_str(),
    facebook::jsi::Value(rt, internalsOut));
  rt.global().setProperty(rt, "React", reactModule);
}

} // namespace

extern Lanes scheduleTaskForRootDuringMicrotask(
  ReactRuntime& runtime,
  facebook::jsi::Runtime& jsRuntime,
  FiberRoot& root,
  int currentTime);

bool runReactFiberRootSchedulerTests() {
  ReactRuntime runtime;
  test::TestRuntime jsRuntime;

  facebook::jsi::Object internals(jsRuntime);
  initializeReactInternals(jsRuntime, internals);

  FiberRoot root{};
  markRootUpdated(root, DefaultLane);

  const int currentTime = static_cast<int>(runtime.now());

  const Lanes scheduledLanes = scheduleTaskForRootDuringMicrotask(runtime, jsRuntime, root, currentTime);
  assert(scheduledLanes != NoLanes);
  assert(root.callbackNode);
  assert(root.callbackPriority == getHighestPriorityLane(root.pendingLanes));
  assert(runtime.rootSchedulerState().actCallbacks.empty());

  const TaskHandle initialHandle = root.callbackNode;

  facebook::jsi::Array actQueue = jsRuntime.createArray(0);
  const std::string actQueueProp(ReactSharedInternalsKeys::kActQueue);
  internals.setProperty(
    jsRuntime,
    actQueueProp.c_str(),
    facebook::jsi::Value(jsRuntime, actQueue));

  const Lanes rescheduledLanes = scheduleTaskForRootDuringMicrotask(runtime, jsRuntime, root, currentTime);
  assert(rescheduledLanes != NoLanes);
  assert(root.callbackNode);
  assert(root.callbackPriority == getHighestPriorityLane(root.pendingLanes));
  assert(root.callbackNode != initialHandle);
  constexpr std::uint64_t kActCallbackBit = 1ull << 63;
  assert((root.callbackNode.id & kActCallbackBit) != 0);
  assert(!runtime.rootSchedulerState().actCallbacks.empty());

  return true;
}

} // namespace react::test
