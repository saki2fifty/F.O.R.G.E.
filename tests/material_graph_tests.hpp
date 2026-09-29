#pragma once
#include "material_graph_compile_fixture.hpp"
#include "mesh_draw_shader.hpp"
#include <forge/material_graph.hpp>
#include <limits>
namespace forge::graph_tests {
using Json = nlohmann::json;
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, std::string code = {}) {
    try {
        f();
    } catch (const MaterialGraphError& e) {
        check(code.empty() || e.diagnostic().code == code, "Wrong graph diagnostic");
        check(bool(e.diagnostic().node), "Missing graph diagnostic node");
        return;
    } catch (const std::exception&) {
        check(code.empty(), "Expected source-located graph diagnostic");
        return;
    }
    throw std::runtime_error("Invalid graph accepted");
}
inline Json edge(const Json& from, std::string out, const Json& to, std::string in) {
    return {{"id", GraphEdgeId::generate()},
            {"from", {{"node", from.at("id")}, {"port", out}}},
            {"to", {{"node", to.at("id")}, {"port", in}}}};
}
inline void run() {
    (void)compile_material_graph(all_node_graph());
    auto graph = create_material_graph();
    validate_material_graph_document(graph);
    const auto source = MaterialGraphSource::create(AssetId::generate());
    const auto text = source.document.dump();
    check(MaterialGraphSource::parse(std::as_bytes(std::span(text))).document == source.document,
          "Graph shader round trip changed document");
    auto conflicting = source;
    conflicting.document["stages"] = Json::array();
    rejects([&] { conflicting.validate(); });
    const auto cooked = compile_material_graph(graph);
    check(cooked.surface.physically_based, "Graph lost lit surface contract");
    check(surface_definition(surface_definition_document(cooked.surface)) == cooked.surface,
          "Graph surface interface did not round trip");
    const auto pixel =
        material_graph_shader_wrapper(cooked.surface, "engine/forge.graph.hlsl", false);
    const auto depth =
        material_graph_shader_wrapper(cooked.surface, "engine/forge.graph.hlsl", true);
    check(pixel.find("ResolveLighting(s,lighting)") != std::string::npos &&
              pixel.find("ForgeSurfaceColor(ForgeSurfaceInput") != std::string::npos,
          "Graph bypassed shared PBR lighting");
    check(pixel.find("g_ForgeSurfaceState.x==1") != std::string::npos &&
              depth.find("g_ForgeSurfaceState.x==1") != std::string::npos,
          "Graph color/depth alpha contracts differ");
    check(cooked.surface.parameters.size() == 1 &&
              cooked.source.find("ForgeGraphSurface") != std::string::npos,
          "Default graph did not compile");
    check(cooked.source.find("g_SurfaceParameter_") != std::string::npos,
          "Graph parameter binding differs from surface header");
    check(cooked.surface.parameters.begin()->second.type == MaterialParameterType::LinearColor4,
          "Color became an untyped vector");
    auto renamed = graph;
    renamed["nodes"][0]["data"]["label"] = "Friendly new name";
    const auto relabeled = compile_material_graph(renamed);
    check(relabeled.surface.parameters == cooked.surface.parameters &&
              relabeled.source == cooked.source &&
              relabeled.surface.labels != cooked.surface.labels,
          "Label rename changed parameter identity or compiled semantics");
    auto unknown = graph;
    unknown["nodes"][0]["type"] = "extension.missing";
    unknown["nodes"][0]["opaque"] = {{"node_identity", graph["nodes"][0]["id"]},
                                     {"future", {1, 2, 3}}};
    validate_material_graph_document(unknown);
    const auto preserved = Json::parse(unknown.dump());
    check(preserved == unknown, "Unknown node data was lost");
    rejects([&] { compile_material_graph(unknown); }, "unavailable_schema");
    auto bad = graph;
    bad["nodes"][0]["data"] = {{"key", "Rate"}, {"type", "scalar"}, {"value", {1.0}}};
    rejects([&] { compile_material_graph(bad); }, "type_mismatch");
    bad = graph;
    bad["edges"].push_back(edge(bad["nodes"][0], "out", bad["nodes"][1], "base_color"));
    rejects([&] { compile_material_graph(bad); }, "multiple_inputs");
    auto op = create_material_graph_node("multiply", {40, 200});
    op["data"]["type"] = "color4";
    bad = graph;
    bad["nodes"].push_back(op);
    bad["edges"].push_back(edge(op, "out", op, "a"));
    rejects([&] { compile_material_graph(bad); }, "cycle");
    bad = graph;
    bad["edges"][0]["from"]["port"] = "wrong";
    rejects([&] { compile_material_graph(bad); }, "unknown_port");
    bad = graph;
    bad["nodes"][0]["data"]["value"][0] = std::numeric_limits<double>::max();
    rejects([&] { compile_material_graph(bad); });
    bad = graph;
    bad["nodes"][0]["position"][0] = std::numeric_limits<double>::infinity();
    rejects([&] { validate_material_graph_document(bad); });
    bad = graph;
    bad["nodes"].push_back(bad["nodes"][0]);
    rejects([&] { validate_material_graph_document(bad); });
    bad = graph;
    bad["nodes"][0]["version"] = -1;
    rejects([&] { validate_material_graph_document(bad); });
    check(compile_material_graph(graph).surface.uv_sets.empty(),
          "Untextured parameter graph required absent mesh UV channels");
    auto normal = create_material_graph_node("normal_map", {30, 200});
    auto normal_graph = graph;
    normal_graph["nodes"].push_back(normal);
    normal_graph["edges"].push_back(edge(normal, "out", normal_graph["nodes"][1], "normal"));
    check(compile_material_graph(normal_graph).surface.uv_sets == std::vector<unsigned>{0},
          "Implicit normal-map coordinates lost their required UV set");
    auto cube_texture = create_material_graph_node("texturecube", {30, 200});
    auto cube_graph = graph;
    cube_graph["nodes"].push_back(cube_texture);
    cube_graph["edges"] =
        Json::array({edge(cube_texture, "out", cube_graph["nodes"][1], "base_color")});
    check(compile_material_graph(cube_graph).surface.uv_sets.empty(),
          "Cube coordinates unnecessarily required planar mesh UV channels");
    auto texture = create_material_graph_node("texture2d", {40, 220});
    texture["data"]["uv_set"] = 2;
    auto uv = create_material_graph_node("uv", {10, 350});
    uv["data"]["uv_set"] = 3;
    bad = graph;
    bad["nodes"].push_back(texture);
    bad["nodes"].push_back(uv);
    bad["edges"] = Json::array(
        {edge(texture, "out", bad["nodes"][1], "base_color"), edge(uv, "out", texture, "uv")});
    const auto textured = compile_material_graph(bad);
    check(textured.surface.uv_sets == std::vector<unsigned>{2, 3}, "Logical UV sets changed");
    check(textured.source.find("input.UV[1]") != std::string::npos &&
              textured.source.find("input.UV[3]") == std::string::npos,
          "Logical UV was used as dense shader index");
    std::set<GraphNodeId> selection;
    for (const auto& n : unknown.at("nodes"))
        selection.insert(n.at("id").get<GraphNodeId>());
    const auto clipboard = copy_material_graph_selection(unknown, selection);
    auto pasted = unknown;
    paste_material_graph_selection(pasted, clipboard, {40, 60});
    check(pasted["nodes"].size() == 4 && pasted["edges"].size() == 2, "Graph paste lost structure");
    check(pasted["nodes"][2]["id"] != unknown["nodes"][0]["id"] &&
              pasted["edges"][1]["id"] != unknown["edges"][0]["id"],
          "Graph paste reused identity");
    check(pasted["nodes"][2]["opaque"] == unknown["nodes"][0]["opaque"],
          "Graph paste rewrote opaque plugin data");
    check(pasted["edges"][1]["from"]["node"] == pasted["nodes"][2]["id"],
          "Graph paste did not remap edge");
    auto malicious = clipboard;
    malicious["edges"][0]["to"]["node"] = GraphNodeId::generate();
    auto unchanged = graph;
    rejects([&] { paste_material_graph_selection(graph, malicious, {0, 0}); });
    check(graph == unchanged, "Failed paste partially mutated graph");
    auto function_graph = create_material_graph();
    auto inverse = create_material_graph_node("one_minus", {200, 60});
    inverse["data"]["type"] = "color4";
    function_graph["nodes"].push_back(inverse);
    function_graph["edges"] =
        Json::array({edge(function_graph["nodes"][0], "out", inverse, "value"),
                     edge(inverse, "out", function_graph["nodes"][1], "base_color")});
    const auto function_id = extract_material_graph_function(
        function_graph, {inverse.at("id").get<GraphNodeId>()}, "Invert Color");
    const auto function_compiled = compile_material_graph(function_graph);
    check(function_graph.at("functions").size() == 1 &&
              function_compiled.surface.parameters.size() == 1,
          "Function lost shared parameter ownership");
    check(compile_material_graph(function_graph).source == function_compiled.source,
          "Function expansion is nondeterministic");
    auto renamed_function = function_graph;
    renamed_function["functions"][0]["label"] = "New name";
    check(compile_material_graph(renamed_function).source == function_compiled.source &&
              renamed_function["functions"][0]["id"] == function_graph["functions"][0]["id"],
          "Function rename changed identity or math");
    auto duplicate_source = MaterialGraphSource::create(AssetId::generate());
    duplicate_source.document["graph"] = function_graph;
    duplicate_source.document["graph"]["nodes"][0]["opaque"] = {
        {"node", function_graph["nodes"][0]["id"]}};
    const auto duplicate = duplicate_source.duplicate(AssetId::generate());
    const auto& copied_graph = duplicate.document.at("graph");
    check(duplicate.asset() != duplicate_source.asset() &&
              copied_graph.at("nodes")[0].at("id") != function_graph.at("nodes")[0].at("id") &&
              copied_graph.at("functions")[0].at("id").get<GraphFunctionId>() != function_id,
          "Whole graph duplicate reused authored identities");
    check(copied_graph.at("nodes")[0].at("opaque") ==
              duplicate_source.document.at("graph").at("nodes")[0].at("opaque"),
          "Whole graph duplicate rewrote opaque references");
    check(surface_definition_document(compile_material_graph(copied_graph).surface) ==
              surface_definition_document(function_compiled.surface),
          "Whole graph duplicate changed the parameter binding contract");
    rejects([&] { duplicate_source.duplicate(duplicate_source.asset()); });
    auto function_bad = function_graph;
    function_bad["functions"][0]["graph"]["nodes"][0]["type"] = "missing.function.node";
    rejects([&] { compile_material_graph(function_bad); });
    auto call = material_graph_function_call(function_graph["functions"][0], {600, 60});
    function_graph["nodes"].push_back(call);
    (void)compile_material_graph(
        function_graph); // second disconnected call shares only pure calculations
    const auto calls =
        copy_material_graph_selection(function_graph, {call.at("id").get<GraphNodeId>()});
    auto into = create_material_graph();
    paste_material_graph_selection(into, calls, {20, 20});
    check(into.at("functions")[0].at("id").get<GraphFunctionId>() == function_id,
          "Paste lost function identity");
    (void)compile_material_graph(into);
    function_bad = function_graph;
    function_bad["functions"][0]["graph"]["nodes"].push_back(call);
    rejects([&] { compile_material_graph(function_bad); }, "invalid_function");
    auto arbitrary_uv = bad;
    arbitrary_uv["nodes"][2]["data"]["uv_set"] = 83;
    arbitrary_uv["nodes"][3]["data"]["uv_set"] = 81;
    const auto wide_uv = compile_material_graph(arbitrary_uv);
    check(wide_uv.surface.uv_sets == std::vector<unsigned>{81, 83},
          "Arbitrary logical UV set was restricted by physical varying count");
    arbitrary_uv["nodes"][3]["data"]["uv_set"] = -1;
    rejects([&] { compile_material_graph(arbitrary_uv); }, "invalid_configuration");
    auto nonfinite = graph;
    nonfinite["nodes"][0]["opaque"] = std::numeric_limits<double>::infinity();
    rejects([&] { validate_material_graph_document(nonfinite); });
    for (const auto& schema : material_graph_node_schemas()) {
        auto node = create_material_graph_node(schema.key, {0, 0});
        try {
            (void)material_graph_inputs(node);
            (void)material_graph_outputs(node);
        } catch (const std::exception& e) {
            throw std::runtime_error("Node projection " + schema.key + ": " + e.what());
        }
    }
}
} // namespace forge::graph_tests
