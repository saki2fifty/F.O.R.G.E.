#include "camera_shot_blend.hpp"
#include <cmath>
#include <forge/transform_animation.hpp>
#include <stdexcept>

namespace forge {
void validate_shot_camera(const Camera& camera) {
    if (camera.render_texture.id)
        throw std::runtime_error("Camera Shot requires a camera with main Game output");
}
void validate_camera_shot_blend(Camera from, Camera to) {
    validate_camera(from);
    validate_camera(to);
    validate_shot_camera(from);
    validate_shot_camera(to);
    if (from.projection == unsigned(CameraProjection::Orthographic) &&
        ((from.orthographic_width == 0) != (to.orthographic_width == 0)))
        throw std::runtime_error(
            "Camera Shot blends require matching automatic/fixed orthographic width");
    from.enabled = to.enabled = true;
    for (auto member : {&Camera::vertical_fov, &Camera::orthographic_height,
                        &Camera::orthographic_width, &Camera::near_plane, &Camera::far_plane})
        from.*member = to.*member = 1;
    if (from != to)
        throw std::runtime_error("Camera Shot blends require matching projection, basis, aspect, "
                                 "viewport, layers and clear settings");
}
RenderCamera blend_camera_shot(const RenderCamera& from, const RenderCamera& to, double weight) {
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        throw std::runtime_error("Camera Shot blend weight must be within zero to one");
    validate_camera_shot_blend(from.camera, to.camera);
    const auto first = camera_rigid_transform(from.world);
    const auto second = camera_rigid_transform(to.world);
    auto result = to;
    if (weight == 1)
        return result;
    if (weight == 0) {
        result.world = from.world;
        result.camera = from.camera;
        result.camera.enabled = to.camera.enabled;
        return result;
    }
    auto pose = interpolate_animation_poses(decompose(first), decompose(second), weight);
    // std::lerp retains finite large-world endpoints without an overflowing difference.
    pose.translation = {std::lerp(first.m[3], second.m[3], weight),
                        std::lerp(first.m[7], second.m[7], weight),
                        std::lerp(first.m[11], second.m[11], weight)};
    result.world = affine_transform(pose);
    for (auto member : {&Camera::vertical_fov, &Camera::orthographic_height,
                        &Camera::orthographic_width, &Camera::near_plane, &Camera::far_plane})
        result.camera.*member = std::lerp(from.camera.*member, to.camera.*member, weight);
    validate_camera(result.camera);
    return result;
}
} // namespace forge
