#include <cstdlib>

namespace react::test {
bool runUpdateQueueTests();
bool runReactFiberLaneRuntimeTests();
bool runReactFiberConcurrentUpdatesRuntimeTests();
bool runReactFiberRuntimeTests();
bool runReactFiberWorkLoopStateTests();
bool runReactFiberAsyncActionTests();
bool runReactFiberRootSchedulerTests();
bool runReactJSXRuntimeTests();
bool runReactRuntimeHostInterfaceTests();
}

int main() {
    bool allPassed = true;
    allPassed &= react::test::runUpdateQueueTests();
    allPassed &= react::test::runReactFiberLaneRuntimeTests();
    allPassed &= react::test::runReactFiberConcurrentUpdatesRuntimeTests();
    allPassed &= react::test::runReactFiberRuntimeTests();
    allPassed &= react::test::runReactFiberWorkLoopStateTests();
    allPassed &= react::test::runReactFiberAsyncActionTests();
    allPassed &= react::test::runReactFiberRootSchedulerTests();
    allPassed &= react::test::runReactJSXRuntimeTests();
    allPassed &= react::test::runReactRuntimeHostInterfaceTests();
    return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
