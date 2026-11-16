#pragma once
#include <cstdint>
#include <forge/render_scene.hpp>

namespace forge {
// Shot views share output/composition intent; only pose and lens values blend.
void validate_shot_camera(const Camera&);
void validate_camera_shot_blend(Camera from, Camera to);
double camera_shot_blend_weight(std::uint32_t curve, double normalized_time);
RenderCamera blend_camera_shot(const RenderCamera& from, const RenderCamera& to, double weight);
} // namespace forge
