#include "camera_shot_blend.hpp"
#include <cmath>
#include <forge/transform_animation.hpp>
#include <stdexcept>

namespace forge {
double camera_shot_blend_weight(std::uint32_t curve, double time) {
    if (!std::isfinite(time) || time < 0 || time > 1)
        throw std::runtime_error("Camera Shot blend time must be within zero to one");
    switch (CameraShotBlendCurve(curve)) {
    case CameraShotBlendCurve::Smooth:
        return time * time * (3 - 2 * time);
    case CameraShotBlendCurve::Linear:
        return time;
    case CameraShotBlendCurve::EaseIn:
        return time * time;
    case CameraShotBlendCurve::EaseOut:
        return time * (2 - time);
    }
    throw std::runtime_error("Camera Shot blend curve is unavailable");
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
