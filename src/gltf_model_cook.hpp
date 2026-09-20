#pragma once
#include "gltf_native.hpp"
#include "model_bundle.hpp"
#include "texture_import.hpp"
namespace forge::asset_detail {
struct GltfModelCookOptions {
    MeshProcessingOptions mesh;
    ExcessSkinInfluences skin_influences = ExcessSkinInfluences::Reject;
    TextureCompression compression = TextureCompression::None;
    bool desktop_bc = false;
    unsigned maximum_texture_size = 16384;
};
// Native worker stage for model geometry, material variants, camera/light values
// and node flags. Animation/skin realization still requires its separate stages.
std::vector<ArtifactFile> cook_static_gltf_bundle(const NativeGltfDocument& source,
                                                  const GltfModelCookOptions& options = {},
                                                  std::stop_token stop = {});
} // namespace forge::asset_detail
