#include "fog_volume_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace forge {
FogVolumeSample prepare_fog_volume(const FogVolume& volume, const AffineTransform& world,
                                   Double3 camera_origin) {
    volume.validate();
    if (!std::all_of(world.m.begin(), world.m.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !std::all_of(camera_origin.begin(), camera_origin.end(),
                     [](double value) { return std::isfinite(value); }))
        throw std::invalid_argument("Fog Volume requires finite world and camera transforms");
    const double extents[]{volume.half_extent_x, volume.half_extent_y, volume.half_extent_z};
    auto canonical = world;
    Double3 relative;
    for (unsigned axis = 0; axis < 3; ++axis) {
        relative[axis] = camera_origin[axis] - world.m[4 * axis + 3];
        if (!std::isfinite(relative[axis]) || std::abs(relative[axis]) > fog_volume_max_view_offset)
            throw std::invalid_argument("Fog Volume is outside this camera's numeric range");
        canonical.m[4 * axis + 3] = 0;
        for (unsigned row = 0; row < 3; ++row)
            canonical.m[4 * row + axis] *= extents[axis];
        const double length =
            std::hypot(canonical.m[axis], canonical.m[4 + axis], canonical.m[8 + axis]);
        if (!std::isfinite(length) || length < fog_volume_min_world_extent ||
            length > fog_volume_max_world_extent)
            throw std::invalid_argument(
                "Fog Volume world half axes must be in 0.000001..1000000 metres");
    }
    const auto local = inverse(canonical);
    auto quantized = local;
    FogVolumeSample result;
    for (unsigned row = 0; row < 3; ++row) {
        const auto offset = row * 4;
        const double length = std::hypot(local.m[offset], local.m[offset + 1], local.m[offset + 2]);
        if (!std::isfinite(length) || length > 1 / fog_volume_min_world_extent)
            throw std::invalid_argument("Fog Volume inverse axes exceed the GPU numeric range");
        double origin = 0;
        for (unsigned column = 0; column < 3; ++column) {
            const auto value = float(local.m[offset + column]);
            if (!std::isfinite(value))
                throw std::invalid_argument("Fog Volume inverse cannot be represented by the GPU");
            quantized.m[offset + column] = value;
            result.rows[row][column] = value;
            origin += local.m[offset + column] * relative[column];
        }
        result.rows[row][3] = float(origin);
        if (!std::isfinite(result.rows[row][3]))
            throw std::invalid_argument(
                "Fog Volume camera offset cannot be represented by the GPU");
    }
    // Apply the existing condition policy again to the actual GPU basis. Do not
    // silently turn a valid double transform into a singular float ellipsoid.
    (void)inverse(quantized);
    result.rows[3] = {volume.color_r, volume.color_g, volume.color_b, volume.density};
    result.rows[4][0] = float(volume.shape);
    return result;
}
FogVolumeUniform fog_volume_uniform(std::span<const FogVolumeSample> volumes,
                                    const Double3* orthographic_forward) {
    if (volumes.size() > fog_volume_limit)
        throw std::invalid_argument("Fog Volume draw exceeds the 16-volume profile");
    FogVolumeUniform result{};
    result[0][0] = float(volumes.size());
    if (orthographic_forward) {
        const auto& forward = *orthographic_forward;
        const double length = std::hypot(forward[0], forward[1], forward[2]);
        if (!std::isfinite(length) || std::abs(length - 1) > .000001)
            throw std::invalid_argument("Fog Volume orthographic direction must be a unit vector");
        for (unsigned axis = 0; axis < 3; ++axis)
            result[0][1 + axis] = float(forward[axis]);
    }
    for (unsigned i = 0; i < volumes.size(); ++i) {
        const auto& rows = volumes[i].rows;
        for (const auto& row : rows)
            for (float value : row)
                if (!std::isfinite(value))
                    throw std::invalid_argument("Fog Volume draw data must be finite");
        if (rows[4][0] != 0 && rows[4][0] != 1)
            throw std::invalid_argument("Fog Volume draw shape is outside its profile");
        FogVolume value;
        value.color_r = rows[3][0];
        value.color_g = rows[3][1];
        value.color_b = rows[3][2];
        value.density = rows[3][3];
        value.shape = std::uint32_t(rows[4][0]);
        value.validate();
        std::copy(rows.begin(), rows.end(), result.begin() + 1 + i * 5);
    }
    return result;
}
} // namespace forge
