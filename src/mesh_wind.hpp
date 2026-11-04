#pragma once
#include "render_bounds.hpp"
#include <forge/mesh_wind.hpp>
namespace forge {
// Detached geometry sample retained by the ordinary instance pose/history owner.
struct MeshWindSample {
    bool enabled = false;
    Double2 displacement{};
    double base_y = 0, inverse_height = 0;
    bool operator==(const MeshWindSample&) const = default;
};
MeshWindSample sample_mesh_wind(const MeshWind&, const AffineTransform&, double seconds);
Double2 wind_displacement(const MeshWindSample&, double height_above_base);
Double3 wind_point(const MeshWindSample&, Double3 world);
// Forward Jacobian for directions; inverse transpose for normal covectors.
// Normalization belongs to the caller's existing surface-frame owner.
Double3 wind_vector(const MeshWindSample&, Double3 world, Double3 vector);
Double3 wind_normal(const MeshWindSample&, Double3 world, Double3 normal);
RenderBounds wind_bounds(const MeshWindSample&, const RenderBounds&, Double3 camera_origin);
} // namespace forge
