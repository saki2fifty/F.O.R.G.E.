#pragma once
#include "gltf_native.hpp"
#include "model_bundle.hpp"
#include "texture_import.hpp"
namespace forge::asset_detail {
struct GltfModelCookOptions {
    MeshProcessingOptions mesh;
    TextureCompression compression = TextureCompression::None;
    bool desktop_bc = false;
    unsigned maximum_texture_size = 16384;
};
// Native worker stage for static model candidates. Animation/skin/camera/light
// realization has its own required stages and is rejected by this profile.
std::vector<ArtifactFile> cook_static_gltf_bundle(const NativeGltfDocument& source,
                                                  const GltfModelCookOptions& options = {},
                                                  std::stop_token stop = {});
} // namespace forge::asset_detail
