#pragma once
#include <cmath>
#include <cstdint>
#include <forge/render_view.hpp>
#include <stdexcept>
namespace forge {
struct ReflectionCaptureSettings {
    static constexpr unsigned minimum_size = 16, maximum_size = 1024;
    unsigned size = 256;
    Double3 position{};
    double near_plane = .1, far_plane = 100;
    std::uint32_t layers = UINT32_MAX;
};
inline void validate_reflection_capture_settings(const ReflectionCaptureSettings& value) {
    if (value.size < value.minimum_size || value.size > value.maximum_size)
        throw std::invalid_argument("Reflection face size must be 16–1024 pixels");
    for (const auto coordinate : value.position)
        if (!std::isfinite(coordinate))
            throw std::invalid_argument("Reflection capture needs a finite world position");
    Camera camera;
    camera.near_plane = value.near_plane;
    camera.far_plane = value.far_plane;
    validate_camera(camera);
}
} // namespace forge
