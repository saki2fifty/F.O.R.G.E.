#pragma once
#include "physics_collider_2d.hpp"
#include <box2d/box2d.h>

namespace forge::physics_detail {
// Pinned native value admission only; no world, body, lifetime or cache state.
b2Polygon compound_box(const BoxPart2D&);
b2Polygon compound_polygon(const PolygonPart2D&);
} // namespace forge::physics_detail
