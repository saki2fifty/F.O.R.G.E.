#pragma once
#include <array>
#include <forge/gltf_source.hpp>
#include <forge/texture_asset.hpp>
#include <map>
#include <variant>
namespace forge::asset_detail {
class NativeGltfDocument;
using GltfMaterialValue = std::variant<float, std::array<float, 3>, std::array<float, 4>>;
struct GltfMaterialFactors {
    std::string workflow, alpha_mode;
    bool double_sided = false;
    std::map<std::string, GltfMaterialValue> values;
};
GltfMaterialFactors gltf_material_factors(const NativeGltfDocument& source, std::size_t material);

struct GltfTextureBinding {
    std::string role;
    std::size_t texture = 0, image = 0;
    TextureSemantic semantic = TextureSemantic::Data;
    SamplerState sampler;
    unsigned uv_set = 0;
    std::array<float, 2> offset{0, 0}, scale{1, 1};
    float rotation = 0;
    // Selected source encoding capability, not a promise of successful decoding.
    std::string image_extension;
};
struct GltfMaterialVariant {
    std::string name;
    // Candidate-local (mesh, primitive) -> material, never durable identities.
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> mappings;
};
std::vector<GltfMaterialVariant> gltf_material_variants(const GltfSourceBundle& source);
void validate_gltf_surfaces(const GltfSourceBundle& source);
// Candidate-local texture/image addresses; durable identity is assigned later.
std::vector<GltfTextureBinding> gltf_texture_bindings(const GltfSourceBundle& source,
                                                      std::size_t material);
} // namespace forge::asset_detail
