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
// Explicitly incomplete when skin/animation data is present; Complete validation
// rejects it until complete_model_animation has admitted the official archives.
std::vector<ArtifactFile> cook_gltf_geometry_bundle(const NativeGltfDocument& source,
                                                    const GltfModelCookOptions& options = {},
                                                    std::stop_token stop = {});
// Native worker stage for model geometry, material variants, camera/light values
// and node flags. Animation/skin realization still requires its separate stages.
std::vector<ArtifactFile> cook_static_gltf_bundle(const NativeGltfDocument& source,
                                                  const GltfModelCookOptions& options = {},
                                                  std::stop_token stop = {});
} // namespace forge::asset_detail
