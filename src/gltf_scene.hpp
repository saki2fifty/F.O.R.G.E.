#pragma once
#include <forge/gltf_source.hpp>
namespace forge::asset_detail {
// Canonical immutable asset metadata. Does not create cameras/lights/entities.
struct GltfSceneMetadata {
    nlohmann::json cameras, lights, nodes;
};
GltfSceneMetadata gltf_scene_metadata(const GltfSourceBundle& source);
} // namespace forge::asset_detail
