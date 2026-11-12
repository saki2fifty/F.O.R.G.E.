#pragma once
#include <forge/material_graph.hpp>
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
inline nlohmann::json ui_effect_graph_document(AssetId asset) {
    using Json = nlohmann::json;
    auto source = MaterialGraphSource::create_ui(asset);
    auto& graph = source.document["graph"];
    auto& tint = graph["nodes"][0];
    tint["data"]["key"] = "tint";
    tint["data"]["value"] = {1, 1, 1, 1};
    const auto tint_node = tint;
    const auto output = graph["nodes"][1];
    graph["edges"] = Json::array();
    const auto node = [&](const char* schema, Json data = Json::object()) {
        auto value = create_material_graph_node(schema, {0, 0});
        value["data"].update(data);
        graph["nodes"].push_back(value);
        return value;
    };
    const auto edge = [&](const Json& from, const Json& to, const char* port) {
        graph["edges"].push_back({{"id", GraphEdgeId::generate()},
                                  {"from", {{"node", from.at("id")}, {"port", "out"}}},
                                  {"to", {{"node", to.at("id")}, {"port", port}}}});
    };
    const auto uv = node("uv");
    const auto x = node("component", {{"from", "vector2"}, {"lane", 0}});
    const auto y = node("component", {{"from", "vector2"}, {"lane", 1}});
    edge(uv, x, "value");
    edge(uv, y, "value");
    const auto time = node("ui_time");
    const auto speed = node("parameter", {{"type", "scalar"}, {"key", "speed"}, {"value", {0}}});
    const auto motion = node("multiply", {{"type", "scalar"}});
    edge(time, motion, "a");
    edge(speed, motion, "b");
    const auto offset = node("add", {{"type", "scalar"}});
    edge(x, offset, "a");
    edge(motion, offset, "b");
    const auto fraction = node("fraction", {{"type", "scalar"}});
    edge(offset, fraction, "value");
    const auto size = node("ui_size");
    const auto width = node("component", {{"from", "vector2"}, {"lane", 0}});
    edge(size, width, "value");
    const auto reference = node("constant", {{"type", "scalar"}, {"value", {160}}});
    const auto ratio = node("divide", {{"type", "scalar"}});
    edge(width, ratio, "a");
    edge(reference, ratio, "b");
    const auto blue = node("saturate", {{"type", "scalar"}});
    edge(ratio, blue, "value");
    const auto color = node("combine", {{"type", "color4"}});
    edge(fraction, color, "x");
    edge(y, color, "y");
    edge(blue, color, "z");
    const auto image = node("texture2d", {{"key", "image"}});
    edge(uv, image, "uv");
    const auto sampled = node("multiply", {{"type", "color4"}});
    edge(image, sampled, "a");
    edge(color, sampled, "b");
    const auto tinted = node("multiply", {{"type", "color4"}});
    edge(sampled, tinted, "a");
    edge(tint_node, tinted, "b");
    edge(tinted, output, "color");
    source.validate();
    return source.document;
}
} // namespace forge::test
