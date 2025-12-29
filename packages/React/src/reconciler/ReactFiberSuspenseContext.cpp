/**
 * React Fiber Suspense Context
 *
 * @source reactjs/packages/react-reconciler/src/ReactFiberSuspenseContext.js
 */

#include "ReactFiberSuspenseContext.h"

namespace react::reconciler {

std::vector<std::any> valueStack;
size_t stackIndex = 0;

StackCursor<FiberRef> suspenseHandlerStackCursor = createCursor<FiberRef>(nullptr);
FiberRef shellBoundary = nullptr;
StackCursor<SuspenseContext> suspenseStackCursor = createCursor(DefaultSuspenseContext);

void resetStack() {
  valueStack.clear();
  stackIndex = 0;
}

FiberRef getShellBoundary() {
  return shellBoundary;
}

bool hasSuspenseListContext(SuspenseContext parentContext, SuspenseContext flag) {
  return (parentContext & flag) != 0;
}

SuspenseContext setDefaultShallowSuspenseListContext(SuspenseContext parentContext) {
  return parentContext & SubtreeSuspenseContextMask;
}

SuspenseContext setShallowSuspenseListContext(
  SuspenseContext parentContext,
  ShallowSuspenseContext shallowContext
) {
  return (parentContext & SubtreeSuspenseContextMask) | shallowContext;
}

void pushSuspenseListContext(FiberRef fiber, SuspenseContext newContext) {
  push(suspenseStackCursor, newContext, fiber);
}

void popSuspenseListContext(FiberRef fiber) {
  pop(suspenseStackCursor, fiber);
}

FiberRef getSuspenseHandler() {
  return suspenseHandlerStackCursor.current;
}

void popSuspenseHandler(FiberRef fiber) {
  pop(suspenseHandlerStackCursor, fiber);
  if (shellBoundary == fiber) {
    shellBoundary = nullptr;
  }
  popSuspenseListContext(fiber);
}

void reuseSuspenseHandlerOnStack(FiberRef fiber) {
  pushSuspenseListContext(fiber, suspenseStackCursor.current);
  push(suspenseHandlerStackCursor, getSuspenseHandler(), fiber);
}

bool isCurrentTreeHidden() {
  // 简化实现
  return false;
}

void pushPrimaryTreeSuspenseHandler(FiberRef handler) {
  auto current = handler ? handler->alternate.lock() : nullptr;

  pushSuspenseListContext(
    handler,
    setDefaultShallowSuspenseListContext(suspenseStackCursor.current)
  );

  push(suspenseHandlerStackCursor, handler, handler);

  if (shellBoundary == nullptr) {
    if (!current || isCurrentTreeHidden()) {
      shellBoundary = handler;
    }
  }
}

void pushFallbackTreeSuspenseHandler(FiberRef fiber) {
  reuseSuspenseHandlerOnStack(fiber);
}

void pushDehydratedActivitySuspenseHandler(FiberRef fiber) {
  pushSuspenseListContext(fiber, suspenseStackCursor.current);
  push(suspenseHandlerStackCursor, fiber, fiber);
  if (shellBoundary == nullptr) {
    shellBoundary = fiber;
  }
}

void pushOffscreenSuspenseHandler(FiberRef fiber) {
  if (fiber && fiber->tag == OffscreenComponent) {
    pushSuspenseListContext(fiber, suspenseStackCursor.current);
    push(suspenseHandlerStackCursor, fiber, fiber);
    if (shellBoundary == nullptr) {
      shellBoundary = fiber;
    }
  } else {
    reuseSuspenseHandlerOnStack(fiber);
  }
}

} // namespace react::reconciler
