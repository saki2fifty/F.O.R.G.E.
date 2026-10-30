#include "decal_material.hpp"
namespace forge {
DecalMaterial prepare_decal_material(const MaterialData& source,
                                     const MaterialTextureBindings& bindings) {
    validate_material_bindings(source, bindings);
    const auto profile = prepare_pbr_material(source);
    const auto& parameters = profile.values.parameters;
    DecalMaterial result;
    result.color = parameters.at("baseColorFactor").value;
    result.alpha = source.alpha;
    result.cutoff = source.alpha_cutoff;
    result.normal_scale = parameters.at("normalScale").value[0];
    const bool gloss = profile.workflow == PbrWorkflow::SpecularGlossiness;
    result.roughness_scale = parameters.at(gloss ? "glossinessFactor" : "roughnessFactor").value[0];
    if (gloss) {
        result.roughness_scale = -result.roughness_scale;
        result.roughness_bias = 1;
        result.roughness_component = 3;
    }
    const auto texture = [&](unsigned channel, const char* role) {
        if (const auto found = profile.values.textures.find(role);
            found != profile.values.textures.end()) {
            auto& selected = result.textures[channel];
            selected.asset = bindings.at(role);
            selected.slot = found->second;
            selected.uv = material_texture_transform_rows(selected.slot);
        }
    };
    texture(decal_color_channel, gloss ? "diffuseTexture" : "baseColorTexture");
    texture(decal_normal_channel, "normalTexture");
    texture(decal_roughness_channel,
            gloss ? "specularGlossinessTexture" : "metallicRoughnessTexture");
    return result;
}
} // namespace forge
