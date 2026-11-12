#pragma once
#include <forge/surface_shader.hpp>

namespace forge::test {
inline SurfaceShaderDefinition ui_effect_definition(bool textures = false) {
    SurfaceShaderDefinition value;
    value.ui_effect = true;
    value.uv_sets = {0};
    value.parameters["tint"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    value.parameters["speed"] = {MaterialParameterType::Scalar, {0}};
    if (textures) {
        auto& slot = value.textures["image"];
        slot.semantic = TextureSemantic::Color;
        slot.offset = {1, 0};
        slot.scale = {-1, 1};
        slot.sampler.min = slot.sampler.mag = slot.sampler.mip = TextureFilter::Nearest;
    }
    return value;
}
inline nlohmann::json ui_effect_document(AssetId asset, bool textures = false) {
    return {{"format", "forge.shader"},
            {"version", 2},
            {"asset_id", asset},
            {"source_root", "Shaders"},
            {"surface", surface_definition_document(ui_effect_definition(textures))},
            {"stages", nlohmann::json::array(
                           {{{"stage", "pixel"}, {"source", "ui.hlsl"}, {"entry", "UiEffect"}}})}};
}
inline constexpr std::string_view ui_effect_source = R"(
#include "engine/forge.surface.hlsli"
float4 UiEffect(ForgeUiInput input) {
    float u = frac(input.UV.x + input.Time * ForgeParameter_speed());
    return ForgeParameter_tint() * float4(u, input.UV.y, saturate(input.Size.x / 160), 1);
}
)";
inline std::string ui_texture_effect_source() {
    std::string result(ui_effect_source);
    const auto at = result.find("return ForgeParameter_tint()");
    if (at == std::string::npos)
        throw std::runtime_error("UI pixel fixture function changed");
    result.insert(at + std::string_view("return ").size(), "ForgeSample_image(input) * ");
    return result;
}
} // namespace forge::test
