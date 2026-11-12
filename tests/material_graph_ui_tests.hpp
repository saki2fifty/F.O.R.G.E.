#pragma once
#include "ui_effect_fixture.hpp"
#include <forge/material_graph.hpp>
#include <forge/shader_asset.hpp>

namespace forge::graph_tests {
inline void ui_tests() {
    using Json = nlohmann::json;
    const auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto rejects = [&](const Json& graph, std::string_view code) {
        const auto original = graph;
        try {
            (void)compile_material_graph(graph);
        } catch (const MaterialGraphError& error) {
            check(error.diagnostic().code == code && bool(error.diagnostic().node),
                  "UI graph rejection lost its diagnostic code/location");
            check(graph == original, "UI domain rejection changed authored source");
            return;
        }
        throw std::runtime_error("Invalid UI graph accepted");
    };
    const auto source = MaterialGraphSource::create_ui(AssetId::generate());
    auto graph = source.document.at("graph");
    const auto original = graph;
    const auto compiled = compile_material_graph(graph);
    check(graph == original && compiled.surface.ui_effect && !compiled.surface.physically_based &&
              !compiled.surface.sprite_image &&
              compiled.surface.uv_sets == std::vector<unsigned>{0} &&
              compiled.surface.textures.empty() && compiled.surface.parameters.size() == 1,
          "Default UI graph lost its numeric UI domain or mutated source");
    check(compiled.source.find("ForgeGraphSurface(ForgeUiInput input)") != std::string::npos &&
              compiled.source.find("input.Color") == std::string::npos &&
              compiled.source.find("ForgeSurface.fxh") == std::string::npos,
          "UI graph emitted a geometry/lighting interface");
    const auto program = shader_program_source(source.document);
    check(program.surface->ui_effect && program.stages.size() == 1 &&
              program.stages.front().stage == ShaderStage::Pixel &&
              surface_definition_document(*program.surface).at("version") == 4,
          "UI graph Shader projection lost its single numeric Pixel interface");
    const auto native_graph = test::ui_effect_graph_document(AssetId::generate());
    const auto native = compile_material_graph(native_graph.at("graph"));
    check(native.surface.ui_effect && native.surface.parameters.contains("tint") &&
              native.surface.parameters.contains("speed") &&
              native.surface.textures.contains("image") &&
              native.source.find("input.Time") != std::string::npos &&
              native.source.find("input.Size") != std::string::npos,
          "Native UI graph fixture lost a connected context input or Material binding");
    const auto bytes = source.document.dump();
    check(MaterialGraphSource::parse(std::as_bytes(std::span(bytes))).document == source.document,
          "UI graph source roundtrip changed identities or declarations");
    const auto duplicate = source.duplicate(AssetId::generate());
    check(duplicate.asset() != source.asset() &&
              duplicate.document.at("graph").at("nodes").front().at("id") !=
                  graph.at("nodes").front().at("id") &&
              compile_material_graph(duplicate.document.at("graph")).surface.ui_effect,
          "UI graph duplication lost its domain or reused node identities");
    const auto edge = [](const Json& from, const Json& to, const char* port) {
        return Json{{"id", GraphEdgeId::generate()},
                    {"from", {{"node", from.at("id")}, {"port", "out"}}},
                    {"to", {{"node", to.at("id")}, {"port", port}}}};
    };
    auto image = create_material_graph_node("texture2d", {0, 200});
    auto uv = create_material_graph_node("uv", {0, 400});
    graph["nodes"].push_back(image);
    graph["nodes"].push_back(uv);
    graph["edges"] = Json::array({edge(uv, image, "uv"), edge(image, graph["nodes"][1], "color")});
    const auto textured = compile_material_graph(graph);
    check(textured.surface.textures.size() == 1 && textured.surface.ui_effect &&
              surface_definition_document(textured.surface).at("version") == 5 &&
              textured.source.find("input.UV") != std::string::npos &&
              textured.source.find("ForgeSample_") != std::string::npos &&
              textured.source.find("FORGE_GRAPH_UV_") == std::string::npos,
          "UI graph lost ordinary Texture2D binding or explicit UV0 lowering");
    for (const auto* schema : {"ui_time", "ui_size"}) {
        auto contextual = original;
        contextual["nodes"].push_back(create_material_graph_node(schema, {0, 300}));
        const auto result = compile_material_graph(contextual);
        check(result.source.find(schema == std::string_view("ui_time")
                                     ? "input.Time"
                                     : "input.Size") != std::string::npos,
              "UI contextual node did not lower to its native field");
        for (auto other : {create_material_graph(), create_sprite_material_graph()}) {
            other["nodes"].push_back(create_material_graph_node(schema, {0, 300}));
            rejects(other, "ui_domain");
        }
    }
    for (const auto* schema :
         {"world_normal", "world_position", "normal_map", "vertex_color", "texturecube",
          "texturecube_array", "texture2d_array", "texture3d"}) {
        auto invalid = original;
        invalid["nodes"].push_back(create_material_graph_node(schema, {0, 300}));
        rejects(invalid, "ui_domain");
    }
    for (const auto* schema : {"uv", "texture2d"}) {
        auto invalid = original;
        auto node = create_material_graph_node(schema, {0, 300});
        node["data"]["uv_set"] = 1;
        invalid["nodes"].push_back(node);
        rejects(invalid, "ui_domain");
    }
    auto mixed = original;
    mixed["nodes"].push_back(create_material_graph_node("surface", {0, 300}));
    rejects(mixed, "multiple_surfaces");
    auto function_graph = original;
    auto time = create_material_graph_node("ui_time", {0, 300});
    auto convert = create_material_graph_node("convert", {200, 300});
    convert["data"]["from"] = "scalar";
    convert["data"]["type"] = "color4";
    function_graph["nodes"].push_back(time);
    function_graph["nodes"].push_back(convert);
    function_graph["edges"] = Json::array(
        {edge(time, convert, "value"), edge(convert, function_graph["nodes"][1], "color")});
    const auto function = extract_material_graph_function(
        function_graph, {time.at("id").get<GraphNodeId>()}, "UI clock");
    check(bool(function) && compile_material_graph(function_graph).surface.ui_effect,
          "UI graph lost contextual function expansion");
    auto uncalled = original;
    uncalled["functions"] = function_graph.at("functions");
    check(compile_material_graph(uncalled).surface.parameters == compiled.surface.parameters,
          "Uncalled UI function changed the parent binding contract");
    for (auto invalid : {create_material_graph(), create_sprite_material_graph()}) {
        invalid["functions"] = function_graph.at("functions");
        rejects(invalid, "ui_domain");
    }
}
} // namespace forge::graph_tests
