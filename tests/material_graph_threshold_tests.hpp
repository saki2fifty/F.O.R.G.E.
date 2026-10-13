#pragma once
#include <forge/material_graph.hpp>
namespace forge::graph_tests {
// Reusable authored fixture for actual shader compiler probes, not an evaluator.
inline nlohmann::json threshold_graph(std::string_view operation, std::string type,
                                      const std::vector<std::vector<double>>& values) {
    using Json = nlohmann::json;
    auto graph = create_material_graph();
    graph["nodes"].erase(graph["nodes"].begin());
    graph["edges"] = Json::array();
    const auto surface = graph["nodes"][0];
    auto node = create_material_graph_node(operation, {250, 0});
    node["data"]["type"] = type;
    node["data"]["opaque"] = {{"preserve", 42}};
    graph["nodes"].push_back(node);
    auto connect = [&](const Json& from, const Json& to, std::string port) {
        graph["edges"].push_back({{"id", GraphEdgeId::generate()},
                                  {"from", {{"node", from["id"]}, {"port", "out"}}},
                                  {"to", {{"node", to["id"]}, {"port", port}}}});
    };
    const auto inputs = material_graph_inputs(node);
    if (!values.empty() && values.size() != inputs.size())
        throw std::runtime_error("Wrong threshold fixture input count");
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto constant = create_material_graph_node("constant", {0, double(i) * 100});
        constant["data"]["type"] = type;
        constant["data"]["value"] = values[i];
        graph["nodes"].push_back(constant);
        connect(constant, node, inputs[i].key);
    }
    auto convert = create_material_graph_node("convert", {500, 0});
    convert["data"]["from"] = type;
    convert["data"]["type"] = "color4";
    graph["nodes"].push_back(convert);
    connect(node, convert, "value");
    connect(convert, surface, "base_color");
    return graph;
}
inline void threshold_tests() {
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const std::array<std::pair<std::string, MaterialParameterType>, 6> types{
        {{"scalar", MaterialParameterType::Scalar},
         {"vector2", MaterialParameterType::Vector2},
         {"vector3", MaterialParameterType::Vector3},
         {"vector4", MaterialParameterType::Vector4},
         {"color3", MaterialParameterType::LinearColor3},
         {"color4", MaterialParameterType::LinearColor4}}};
    for (const auto& [name, type] : types)
        for (const auto key : {"step", "smoothstep", "ceiling", "round"}) {
            auto node = create_material_graph_node(key, {0, 0});
            node["data"]["type"] = name;
            const auto inputs = material_graph_inputs(node);
            const auto outputs = material_graph_outputs(node);
            check(outputs.size() == 1 && outputs[0].type == type,
                  "Threshold/rounding output changed semantic type");
            for (const auto& input : inputs)
                check(input.type == type, "Threshold/rounding input has an implicit conversion");
            check(inputs.size() == (key == std::string("smoothstep") ? 3u
                                    : key == std::string("step")     ? 2u
                                                                     : 1u),
                  "Threshold input count differs");
            const auto lanes = material_parameter_width(type);
            std::vector<std::vector<double>> values(inputs.size(), std::vector<double>(lanes, .5));
            if (key == std::string("smoothstep")) {
                values[0].assign(lanes, 0);
                values[1].assign(lanes, 1);
            }
            auto graph = threshold_graph(key, name, values);
            const auto compiled = compile_material_graph(graph);
            check(compiled.surface.parameters.empty() && compiled.surface.textures.empty() &&
                      compiled.surface.uv_sets.empty() && compiled.surface.physically_based,
                  "New math nodes added hidden runtime binding/UV requirements");
            if (key == std::string("smoothstep"))
                check(compiled.source.find("?1.0:") != std::string::npos &&
                          compiled.source.find("?step(") != std::string::npos &&
                          compiled.source.find("smoothstep(0.0,1.0,") != std::string::npos,
                      "Smoothstep omitted its equal-edge nonzero-divisor contract");
            auto source = MaterialGraphSource::create(AssetId::generate());
            source.document["graph"] = graph;
            const auto bytes = source.document.dump();
            check(MaterialGraphSource::parse(std::as_bytes(std::span(bytes))).document ==
                      source.document,
                  "New math source roundtrip lost typed or opaque data");
            std::set<GraphNodeId> pure;
            for (const auto& n : graph["nodes"])
                if (n["type"] != "surface")
                    pure.insert(n["id"].get<GraphNodeId>());
            const auto clipboard = copy_material_graph_selection(graph, pure);
            paste_material_graph_selection(graph, clipboard, {30, 40});
            (void)compile_material_graph(graph);
            std::size_t copies = 0;
            for (const auto& n : graph["nodes"])
                if (n["type"] == key) {
                    ++copies;
                    check(n["data"]["type"] == name && n["data"]["opaque"]["preserve"] == 42,
                          "New math copy/paste lost type or opaque fields");
                }
            check(copies == 2, "New math copy/paste changed node counts");
            auto function_graph = source.document["graph"];
            const auto function = extract_material_graph_function(function_graph, pure, "Math");
            check(bool(function), "Pure threshold/rounding extraction failed");
            (void)compile_material_graph(function_graph);
            auto bad = source.document["graph"];
            bad["nodes"][1]["data"]["type"] = name == "color4" ? "vector4" : "color4";
            bool rejected = false;
            try {
                (void)compile_material_graph(bad);
            } catch (const MaterialGraphError& e) {
                rejected = e.diagnostic().code == "type_mismatch" && bool(e.diagnostic().node);
            }
            check(rejected, "New math incompatible link lacks source-located type rejection");
        }
    for (const auto& [name, type] : types)
        for (const auto key : {"step", "smoothstep", "ceiling", "round"})
            (void)compile_material_graph(threshold_graph(key, name, {}));
}
} // namespace forge::graph_tests
