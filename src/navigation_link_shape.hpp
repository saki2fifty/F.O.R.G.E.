#pragma once
#include "navigation_space.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
namespace forge::navigation_detail {
inline void validate_link_shape(const std::array<float, 3>& start, const std::array<float, 3>& end,
                                float radius, MeshSpace space) {
    if (space != MeshSpace::Spatial3D && space != MeshSpace::PlanarXY)
        throw std::runtime_error("Unsupported navigation link plane");
    for (const auto& endpoint : {start, end}) {
        for (float value : endpoint)
            if (!std::isfinite(value) || std::abs(value) > 4090)
                throw std::runtime_error("Navigation link endpoint exceeds bounds");
        if (space == MeshSpace::PlanarXY && endpoint[2] != 0)
            throw std::runtime_error("XY navigation link endpoints must have zero depth");
    }
    if (!std::isfinite(radius) || radius < .05f || radius > 5)
        throw std::runtime_error("Navigation link endpoint radius must be within 0.05-5 metres");
    double squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto difference = double(end[axis]) - start[axis];
        squared += difference * difference;
    }
    constexpr auto minimum_length = double(.01f);
    if (squared < minimum_length * minimum_length)
        throw std::runtime_error("Navigation link endpoints must be at least 0.01 metres apart");
}
} // namespace forge::navigation_detail
