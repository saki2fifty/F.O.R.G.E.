#pragma once
#include "json_row_index.hpp"
#include <forge/render_scene.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace forge::detail {
enum class CameraViewModifier { Aim, Follow };
struct CameraViewFailure : std::runtime_error {
    EntityId camera;
    CameraViewModifier modifier;
    CameraViewFailure(EntityId source, CameraViewModifier kind, std::string message)
        : std::runtime_error(std::move(message)), camera(source), modifier(kind) {}
    const char* component() const {
        return modifier == CameraViewModifier::Aim ? "forge.camera_aim" : "forge.camera_follow";
    }
    const char* category() const {
        return modifier == CameraViewModifier::Aim ? "render.camera_aim.invalid"
                                                   : "render.camera_follow.invalid";
    }
    static const char* projection_key(CameraViewModifier kind) {
        return kind == CameraViewModifier::Aim ? "camera_aim_error" : "camera_follow_error";
    }
    const char* projection_key() const { return projection_key(modifier); }
};
struct CameraAimFailure : CameraViewFailure {
    CameraAimFailure(EntityId source, std::string message)
        : CameraViewFailure(source, CameraViewModifier::Aim, std::move(message)) {}
};
struct CameraFollowFailure : CameraViewFailure {
    CameraFollowFailure(EntityId source, std::string message)
        : CameraViewFailure(source, CameraViewModifier::Follow, std::move(message)) {}
};
// Borrow one immutable Scene's existing index. Targets use node poses, never
// other camera views; there is no recursive target evaluation or pose cache.
RenderCamera snapshot_camera_view(AssetId scene, EntityId camera, Camera settings,
                                  const JsonRowIndex&, bool use_projected = true);
} // namespace forge::detail
