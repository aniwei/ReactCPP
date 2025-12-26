#pragma once

#include <cstdint>

namespace react {

// Test-only mirrors for ReactJS Flow types in ReactInternalTypes.js.
// Purpose: enforce 1:1 field parity via scripts/check-parity.js.

struct ContextDependencyNode {
  void* context;
  ContextDependencyNode* next;
  void* memoizedValue;
};

struct DependenciesNode {
  std::uint32_t lanes;
  ContextDependencyNode* firstContext;
  void* _debugThenableState;
};

struct MemoCacheNode {
  void* data;
  std::uint32_t index;
};

// Mirrors ReactFiberHooks.js
struct UpdateNode {
  std::uint32_t lane;
  std::uint32_t revertLane;
  void* action;
  bool hasEagerState;
  void* eagerState;
  UpdateNode* next;
  void* gesture;
};

struct UpdateQueueNode {
  UpdateNode* pending;
  std::uint32_t lanes;
  void* dispatch;
  void* lastRenderedReducer;
  void* lastRenderedState;
};

struct HookNode {
  void* memoizedState;
  void* baseState;
  UpdateNode* baseQueue;
  void* queue;
  HookNode* next;
};

struct EffectInstanceNode {
  void* destroy;
};

struct EffectNode {
  std::uint32_t tag;
  EffectInstanceNode* inst;
  void* create;
  void* deps;
  EffectNode* next;
};

struct StoreInstanceNode {
  void* value;
  void* getSnapshot;
};

struct StoreConsistencyCheckNode {
  void* value;
  void* getSnapshot;
};

struct FunctionComponentUpdateQueueNode {
  EffectNode* lastEffect;
  void* events;
  void* stores;
  MemoCacheNode* memoCache;
};

struct EventFunctionPayloadRefNode {
  void* eventFn;
  void* impl;
};

struct EventFunctionPayloadNode {
  EventFunctionPayloadRefNode* ref;
  void* nextImpl;
};

} // namespace react
