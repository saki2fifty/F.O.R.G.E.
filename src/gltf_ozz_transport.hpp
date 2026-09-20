#pragma once
#include "gltf_native.hpp"
#include <forge/derived_cache.hpp>
namespace forge::asset_detail {
struct GltfOzzOptions {
    unsigned sampling_rate = 30;
    bool optimize = true;
    // glTF permits a single key at time zero; Ozz requires positive duration.
    // Constant-only clips use this explicit duration, without changing their pose.
    float constant_duration = 1.f;
};
struct GltfOzzTransport {
    std::vector<ArtifactFile> converter_inputs;
    // Canonical converter node index -> original candidate-local glTF node index.
    std::vector<std::size_t> nodes;
    // Expected native depth-first order; actual names/parents/rest still checked.
    std::vector<std::size_t> joint_nodes;
    nlohmann::json metadata;
};
// Private animation-only canonical input for official gltf2ozz. All source
// accessors have already passed native admission; no original URIs/extensions
// or source file paths enter the converter. Does not invoke/publish anything.
GltfOzzTransport prepare_gltf_ozz_transport(const NativeGltfDocument& source,
                                            const GltfOzzOptions& options = {},
                                            std::stop_token stop = {});
} // namespace forge::asset_detail
