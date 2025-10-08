#pragma once

// Simplified port of react-main/packages/react-reconciler/src/ReactFiberOffscreenComponent.js
// Provides minimal Offscreen visibility primitives required by the concurrent
// updates helpers.

#include "ReactReconciler/ReactFiberSuspenseComponent.h"
#include "ReactReconciler/ReactFiberLane.h"

#include "jsi/jsi.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace react {

using OffscreenVisibility = std::uint8_t;

inline constexpr OffscreenVisibility OffscreenVisible = 0b001;
inline constexpr OffscreenVisibility OffscreenPassiveEffectsConnected = 0b010;

enum class OffscreenMode : std::uint8_t {
	Visible = 0,
	Hidden,
	UnstableDeferWithoutHiding,
};

struct OffscreenProps {
	OffscreenMode mode{OffscreenMode::Visible};
	facebook::jsi::Value* children{nullptr};
};

struct LegacyHiddenProps {
	OffscreenMode mode{OffscreenMode::Hidden};
	facebook::jsi::Value* children{nullptr};
};

struct SpawnedCachePool {
	void* parent{nullptr};
	void* pool{nullptr};
};

struct OffscreenInstance {
	OffscreenVisibility _visibility{OffscreenVisible};
	void* _pendingMarkers{nullptr};
	void* _retryCache{nullptr};
	std::vector<const Transition*>* _transitions{nullptr};
};

struct OffscreenState {
	Lanes baseLanes{NoLanes};
	std::shared_ptr<SpawnedCachePool> cachePool{};
};

struct OffscreenQueue {
	std::vector<const Transition*>* transitions{nullptr};
	std::vector<void*>* markerInstances{nullptr};
	std::unique_ptr<RetryQueue> retryQueue{};
};

} // namespace react
