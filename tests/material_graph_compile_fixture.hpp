#pragma once
#include <forge/material_graph.hpp>
namespace forge::graph_tests {
// Keep each schema reachable so actual compilers check its projected expression.
inline nlohmann::json all_node_graph() {
    using Json = nlohmann::json;
    auto graph = create_material_graph();
    graph["nodes"].erase(graph["nodes"].begin());
    const auto surface = graph.at("nodes").at(0);
    graph["edges"] = Json::array();
    auto edge = [&](const Json& from, const Json& to, const char* port) {
        graph["edges"].push_back({{"id", GraphEdgeId::generate()},
                                  {"from", {{"node", from.at("id")}, {"port", "out"}}},
                                  {"to", {{"node", to.at("id")}, {"port", port}}}});
    };
    auto name = [](MaterialParameterType type) {
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
        throw std::runtime_error("Unknown fixture type");
    };
    Json sum;
    for (const auto& schema : material_graph_node_schemas()) {
        if (schema.key == "surface" || schema.key.starts_with("function"))
            continue;
        auto n = create_material_graph_node(schema.key, {0, 0});
        if (schema.key == "parameter" || schema.key == "constant") {
            n["data"]["type"] = "color4";
            n["data"]["value"] = {0.1, 0.2, 0.3, 1.0};
        }
        if (schema.key == "divide")
            n["data"]["type"] = "vector4";
        if (schema.key == "lerp")
            n["data"]["type"] = "color3";
        graph["nodes"].push_back(n);
        auto converted = create_material_graph_node("convert", {0, 0});
        converted["data"]["from"] = name(material_graph_outputs(n).at(0).type);
        converted["data"]["type"] = "color4";
        graph["nodes"].push_back(converted);
        edge(n, converted, "value");
        if (sum.is_null())
            sum = converted;
        else {
            auto add = create_material_graph_node("add", {0, 0});
            add["data"]["type"] = "color4";
            graph["nodes"].push_back(add);
            edge(sum, add, "a");
            edge(converted, add, "b");
            sum = add;
        }
    }
    edge(sum, surface, "base_color");
    return graph;
}
} // namespace forge::graph_tests
