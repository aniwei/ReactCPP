#pragma once

namespace react {

// Test-only mirror of ReactJS `ContextDependency<T>` fields.
// Source of truth: reactjs/packages/react-reconciler/src/ReactInternalTypes.js
struct ContextDependencyNode {
  // context: ReactContext<T>
  void* context;

  // next: ContextDependency<mixed> | null
  void* next;

  // memoizedValue: T
  void* memoizedValue;
};

} // namespace react
