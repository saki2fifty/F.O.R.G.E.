#pragma once
#include <array>
#include <forge/decal.hpp>
#include <forge/render_components.hpp>
#include <forge/transform.hpp>
namespace forge {
// Scalar/cross-field admission independent of a world, device or UI. Native ECS
// producers must pass the same check again at extraction before GPU conversion.
void validate_camera(const Camera&);
void validate_light(const Light&);
void validate_decal(const DecalProjector&);
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
    float shadow_source_radius{}, shadow_angular_radius{};
    float emitter_radius{}, emitter_width{}, emitter_height{};
    Double3 right{}, up{}; // Rectangle plane; cross(right, up) opposes emission.
};
LightView light_view(const Light&, const AffineTransform&);
// Transient image coordinates owned by view extraction. Node scale does not size
// a directional image or change a spot cone; reflections retain image orientation.
struct LightImageFrame {
    Double3 position{};
    std::array<Double3, 3> rows{};
    std::uint32_t kind{};
    bool clipped = true;
};
LightImageFrame light_image_frame(const Light&, const AffineTransform&);
// Position relative to origin, then the three coordinate rows. Subtract position
// before applying rows in the shader, as for the existing positional light ray.
std::array<std::array<float, 4>, 4> light_image_gpu_frame(const LightImageFrame&, Double3 origin);
// Derived normalized projector coordinates: centered box[-.5,.5], +Z travel.
// World/inverse data is transient; the component owns only local dimensions.
struct DecalView {
    DecalProjector values;
    AffineTransform world, projector_from_world;
    Double3 forward;
    float cosine_fade_start{}, cosine_fade_end{};
};
DecalView decal_view(const DecalProjector&, const AffineTransform&);
// Camera-relative rows for GPU projection. Reject unrepresentable values per view
// without narrowing the native transform or preventing other geometry from drawing.
std::array<std::array<float, 4>, 3> decal_gpu_transform(const DecalView&, Double3 origin);
} // namespace forge
