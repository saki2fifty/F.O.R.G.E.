#pragma once
#include <forge/texture_asset.hpp>
#include <map>
namespace forge {

enum class MaterialParameterType { Scalar, Vector2, Vector3, Vector4, LinearColor3, LinearColor4 };
struct MaterialParameter {
    MaterialParameterType type = MaterialParameterType::Scalar;
    // Unused lanes must be zero. Linear colors never pass through sRGB decoding.
    std::array<float, 4> value{};
    bool operator==(const MaterialParameter&) const = default;
};
unsigned material_parameter_width(MaterialParameterType type);
enum class MaterialAlpha { Opaque, Mask, Blend };
struct MaterialTextureSlot {
    TextureSemantic semantic = TextureSemantic::Data;
    TextureDimension dimension = TextureDimension::D2;
    SamplerState sampler;
    unsigned uv_set = 0;
    std::array<float, 2> offset{0, 0}, scale{1, 1};
    float rotation = 0;
    bool operator==(const MaterialTextureSlot&) const = default;
};
// Identity-neutral cooked values. The immutable catalog selection binds these
// stable texture-slot keys to typed AssetRefs. Source array positions are absent.
struct MaterialData {
    std::string model;
    MaterialAlpha alpha = MaterialAlpha::Opaque;
    float alpha_cutoff = .5f;
    bool double_sided = false, depth_test = true, depth_write = true;
    std::map<std::string, MaterialParameter> parameters;
    std::map<std::string, MaterialTextureSlot> textures;
    std::size_t resident_bytes() const;
    bool operator==(const MaterialData&) const = default;
};
using MaterialTextureBindings = std::map<std::string, AssetRef<TextureAsset>>;
struct MaterialTextureLayout {
    TextureSemantic semantic = TextureSemantic::Data;
    TextureDimension dimension = TextureDimension::D2;
    bool required = false;
};
// Compatibility input supplied by the selected material model/shader adapter.
// This does not discover shader reflection or claim GPU support by itself.
struct MaterialLayout {
    std::string model;
    std::map<std::string, MaterialParameterType> parameters;
    std::map<std::string, MaterialTextureLayout> textures;
};
void validate_material(const MaterialData& material);
void validate_material_bindings(const MaterialData& material,
                                const MaterialTextureBindings& bindings);
void validate_material_layout(const MaterialData& material, const MaterialLayout& layout);
std::vector<std::byte> encode_material(const MaterialData& material);
MaterialData decode_material(std::span<const std::byte> bytes);
} // namespace forge
