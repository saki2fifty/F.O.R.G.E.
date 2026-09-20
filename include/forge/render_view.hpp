#pragma once
#include <array>
#include <forge/render_components.hpp>
#include <forge/transform.hpp>
namespace forge {
// Scalar/cross-field admission independent of a world, device or UI. Native ECS
// producers must pass the same check again at extraction before GPU conversion.
void validate_camera(const Camera&);
void validate_light(const Light&);
struct PixelViewport {
    std::uint32_t x{}, y{}, width{}, height{};
    bool operator==(const PixelViewport&) const = default;
};
struct CameraView {
    Double3 position, right, up, forward;
    AffineTransform view;             // World->positive-depth view. Doubles; never raw GPU memcpy.
    std::array<float, 16> projection; // Row-major, column vectors, D3D depth[0,1].
    PixelViewport viewport;
    bool orientation_reversed{}; // Relative to FORGE +Z / unflipped projection.
};
// Reject undefined orientation, empty viewport or unrepresentable projection.
// World scale affects position, never FOV/clip distance/orthographic size.
CameraView camera_view(const Camera&, const AffineTransform&, std::uint32_t target_width,
                       std::uint32_t target_height);
struct LightView {
    Double3 position, direction;
    std::array<float, 3> color;
    float intensity{}, range{}, cosine_inner{}, cosine_outer{};
    std::uint32_t kind{}, layers{};
    bool cast_shadows{};
    float shadow_bias{}, shadow_normal_bias{};
};
LightView light_view(const Light&, const AffineTransform&);
} // namespace forge
