#pragma once
#include "navigation_geometry.hpp"
#include <forge/scene.hpp>

namespace forge::navigation_detail {
// Copies only navigation inputs from this Scene instance. The existing transform
// owner evaluates matrices, and the shared bake validator supplies source rules.
Sources capture_native(WorldContext&, const Scene&, const flecs::query<const PersistentEntityId>&);
} // namespace forge::navigation_detail
