#include "pbr_material.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
using Type = MaterialParameterType;
using Semantic = TextureSemantic;
constexpr float max_value = std::numeric_limits<float>::max();
constexpr unsigned mr = 1, sg = 2, unlit = 4, all = mr | sg | unlit;
struct Parameter {
    const char* name;
    Type type;
    std::array<float, 4> initial;
    float minimum = 0, maximum = 1;
    unsigned workflows = all;
};
constexpr Parameter parameters[]{
    {"baseColorFactor", Type::LinearColor4, {1, 1, 1, 1}},
    {"metallicFactor", Type::Scalar, {1}},
    {"roughnessFactor", Type::Scalar, {1}, 0, 1, mr | unlit},
    {"glossinessFactor", Type::Scalar, {1}, 0, 1, sg},
    {"specularFactor", Type::LinearColor3, {1, 1, 1}, 0, 1, sg},
    {"specularFactor", Type::Scalar, {1}, 0, 1, mr},
    {"emissiveFactor", Type::LinearColor3, {0, 0, 0}},
    {"emissiveStrength", Type::Scalar, {1}, 0, max_value},
    {"ior", Type::Scalar, {1.5f}, 0, max_value},
    {"dispersion", Type::Scalar, {0}, 0, max_value},
    {"occlusionStrength", Type::Scalar, {1}},
    {"normalScale", Type::Scalar, {1}, -max_value, max_value},
    {"clearcoatFactor", Type::Scalar, {0}},
    {"clearcoatRoughnessFactor", Type::Scalar, {0}},
    {"clearcoatNormalScale", Type::Scalar, {1}, -max_value, max_value},
    {"specularColorFactor", Type::LinearColor3, {1, 1, 1}, 0, max_value, mr},
    {"sheenColorFactor", Type::LinearColor3, {0, 0, 0}, 0, 1, mr},
    {"sheenRoughnessFactor", Type::Scalar, {0}, 0, 1, mr},
    {"anisotropyStrength", Type::Scalar, {0}, 0, 1, mr},
    {"anisotropyRotation", Type::Scalar, {0}, -max_value, max_value, mr},
    {"iridescenceFactor", Type::Scalar, {0}, 0, 1, mr},
    {"iridescenceIor", Type::Scalar, {1.3f}, 1, max_value, mr},
    {"iridescenceThicknessMinimum", Type::Scalar, {100}, 0, max_value, mr},
    {"iridescenceThicknessMaximum", Type::Scalar, {400}, 0, max_value, mr},
    {"transmissionFactor", Type::Scalar, {0}, 0, 1, mr},
    {"thicknessFactor", Type::Scalar, {0}, 0, max_value, mr},
    {"attenuationColor", Type::LinearColor3, {1, 1, 1}, 0, 1, mr},
    // No synthesized finite default: absence denotes infinite attenuation length.
    {"attenuationDistance", Type::Scalar, {0}, 0, max_value, mr},
};
struct Texture {
    const char* name;
    Semantic semantic;
    unsigned workflows;
};
constexpr Texture textures[]{
    {"baseColorTexture", Semantic::Color, mr | unlit},
    {"metallicRoughnessTexture", Semantic::Data, mr},
    {"diffuseTexture", Semantic::Color, sg},
    {"specularGlossinessTexture", Semantic::Color, sg},
    {"normalTexture", Semantic::Normal, mr | sg},
    {"occlusionTexture", Semantic::Data, mr | sg},
    {"emissiveTexture", Semantic::Color, mr | sg},
    {"clearcoatTexture", Semantic::Data, mr},
    {"clearcoatRoughnessTexture", Semantic::Data, mr},
    {"clearcoatNormalTexture", Semantic::Normal, mr},
    {"specularTexture", Semantic::Data, mr},
    {"specularColorTexture", Semantic::Color, mr},
    {"sheenColorTexture", Semantic::Color, mr},
    {"sheenRoughnessTexture", Semantic::Data, mr},
    {"anisotropyTexture", Semantic::Data, mr},
    {"iridescenceTexture", Semantic::Data, mr},
    {"iridescenceThicknessTexture", Semantic::Data, mr},
    {"transmissionTexture", Semantic::Data, mr},
    {"thicknessTexture", Semantic::Data, mr},
};
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error("PBR material: " + why);
}
} // namespace
PbrMaterialProfile prepare_pbr_material(const MaterialData& source) {
    validate_material(source);
    PbrMaterialProfile result;
    unsigned workflow;
    if (source.model == "forge.gltf.metallic-roughness.v1") {
        workflow = mr;
        result.workflow = PbrWorkflow::MetallicRoughness;
    } else if (source.model == "forge.gltf.specular-glossiness.v1") {
        workflow = sg;
        result.workflow = PbrWorkflow::SpecularGlossiness;
    } else {
        require(source.model == "forge.gltf.unlit.v1", "unknown built-in model: " + source.model);
        workflow = unlit;
        result.workflow = PbrWorkflow::Unlit;
    }
    result.values = source;
    result.layout.model = source.model;
    std::map<std::string_view, const Parameter*> declarations;
    for (const auto& p : parameters) {
        if (!(p.workflows & workflow))
            continue;
        declarations.emplace(p.name, &p);
        if (std::string_view(p.name) != "attenuationDistance")
            result.values.parameters.try_emplace(p.name, MaterialParameter{p.type, p.initial});
    }
    for (const auto& [name, value] : source.parameters) {
        const auto found = declarations.find(name);
        require(found != declarations.end(), "unknown parameter for selected model: " + name);
        const auto& p = *found->second;
        require(value.type == p.type, "wrong parameter type: " + name);
        for (unsigned i = 0; i < material_parameter_width(p.type); ++i)
            require(value.value[i] >= p.minimum && value.value[i] <= p.maximum,
                    "parameter outside model bounds: " + name);
        result.layout.parameters.emplace(name, p.type);
    }
    for (const auto& t : textures)
        if (t.workflows & workflow)
            result.layout.textures.emplace(t.name, MaterialTextureLayout{t.semantic});
    validate_material_layout(source, result.layout);
    for (const auto& [name, slot] : source.textures)
        require(slot.sampler.compare == TextureCompare::None,
                "comparison sampler is invalid for surface texture: " + name);
    const auto scalar = [&](const char* name) {
        return result.values.parameters.at(name).value[0];
    };
    const double ior = scalar("ior");
    require(ior == 0 || ior >= 1, "ior must be zero or at least one");
    // Evaluate the ratio in double: (large float + 1) and its square are not
    // intermediate GPU products. IOR zero is the specification's F0=1 special case.
    const auto ratio = (ior - 1) / (ior + 1);
    result.dielectric_f0 = float(ratio * ratio);
    if (source.parameters.contains("attenuationDistance"))
        require(scalar("attenuationDistance") > 0, "attenuationDistance must be positive");
    // The importer retains these common default fields for all three workflows.
    // Non-default extension effects cannot masquerade as legacy or unlit support.
    if (workflow != mr) {
        require(ior == 1.5 && scalar("dispersion") == 0 && scalar("clearcoatFactor") == 0 &&
                    scalar("clearcoatRoughnessFactor") == 0 && scalar("clearcoatNormalScale") == 1,
                "metallic-roughness extension is incompatible with this workflow");
    }
    // Iridescence thickness endpoints MAY be reversed by the exact extension.
    // Do not impose an invented minimum<=maximum restriction.
    return result;
}
bool material_transmits(const PbrMaterialProfile& profile) {
    if (profile.workflow != PbrWorkflow::MetallicRoughness)
        return false;
    const auto& p = profile.values.parameters;
    return p.at("transmissionFactor").value[0] > 0 && p.at("ior").value[0] != 0 &&
           (p.at("metallicFactor").value[0] < 1 ||
            profile.values.textures.contains("metallicRoughnessTexture"));
}
} // namespace forge
