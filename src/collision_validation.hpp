#pragma once
#include <forge/collision_asset.hpp>
namespace forge::collision_detail {
// Source recipes have unresolved geometry; cooked admission always requires it.
void validate(const CollisionData&, CollisionLimits, bool require_geometry);
} // namespace forge::collision_detail
