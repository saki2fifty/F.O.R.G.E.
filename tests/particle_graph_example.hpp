#pragma once
#include <forge/material_graph.hpp>
namespace forge::test {
// Original editable example, shared by Gallery authoring, cooking and native canvas probes.
inline MaterialGraphSource particle_graph_example(AssetId id) {
    using Json = nlohmann::json;
    const auto connect = [](Json& graph, const Json& from, const Json& to, std::size_t input) {
        graph["edges"].push_back(
            {{"id", GraphEdgeId::generate()},
             {"from", {{"node", from.at("id")}, {"port", material_graph_outputs(from).at(0).key}}},
             {"to", {{"node", to.at("id")}, {"port", material_graph_inputs(to).at(input).key}}}});
    };
    const auto scalar = [](double value, std::array<double, 2> position) {
        auto node = create_material_graph_node("constant", position);
        node["data"] = {{"type", "scalar"}, {"value", {value}}};
        return node;
    };
    Json function_graph{{"format", "forge.material.graph"},
                        {"version", 1},
                        {"nodes", Json::array()},
                        {"edges", Json::array()}};
    auto age = create_material_graph_node("function_input", {0, 0});
    age["data"]["key"] = "age";
    age["data"]["label"] = "Age seconds";
    auto random = create_material_graph_node("function_input", {0, 240});
    random["data"]["key"] = "birth";
    random["data"]["label"] = "Birth random";
    const auto speed = scalar(4, {0, 120}), turns = scalar(6.283185307179586, {0, 360}),
               radius = scalar(.45, {530, 360});
    const auto elapsed = create_material_graph_node("multiply", {220, 0}),
               seeded = create_material_graph_node("multiply", {220, 240}),
               phase = create_material_graph_node("add", {430, 120}),
               cosine = create_material_graph_node("cosine", {640, 0}),
               sine = create_material_graph_node("sine", {640, 220}),
               x = create_material_graph_node("multiply", {850, 0}),
               z = create_material_graph_node("multiply", {850, 220});
    auto combine = create_material_graph_node("combine", {1060, 100});
    combine["data"]["type"] = "vector3";
    auto returned = create_material_graph_node("function_output", {1270, 100});
    returned["data"]["type"] = "vector3";
    function_graph["nodes"] = {age,   random, speed, turns, radius, elapsed, seeded,
                               phase, cosine, sine,  x,     z,      combine, returned};
    connect(function_graph, age, elapsed, 0);
    connect(function_graph, speed, elapsed, 1);
    connect(function_graph, random, seeded, 0);
    connect(function_graph, turns, seeded, 1);
    connect(function_graph, elapsed, phase, 0);
    connect(function_graph, seeded, phase, 1);
    connect(function_graph, phase, cosine, 0);
    connect(function_graph, phase, sine, 0);
    connect(function_graph, cosine, x, 0);
    connect(function_graph, sine, z, 0);
    connect(function_graph, radius, x, 1);
    connect(function_graph, radius, z, 1);
    connect(function_graph, x, combine, 0);
    connect(function_graph, z, combine, 2);
    connect(function_graph, combine, returned, 0);
    Json function{
        {"id", GraphFunctionId::generate()}, {"label", "Orbit offset"}, {"graph", function_graph}};
    auto source = MaterialGraphSource::create_particle(id);
    auto& graph = source.document["graph"];
    graph["functions"] = Json::array({function});
    const auto position = create_material_graph_node("particle_position", {0, 0}),
               seconds = create_material_graph_node("particle_age", {0, 180}),
               birth = create_material_graph_node("particle_random", {0, 330}),
               call = material_graph_function_call(function, {260, 200});
    auto offset = create_material_graph_node("add", {500, 0});
    offset["data"]["type"] = "vector3";
    const auto age01 = create_material_graph_node("particle_age01", {0, 500}),
               remaining = create_material_graph_node("one_minus", {260, 500}),
               size = create_material_graph_node("particle_size", {260, 650}),
               fade = create_material_graph_node("multiply", {500, 500}),
               color = create_material_graph_node("particle_color", {0, 850});
    auto accent = create_material_graph_node("constant", {0, 1020});
    accent["data"] = {{"type", "color3"}, {"value", {.05, .7, 1.}}};
    auto blend = create_material_graph_node("lerp", {500, 850});
    blend["data"]["type"] = "color3";
    auto output = graph["nodes"][0];
    output["position"] = {800, 360};
    graph["nodes"] = {position, seconds, birth, call,   offset, age01, remaining,
                      size,     fade,    color, accent, blend,  output};
    connect(graph, seconds, call, 0);
    connect(graph, birth, call, 1);
    connect(graph, position, offset, 0);
    connect(graph, call, offset, 1);
    connect(graph, offset, output, 0);
    connect(graph, age01, remaining, 0);
    connect(graph, size, fade, 0);
    connect(graph, remaining, fade, 1);
    connect(graph, fade, output, 1);
    connect(graph, color, blend, 0);
    connect(graph, accent, blend, 1);
    connect(graph, age01, blend, 2);
    connect(graph, blend, output, 2);
    source.validate();
    (void)compile_particle_graph(graph);
    return source;
}
} // namespace forge::test
