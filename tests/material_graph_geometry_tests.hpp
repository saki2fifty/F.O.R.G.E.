#pragma once
#include <forge/material_graph.hpp>
namespace forge::graph_tests {
inline const char* geometry_type_name(MaterialParameterType type) {
    switch (type) {
    case MaterialParameterType::Scalar:
        return "scalar";
    case MaterialParameterType::Vector2:
        return "vector2";
    case MaterialParameterType::Vector3:
        return "vector3";
    case MaterialParameterType::Vector4:
        return "vector4";
    case MaterialParameterType::LinearColor3:
        return "color3";
    case MaterialParameterType::LinearColor4:
        return "color4";
    }
    throw std::runtime_error("Unknown geometry fixture type");
}
// An authored reachable graph, also consumed by actual shader compiler probes.
inline nlohmann::json geometry_graph(std::string_view operation, std::string input_type,
                                     const std::vector<std::vector<double>>& values) {
    using Json = nlohmann::json;
    auto graph = create_material_graph();
    graph["nodes"].erase(graph["nodes"].begin());
    graph["edges"] = Json::array();
    const auto surface = graph["nodes"][0];
    auto node = create_material_graph_node(operation, {250, 0});
    node["data"]["type"] = input_type;
    node["data"]["opaque"] = {{"preserve", 75}};
    graph["nodes"].push_back(node);
    auto connect = [&](const Json& from, const Json& to, const std::string& port) {
        graph["edges"].push_back({{"id", GraphEdgeId::generate()},
                                  {"from", {{"node", from["id"]}, {"port", "out"}}},
                                  {"to", {{"node", to["id"]}, {"port", port}}}});
    };
    const auto inputs = material_graph_inputs(node);
    if (!values.empty() && values.size() != inputs.size())
        throw std::runtime_error("Wrong geometry fixture input count");
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].size() != material_parameter_width(inputs[i].type))
            throw std::runtime_error("Wrong geometry fixture input width");
        auto constant = create_material_graph_node("constant", {0, double(i) * 100});
        constant["data"]["type"] = geometry_type_name(inputs[i].type);
        constant["data"]["value"] = values[i];
        graph["nodes"].push_back(constant);
        connect(constant, node, inputs[i].key);
    }
    auto convert = create_material_graph_node("convert", {500, 0});
    convert["data"]["from"] = geometry_type_name(material_graph_outputs(node)[0].type);
    convert["data"]["type"] = "color4";
    graph["nodes"].push_back(convert);
    connect(node, convert, "value");
    connect(convert, surface, "base_color");
    return graph;
}
inline void geometry_tests() {
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const std::array<MaterialParameterType, 6> types{
        MaterialParameterType::Scalar,       MaterialParameterType::Vector2,
        MaterialParameterType::Vector3,      MaterialParameterType::Vector4,
        MaterialParameterType::LinearColor3, MaterialParameterType::LinearColor4};
    for (const auto op : {"length", "distance", "reflect3", "refract3"}) {
        const bool metric = std::string_view(op) == "length" || std::string_view(op) == "distance";
        for (const auto type : types) {
            if (!metric && type != MaterialParameterType::Vector3)
                continue;
            auto node = create_material_graph_node(op, {0, 0});
            const auto name = geometry_type_name(type);
            node["data"]["type"] = name;
            const auto inputs = material_graph_inputs(node), outputs = material_graph_outputs(node);
            check(outputs.size() == 1 && outputs[0].key == "out" &&
                      outputs[0].type ==
                          (metric ? MaterialParameterType::Scalar : MaterialParameterType::Vector3),
                  "Geometry output type/key differs");
            check(inputs.size() == (std::string_view(op) == "length"     ? 1u
                                    : std::string_view(op) == "refract3" ? 3u
                                                                         : 2u),
                  "Geometry input count differs");
            std::vector<std::vector<double>> values;
            for (const auto& input : inputs) {
                check(input.type == (input.key == "ratio" ? MaterialParameterType::Scalar : type),
                      "Geometry input lost semantic type");
                values.emplace_back(material_parameter_width(input.type), .5);
            }
            auto graph = geometry_graph(op, name, values);
            const auto compiled = compile_material_graph(graph);
            const auto defaults = compile_material_graph(geometry_graph(op, name, {}));
            check(compiled.surface.parameters.empty() && compiled.surface.textures.empty() &&
                      compiled.surface.uv_sets.empty() && compiled.surface.physically_based,
                  "Geometry introduced hidden bindings/UV requirements");
            check(defaults.source.find(metric ? std::string(op) + "("
                                              : std::string(op == std::string_view("reflect3")
                                                                ? "reflect("
                                                                : "refract(")) != std::string::npos,
                  "Reachable geometry defaults omitted intrinsic");
            auto source = MaterialGraphSource::create(AssetId::generate());
            source.document["graph"] = graph;
            const auto bytes = source.document.dump();
            check(MaterialGraphSource::parse(std::as_bytes(std::span(bytes))).document ==
                      source.document,
                  "Geometry source roundtrip lost fields");
            std::set<GraphNodeId> pure;
            for (const auto& item : graph.at("nodes"))
                if (item.at("type") != "surface")
                    pure.insert(item.at("id").get<GraphNodeId>());
            const auto clipboard = copy_material_graph_selection(graph, pure);
            paste_material_graph_selection(graph, clipboard, {30, 40});
            (void)compile_material_graph(graph);
            unsigned copies = 0;
            for (const auto& item : graph.at("nodes"))
                if (item.at("type") == op) {
                    ++copies;
                    check(item.at("data").at("opaque").at("preserve") == 75,
                          "Geometry copy lost opaque fields");
                }
            check(copies == 2, "Geometry copy count differs");
            auto function_graph = source.document.at("graph");
            check(bool(extract_material_graph_function(function_graph, pure, "Geometry")),
                  "Geometry function extraction failed");
            (void)compile_material_graph(function_graph);
            auto bad = source.document.at("graph");
            bad["nodes"][2]["data"]["type"] =
                type == MaterialParameterType::LinearColor4 ? "vector4" : "color4";
            bad["nodes"][2]["data"]["value"] = {0.5, 0.5, 0.5, 0.5};
            bool rejected = false;
            try {
                (void)compile_material_graph(bad);
            } catch (const MaterialGraphError& e) {
                rejected = e.diagnostic().code == "type_mismatch" && bool(e.diagnostic().node);
            }
            check(rejected, "Geometry accepted incompatible semantic input");
        }
    }
    for (const auto op : {"dot3", "cross3", "normalize3"}) {
        const auto node = create_material_graph_node(op, {0, 0});
        for (const auto& input : material_graph_inputs(node))
            check(input.type == MaterialParameterType::Vector3, "Legacy 3D input changed");
        check(material_graph_outputs(node)[0].type == (std::string_view(op) == "dot3"
                                                           ? MaterialParameterType::Scalar
                                                           : MaterialParameterType::Vector3),
              "Legacy 3D output changed");
    }
}
} // namespace forge::graph_tests
