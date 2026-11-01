#pragma once
#include <forge/material_graph.hpp>

namespace forge::graph_tests {
inline void sprite_tests() {
    using Json = nlohmann::json;
    const auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto rejects = [&](auto run, std::string_view code = {}) {
        try {
            run();
        } catch (const MaterialGraphError& error) {
            check(code.empty() || error.diagnostic().code == code, "Wrong Sprite graph diagnostic");
            check(bool(error.diagnostic().node), "Missing Sprite graph diagnostic location");
            return;
        } catch (const std::exception&) {
            check(code.empty(), "Expected located Sprite graph diagnostic");
            return;
        }
        throw std::runtime_error("Invalid Sprite graph accepted");
    };
    const auto source = MaterialGraphSource::create_sprite(AssetId::generate());
    const auto graph = source.document.at("graph");
    const auto compiled = compile_material_graph(graph);
    const auto image = graph.at("nodes").at(0).at("data").at("key").get<std::string>();
    check(compiled.surface.sprite_image == image && !compiled.surface.physically_based &&
              compiled.surface.uv_sets == std::vector<unsigned>{0} &&
              compiled.surface.parameters.size() == 1 && compiled.surface.textures.size() == 1,
          "Default Sprite graph lost its domain, image or complete declarations");
    check(compiled.source.find("ForgeGraphSurface(ForgeSpriteInput input)") != std::string::npos &&
              compiled.source.find("ForgeSample_" + image + "(input)") != std::string::npos &&
              compiled.source.find("*input.Color") != std::string::npos &&
              compiled.source.find("input.Normal") == std::string::npos,
          "Sprite graph lost sampling/Tint policy or acquired a Mesh normal frame");
    const auto defaults = surface_material_defaults(compiled.surface);
    check(defaults.alpha == MaterialAlpha::Blend && !defaults.depth_write && defaults.double_sided,
          "Sprite material defaults reject a normal transparent/trimmed image");
    const auto text = source.document.dump();
    check(MaterialGraphSource::parse(std::as_bytes(std::span(text))).document == source.document,
          "Sprite graph source roundtrip changed data");
    check(compile_material_graph(source.duplicate(AssetId::generate()).document.at("graph"))
                  .surface.sprite_image == image,
          "Logical Shader duplication changed stable Material binding identity");
    auto no_tint = graph;
    no_tint["nodes"][3]["data"]["tint"] = false;
    check(compile_material_graph(no_tint).source.find("*input.Color") == std::string::npos,
          "Sprite Output ignored explicit Tint ownership");
    auto warped = graph;
    const auto uv = create_material_graph_node("uv", {0, -180});
    warped["nodes"].push_back(uv);
    warped["edges"].push_back(
        {{"id", GraphEdgeId::generate()},
         {"from", {{"node", uv.at("id")}, {"port", "out"}}},
         {"to", {{"node", graph.at("nodes").at(0).at("id")}, {"port", "uv"}}}});
    const auto warped_source = compile_material_graph(warped).source;
    check(warped_source.find("ForgeUV0(input)") != std::string::npos &&
              warped_source.find("ForgeSample_" + image + "(input,") != std::string::npos &&
              warped_source.find("input.UV[") == std::string::npos,
          "Explicit primary-image UV lost Sprite frame/fill context");
    auto invalid = graph;
    invalid["nodes"][3]["data"]["image"] = "absent";
    rejects([&] { compile_material_graph(invalid); }, "invalid_sprite_surface");
    invalid = graph;
    invalid["nodes"][3]["data"]["tint"] = "false";
    rejects([&] { compile_material_graph(invalid); }, "invalid_configuration");
    invalid = graph;
    invalid["nodes"][3]["data"].erase("image");
    rejects([&] { compile_material_graph(invalid); }, "invalid_configuration");
    invalid = graph;
    invalid["nodes"].push_back(create_material_graph_node("surface", {0, 0}));
    rejects([&] { compile_material_graph(invalid); }, "multiple_surfaces");
    for (const auto* schema : {"world_normal", "normal_map", "uv", "texture2d_array"}) {
        invalid = graph;
        auto hidden = create_material_graph_node(schema, {0, 0});
        if (std::string_view(schema) == "uv" || std::string_view(schema) == "texture2d_array")
            hidden["data"]["uv_set"] = 2;
        invalid["nodes"].push_back(hidden);
        rejects([&] { compile_material_graph(invalid); }, "sprite_domain");
    }
    invalid = graph;
    invalid["nodes"].push_back(create_material_graph_node("texturecube", {0, 0}));
    rejects([&] { compile_material_graph(invalid); }, "missing_coordinate");
    invalid = graph;
    auto unavailable = create_material_graph_node("uv", {0, 0});
    unavailable["version"] = 999;
    unavailable["data"] = {{"uv_set", {{"opaque", "future format"}}}};
    invalid["nodes"].push_back(unavailable);
    const auto preserved = invalid;
    rejects([&] { compile_material_graph(invalid); }, "unavailable_schema");
    check(invalid == preserved, "Sprite domain checks rewrote opaque future-version data");
    auto function_graph = graph;
    const auto function_id = extract_material_graph_function(
        function_graph, {graph.at("nodes").at(2).at("id").get<GraphNodeId>()}, "Sprite color");
    check(bool(function_id) && compile_material_graph(function_graph).surface == compiled.surface,
          "Pure function extraction changed the Sprite binding contract");
    invalid = function_graph;
    invalid["functions"][0]["graph"]["nodes"].push_back(
        create_material_graph_node("world_normal", {0, 0}));
    // Even an unused function definition must retain this domain restriction.
    for (auto& node : invalid["nodes"])
        if (node.at("type") == "function") {
            node["type"] = "constant";
            node["data"] = {{"type", "color4"}, {"value", {1, 1, 1, 1}}};
        }
    invalid["edges"] = Json::array();
    rejects([&] { compile_material_graph(invalid); }, "sprite_domain");
    std::set<GraphNodeId> selection;
    for (const auto& node : graph.at("nodes"))
        selection.insert(node.at("id").get<GraphNodeId>());
    auto clipboard = copy_material_graph_selection(graph, selection);
    auto pasted = graph;
    pasted["nodes"] = Json::array();
    pasted["edges"] = Json::array();
    paste_material_graph_selection(pasted, clipboard, {0, 0});
    const auto copied = compile_material_graph(pasted);
    const auto copied_key = pasted.at("nodes").at(0).at("data").at("key").get<std::string>();
    check(copied.surface.sprite_image == copied_key && copied_key != image &&
              pasted.at("nodes").at(3).at("data").at("image") == copied_key,
          "Clipboard copy did not remap the primary-image reference with its declaration");
    clipboard["nodes"][3]["opaque"] = {{"image", image}};
    pasted["nodes"] = Json::array();
    pasted["edges"] = Json::array();
    paste_material_graph_selection(pasted, clipboard, {0, 0});
    check(pasted.at("nodes").at(3).at("opaque").at("image") == image,
          "Clipboard remapped opaque extension data");
    for (const auto index : {0u, 1u, 3u}) {
        auto future_clipboard = clipboard;
        auto& future = future_clipboard["nodes"][index];
        future["version"] = 999;
        future["data"] = {{"opaque", {{"image", image}, {"key", {1, 2, 3}}}}};
        const auto future_data = future.at("data");
        pasted["nodes"] = Json::array();
        pasted["edges"] = Json::array();
        paste_material_graph_selection(pasted, future_clipboard, {0, 0});
        check(pasted.at("nodes").at(index).at("data") == future_data,
              "Clipboard interpreted or rewrote opaque future-version binding/output data");
    }
}
} // namespace forge::graph_tests
