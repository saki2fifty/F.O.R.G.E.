#pragma once
#include "json_row_index.hpp"
#include <forge/render_scene.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace forge::detail {
struct CameraAimFailure : std::runtime_error {
    EntityId camera;
    CameraAimFailure(EntityId source, std::string message)
        : std::runtime_error(std::move(message)), camera(source) {}
};
// Borrow one immutable Scene's existing index. Targets use node poses, never
// other camera views; there is no recursive target evaluation or pose cache.
RenderCamera snapshot_camera_view(AssetId scene, EntityId camera, Camera settings,
                                  const JsonRowIndex&, bool use_projected = true);
} // namespace forge::detail
