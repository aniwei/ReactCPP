#pragma once

namespace react {

struct CacheComponentState {
  void* parent{nullptr};
  void* cache{nullptr};
};

struct CacheComponentUpdateQueue {
  CacheComponentState baseState{};
};

} // namespace react
