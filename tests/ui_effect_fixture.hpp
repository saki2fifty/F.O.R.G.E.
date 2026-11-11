#pragma once
#include <forge/surface_shader.hpp>

namespace forge::test {
inline SurfaceShaderDefinition ui_effect_definition() {
    SurfaceShaderDefinition value;
    value.ui_effect = true;
    value.uv_sets = {0};
    value.parameters["tint"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    value.parameters["speed"] = {MaterialParameterType::Scalar, {0}};
    return value;
}
inline nlohmann::json ui_effect_document(AssetId asset) {
    return {{"format", "forge.shader"},
            {"version", 2},
            {"asset_id", asset},
            {"source_root", "Shaders"},
            {"surface", surface_definition_document(ui_effect_definition())},
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
} // namespace forge::test
