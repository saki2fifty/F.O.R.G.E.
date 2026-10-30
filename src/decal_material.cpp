#include "decal_material.hpp"
namespace forge {
DecalMaterial prepare_decal_material(const MaterialData& source,
                                     const MaterialTextureBindings& bindings) {
    validate_material_bindings(source, bindings);
    const auto profile = prepare_pbr_material(source);
    DecalMaterial result;
    result.color = profile.values.parameters.at("baseColorFactor").value;
    result.alpha = source.alpha;
    result.cutoff = source.alpha_cutoff;
    const auto* role =
        profile.workflow == PbrWorkflow::SpecularGlossiness ? "diffuseTexture" : "baseColorTexture";
    if (const auto slot = profile.values.textures.find(role);
        slot != profile.values.textures.end()) {
        result.texture = bindings.at(role);
        result.slot = slot->second;
        result.uv = material_texture_transform_rows(result.slot);
    }
    return result;
}
} // namespace forge
