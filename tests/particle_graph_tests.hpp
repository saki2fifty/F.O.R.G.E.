#pragma once
#include "particle_graph_example.hpp"
#include <algorithm>
#include <forge/material_graph.hpp>
#include <limits>
namespace forge::particle_graph_tests {
using Json = nlohmann::json;
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view code = {}) {
    try {
        fn();
    } catch (const MaterialGraphError& error) {
        check(code.empty() || error.diagnostic().code == code,
              "Particle graph returned the wrong diagnostic");
        check(bool(error.diagnostic().node), "Particle diagnostic lost its node");
        return;
    } catch (const std::exception&) {
        check(code.empty(), "Particle graph lost its structured diagnostic");
        return;
    }
    throw std::runtime_error("Invalid particle graph was accepted");
}
inline Json edge(const Json& from, const Json& to, std::string_view port) {
    return {{"id", GraphEdgeId::generate()},
            {"from", {{"node", from.at("id")}, {"port", "out"}}},
            {"to", {{"node", to.at("id")}, {"port", port}}}};
}
inline void run() {
    (void)test::particle_graph_example(AssetId::generate());
    const auto original = create_particle_graph();
    const auto identity = compile_particle_graph(original).source;
    for (const auto& format : Json::array({nullptr, 42, true, Json::object(), "unknown.graph"})) {
        auto particle = original;
        particle["format"] = format;
        rejects([&] { (void)compile_particle_graph(particle); });
        auto material = create_material_graph();
        material["format"] = format;
        rejects([&] { (void)compile_material_graph(material); });
    }
    check(identity.find("result.Position=input.Position;") != std::string::npos &&
              identity.find("result.Size=input.Size;") != std::string::npos &&
              identity.find("result.Color=input.Color;") != std::string::npos,
          "Disconnected particle output must preserve the evaluated particle");
    for (const auto& schema : material_graph_node_schemas())
        check(!schema.key.starts_with("particle_"),
              "Particle nodes leaked into the material palette");
    auto all = original;
    for (const auto& schema : particle_graph_node_schemas()) {
        if (schema.key == "particle_output" || schema.key == "function")
            continue;
        auto node = create_material_graph_node(schema.key, {0, 0});
        (void)material_graph_inputs(node);
        (void)material_graph_outputs(node);
        all["nodes"].push_back(node);
    }
    const auto all_source = compile_particle_graph(all).source;
    for (const auto* field :
         {"Position", "Velocity", "Origin", "Age", "Age01", "Size", "Color", "Random"})
        check(all_source.find(std::string("input.") + field) != std::string::npos,
              "Particle context input did not project to generated source");
    check(compile_particle_graph(all).source == all_source,
          "Particle graph code generation is nondeterministic");
    auto graph = original;
    auto age = create_material_graph_node("particle_age", {0, 0});
    auto sine = create_material_graph_node("sine", {200, 0});
    graph["nodes"].push_back(age);
    graph["nodes"].push_back(sine);
    graph["edges"] = {edge(age, sine, "value"), edge(sine, graph["nodes"][0], "size")};
    const auto before = graph;
    const auto wave = compile_particle_graph(graph).source;
    check(graph == before && wave.find("sin(forge_node_") != std::string::npos &&
              wave.find("result.Size=forge_node_") != std::string::npos,
          "Typed wave graph lost its connection or mutated authored identity");
    const auto extracted_before = graph;
    auto extracted = graph;
    (void)extract_material_graph_function(extracted, {sine.at("id").get<GraphNodeId>()}, "Wave");
    check(compile_particle_graph(extracted).source.find("sin(") != std::string::npos &&
              graph == extracted_before,
          "Particle function extraction failed to retain typed context inputs");
    auto clipboard = copy_material_graph_selection(graph, {age.at("id").get<GraphNodeId>()});
    paste_material_graph_selection(graph, clipboard, {20, 40});
    check(graph["nodes"].back()["id"] != age.at("id") &&
              graph["nodes"].back()["position"] == Json({20, 40}),
          "Particle graph paste reused node identity or lost canvas offset");
    graph = extracted_before;
    auto source = MaterialGraphSource::create_particle(AssetId::generate());
    source.document["graph"] = graph;
    const auto text = source.document.dump();
    check(MaterialGraphSource::parse(std::as_bytes(std::span(text))).document == source.document,
          "Particle graph source did not roundtrip");
    const auto duplicate = source.duplicate(AssetId::generate());
    check(duplicate.asset() != source.asset() &&
              duplicate.document["graph"]["nodes"][1]["id"] != age.at("id"),
          "Particle graph duplication lost its domain or reused node identity");
    (void)compile_particle_graph(duplicate.document.at("graph"));
    for (const auto* field : {"stages", "surface", "source_root", "defines", "permutations"}) {
        auto conflicting = source;
        conflicting.document[field] = Json::object();
        rejects([&] { conflicting.validate(); });
    }
    auto bad = graph;
    bad["edges"][1]["to"]["port"] = "position";
    rejects([&] { (void)compile_particle_graph(bad); }, "type_mismatch");
    bad = graph;
    bad["edges"].push_back(edge(sine, sine, "value"));
    rejects([&] { (void)compile_particle_graph(bad); }, "multiple_inputs");
    bad = graph;
    bad["edges"][0] = edge(sine, sine, "value");
    rejects([&] { (void)compile_particle_graph(bad); }, "cycle");
    bad = graph;
    bad["edges"][0]["from"]["port"] = "missing";
    rejects([&] { (void)compile_particle_graph(bad); }, "unknown_port");
    bad = graph;
    bad["nodes"].push_back(create_material_graph_node("particle_output", {0, 0}));
    rejects([&] { (void)compile_particle_graph(bad); }, "multiple_outputs");
    for (const auto* schema : {"parameter", "texture2d", "world_position", "ui_time", "surface",
                               "ui_output", "sprite_output"}) {
        bad = graph;
        bad["nodes"].push_back(create_material_graph_node(schema, {0, 0}));
        rejects([&] { (void)compile_particle_graph(bad); }, "particle_domain");
    }
    bad = create_material_graph();
    bad["nodes"].push_back(age);
    rejects([&] { (void)compile_material_graph(bad); }, "particle_domain");
    auto unknown = graph;
    unknown["nodes"][1]["type"] = "future.particle.node";
    unknown["nodes"][1]["opaque"] = {{"id", age.at("id")}, {"value", {1, 2, 3}}};
    const auto unknown_bytes = unknown.dump();
    validate_particle_graph_document(unknown);
    rejects([&] { (void)compile_particle_graph(unknown); }, "unavailable_schema");
    check(unknown.dump() == unknown_bytes && Json::parse(unknown_bytes) == unknown,
          "Unavailable particle node data was lost");
    bad = graph;
    bad["nodes"][1]["version"] = 2;
    check(!shader_graph_node_available(bad["nodes"][1]), "Unavailable node reported current ports");
    rejects([&] { (void)compile_particle_graph(bad); }, "unavailable_schema");
    bad = graph;
    bad["nodes"][1]["opaque"] = std::numeric_limits<double>::infinity();
    rejects([&] { validate_particle_graph_document(bad); });
    bad = graph;
    while (bad["nodes"].size() < 257)
        bad["nodes"].push_back(create_material_graph_node("constant", {0, 0}));
    rejects([&] { validate_particle_graph_document(bad); });
    auto input = create_material_graph_node("function_input", {0, 0});
    input["data"]["key"] = "age";
    auto returned = create_material_graph_node("function_output", {400, 0});
    Json body{{"format", "forge.material.graph"},
              {"version", 1},
              {"nodes", {input, sine, returned}},
              {"edges", {edge(input, sine, "value"), edge(sine, returned, "value")}}};
    Json function{{"id", GraphFunctionId::generate()}, {"label", "Wave"}, {"graph", body}};
    auto call = material_graph_function_call(function, {200, 0});
    graph = original;
    graph["functions"] = Json::array({function});
    graph["nodes"].push_back(age);
    graph["nodes"].push_back(call);
    graph["edges"] = {edge(age, call, "age"), edge(call, graph["nodes"][0], "size")};
    const auto function_before = graph;
    const auto function_source = compile_particle_graph(graph).source;
    check(function_source.find("sin(") != std::string::npos && graph == function_before &&
              function_source == compile_particle_graph(graph).source,
          "Particle context did not pass through the existing pure function owner");
    bad = graph;
    bad["nodes"][2]["version"] = 2;
    bad["nodes"][2]["data"] = {{"opaque_future", {1, 2, 3}}};
    const auto unavailable_call = bad;
    rejects([&] { (void)compile_particle_graph(bad); }, "unavailable_schema");
    check(bad == unavailable_call,
          "Unavailable function interface data was interpreted or changed");
    auto material_call = graph;
    material_call["format"] = "forge.material.graph";
    material_call["nodes"] = Json::array({call});
    material_call["nodes"][0]["version"] = 2;
    material_call["nodes"][0]["data"] = {{"opaque_future", true}};
    material_call["edges"] = Json::array();
    rejects([&] { (void)compile_material_graph(material_call); }, "unavailable_schema");
    bad = original;
    bad["functions"] = Json::array({function});
    bad["functions"][0]["graph"]["nodes"].push_back(age);
    rejects([&] { (void)compile_particle_graph(bad); }, "particle_domain");
}
} // namespace forge::particle_graph_tests
