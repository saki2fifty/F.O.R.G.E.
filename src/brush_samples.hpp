#pragma once
#include <algorithm>
#include <cmath>
#include <forge/brush_stroke.hpp>
#include <span>
#include <stdexcept>
namespace forge::brush_detail {
inline void validate_brush(std::span<const MeshBrushSample> samples, double radius) {
    if (samples.empty() || samples.size() > 256 || !std::isfinite(radius) || radius <= 1e-4 ||
        radius > 1000)
        throw std::runtime_error("Brush stroke is empty or outside bounded settings");
    for (const auto& sample : samples) {
        for (const auto value : sample.point)
            if (!std::isfinite(value) || std::abs(value) > 100000)
                throw std::runtime_error("Brush point is nonfinite or outside editable bounds");
        if (!std::isfinite(sample.pressure) || sample.pressure < 0 || sample.pressure > 1)
            throw std::runtime_error("Brush pressure must be within [0,1]");
    }
}
inline double brush_influence(std::array<double, 3> point, std::span<const MeshBrushSample> samples,
                              double radius, MeshBrushSymmetry symmetry) {
    double influence = 0;
    for (const auto& sample : samples) {
        const auto axes = static_cast<std::uint8_t>(symmetry);
        auto difference = [&](unsigned axis) {
            // Max influence is the union of independently reflected stamps.
            return axes & (1u << axis) ? std::abs(point[axis]) - std::abs(sample.point[axis])
                                       : point[axis] - sample.point[axis];
        };
        const double dx = difference(0), dy = difference(1), dz = difference(2);
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < radius) {
            const auto falloff = 1 - distance / radius;
            influence = std::max(influence, sample.pressure * falloff * falloff);
        }
    }
    return influence;
}
} // namespace forge::brush_detail
