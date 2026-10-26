#pragma once
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
namespace forge::navigation_detail {
using MeshSpace = NavigationPlane;
inline Double3 native_coordinates(MeshSpace space, Double3 value) {
    return space == MeshSpace::PlanarXY ? Double3{value[0], 0, value[1]} : value;
}
inline Double3 world_coordinates(MeshSpace space, Double3 value, double depth = 0) {
    return space == MeshSpace::PlanarXY ? Double3{value[0], value[2], depth} : value;
}
} // namespace forge::navigation_detail
