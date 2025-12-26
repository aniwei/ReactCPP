#pragma once

#include <cstdint>

namespace react {

// Test-only mirror of ReactJS `Dependencies` fields.
// Source of truth: reactjs/packages/react-reconciler/src/ReactInternalTypes.js
struct DependenciesNode {
  // lanes: Lanes
  std::uint32_t lanes;

  // firstContext: ContextDependency<mixed> | null
  void* firstContext;

  // _debugThenableState?: null | ThenableState (DEV-only)
  void* _debugThenableState;
};

} // namespace react
