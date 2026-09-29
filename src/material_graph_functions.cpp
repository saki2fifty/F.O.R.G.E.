#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/material_graph.hpp>
#include <functional>
namespace forge {
namespace {
using Json = nlohmann::json;
void need(bool ok, std::string_view message) {
    if (!ok)
        throw std::runtime_error("Material function: " + std::string(message));
}
std::string type_name(MaterialParameterType t) {
    switch (t) {
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
    throw std::runtime_error("Unknown function value type");
}
Json signature(const Json& f) {
    Json inputs = Json::array();
    std::set<std::string> keys;
    unsigned outputs = 0;
    for (const auto& n : f.at("graph").at("nodes")) {
        const auto k = n.at("type").template get<std::string>();
        need(k != "surface" && k != "parameter" && !k.starts_with("texture"),
             "Pure functions receive material parameters/textures through typed inputs");
        if (k == "function_input") {
            const auto& d = n.at("data");
            auto key = d.at("key").template get<std::string>();
            need(!key.empty() && key.size() <= 128 && keys.insert(key).second,
                 "Invalid/duplicate function input identity");
            const auto ports = material_graph_outputs(n);
            inputs.push_back({{"key", key},
                              {"type", type_name(ports.at(0).type)},
                              {"label", d.value("label", key)}});
        }
        if (k == "function_output")
            ++outputs;
    }
    need(outputs == 1 && inputs.size() <= 64,
         "A function requires one output and at most 64 inputs");
    return inputs;
}
const Json& output(const Json& f) {
    for (const auto& n : f.at("graph").at("nodes"))
        if (n.at("type") == "function_output")
            return n;
    throw std::runtime_error("Function output is missing");
}
// This identifier exists only in compiler scratch data. It is never a new
// authored UUID or serialized identity. Digesting the call path makes generated
// source deterministic while keeping source locations distinct across instances.
template <class Id> Id temporary_id(std::string path) {
    const auto bytes = std::as_bytes(std::span(path.data(), path.size()));
    auto hex = asset_detail::content_digest(bytes).substr(0, 32);
    hex[12] = '4';
    hex[16] = '8';
    return Id::parse(hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
                     hex.substr(16, 4) + "-" + hex.substr(20));
}
template <class F> void erase(Json& a, F f) {
    a.erase(std::remove_if(a.begin(), a.end(), f), a.end());
}
} // namespace
Json material_graph_function_call(const Json& f, std::array<double, 2> position) {
    auto n = create_material_graph_node("function", position);
    n["data"] = {{"function", f.at("id")},
                 {"inputs", signature(f)},
                 {"type", output(f).at("data").at("type")},
                 {"label", f.at("label")}};
    return n;
}
Json expand_material_graph_functions(const Json& graph) {
    Json result = graph;
    result.erase("functions");
    // Origin records belong only to compiler scratch nodes, never authored payload.
    for (auto& n : result["nodes"])
        n.erase("_compile_origin");
    if (!graph.contains("functions")) {
        for (const auto& n : graph.at("nodes"))
            if (n.at("type") == "function")
                throw MaterialGraphError({"missing_function", "Function definition is missing", "",
                                          n.at("id").template get<GraphNodeId>()});
        return result;
    }
    std::map<GraphFunctionId, const Json*> definitions;
    for (const auto& f : graph.at("functions")) {
        (void)signature(f);
        definitions.emplace(f.at("id").template get<GraphFunctionId>(), &f);
    }
    std::function<Json(Json, std::vector<GraphFunctionId>, std::string)> expand;
    expand = [&](Json g, std::vector<GraphFunctionId> stack, std::string path) {
        for (;;) {
            const auto found =
                std::find_if(g["nodes"].begin(), g["nodes"].end(),
                             [](const auto& n) { return n.at("type") == "function"; });
            if (found == g["nodes"].end())
                break;
            const auto call = *found;
            const auto caller = call.at("id").template get<GraphNodeId>();
            try {
                const auto id = call.at("data").at("function").template get<GraphFunctionId>();
                need(definitions.contains(id), "Function definition is missing");
                need(stack.size() < 16 && std::find(stack.begin(), stack.end(), id) == stack.end(),
                     "Recursive function or expansion depth exceeds 16");
                const auto& f = *definitions.at(id);
                need(call.at("data").at("inputs") == signature(f) &&
                         call.at("data").at("type") == output(f).at("data").at("type"),
                     "Function signature changed; update call ports explicitly");
                auto next_stack = stack;
                next_stack.push_back(id);
                auto body = expand(f.at("graph"), next_stack, path + caller.str() + "/");
                const auto local_path = path + caller.str() + "/";
                std::map<GraphNodeId, GraphNodeId> remap;
                std::map<GraphNodeId, Json> inputs;
                GraphNodeId out;
                for (const auto& n : body.at("nodes")) {
                    const auto old = n.at("id").template get<GraphNodeId>();
                    if (n.at("type") == "function_output") {
                        out = old;
                        continue;
                    }
                    if (n.at("type") == "function_input") {
                        const auto key = n.at("data").at("key").template get<std::string>();
                        auto incoming =
                            std::find_if(g["edges"].begin(), g["edges"].end(), [&](const auto& e) {
                                return e.at("to").at("node") == call.at("id") &&
                                       e.at("to").at("port") == key;
                            });
                        if (incoming != g["edges"].end()) {
                            inputs[old] = incoming->at("from");
                            continue;
                        }
                        auto zero = create_material_graph_node("constant", {0, 0});
                        zero["id"] = temporary_id<GraphNodeId>(local_path + old.str());
                        zero["data"]["type"] = n.at("data").at("type");
                        zero["data"]["value"] = Json::array();
                        for (unsigned i = 0;
                             i < material_parameter_width(material_graph_outputs(n).at(0).type);
                             ++i)
                            zero["data"]["value"].push_back(0);
                        inputs[old] = {{"node", zero.at("id")}, {"port", "out"}};
                        g["nodes"].push_back(std::move(zero));
                        continue;
                    }
                    auto copy = n;
                    const auto derived = temporary_id<GraphNodeId>(local_path + old.str());
                    remap[old] = derived;
                    copy["id"] = derived;
                    copy["_compile_origin"] = {
                        {"node", old}, {"function", f.at("id")}, {"call", caller}};
                    g["nodes"].push_back(std::move(copy));
                }
                auto source = [&](const Json& ref) {
                    const auto id = ref.at("node").template get<GraphNodeId>();
                    if (inputs.contains(id))
                        return inputs.at(id);
                    auto copy = ref;
                    copy["node"] = remap.at(id);
                    return copy;
                };
                Json returned;
                for (auto edge : body.at("edges")) {
                    const auto target = edge.at("to").at("node").template get<GraphNodeId>();
                    if (target == out) {
                        need(edge.at("to").at("port") == "value" && returned.is_null(),
                             "Invalid function output connections");
                        returned = source(edge.at("from"));
                        continue;
                    }
                    need(remap.contains(target), "Function input cannot receive a connection");
                    edge["id"] = temporary_id<GraphEdgeId>(
                        local_path + edge.at("id").template get<GraphEdgeId>().str());
                    edge["from"] = source(edge.at("from"));
                    edge["to"]["node"] = remap.at(target);
                    g["edges"].push_back(std::move(edge));
                }
                need(!returned.is_null(), "Function output requires a connection");
                for (auto& e : g["edges"])
                    if (e.at("from").at("node") == call.at("id")) {
                        need(e.at("from").at("port") == "out", "Unknown function output port");
                        e["from"] = returned;
                    }
                erase(g["edges"],
                      [&](const auto& e) { return e.at("to").at("node") == call.at("id"); });
                erase(g["nodes"], [&](const auto& n) { return n.at("id") == call.at("id"); });
                need(g["nodes"].size() <= 1024 && g["edges"].size() <= 4096,
                     "Expanded function exceeds graph admission budget");
            } catch (const MaterialGraphError&) {
                throw;
            } catch (const std::exception& e) {
                throw MaterialGraphError({"invalid_function", e.what(), "", caller});
            }
        }
        return g;
    };
    // Unused definitions are validated as well, without creating unused bindings.
    for (const auto& [id, f] : definitions) {
        auto expanded = expand(f->at("graph"), {id}, id.str());
        validate_material_graph_document(expanded);
        GraphNodeId returned;
        for (auto& n : expanded["nodes"]) {
            if (n.at("type") == "function_input") {
                const auto width = material_parameter_width(material_graph_outputs(n).at(0).type);
                n["type"] = "constant";
                n["data"]["value"] = Json::array();
                for (unsigned i = 0; i < width; ++i)
                    n["data"]["value"].push_back(0);
            } else if (n.at("type") == "function_output") {
                returned = n.at("id").template get<GraphNodeId>();
                n["type"] = "convert";
                n["data"]["from"] = n.at("data").at("type");
                n["data"]["type"] = "color4";
            }
        }
        auto surface = create_material_graph_node("surface", {600, 0});
        expanded["edges"].push_back({{"id", GraphEdgeId::generate()},
                                     {"from", {{"node", returned}, {"port", "out"}}},
                                     {"to", {{"node", surface.at("id")}, {"port", "base_color"}}}});
        expanded["nodes"].push_back(surface);
        (void)compile_material_graph(expanded);
    }
    return expand(std::move(result), {}, "");
}
GraphFunctionId extract_material_graph_function(Json& graph, const std::set<GraphNodeId>& selected,
                                                std::string label) {
    validate_material_graph_document(graph);
    need(!selected.empty(), "Select connected math/value nodes to extract");
    Json next = graph, body{{"format", "forge.material.graph"},
                            {"version", 1},
                            {"nodes", Json::array()},
                            {"edges", Json::array()}};
    std::optional<Json> external_output;
    std::vector<Json> incoming;
    for (const auto& n : graph.at("nodes"))
        if (selected.contains(n.at("id").template get<GraphNodeId>())) {
            need(n.at("type") != "surface" && n.at("type") != "parameter" &&
                     !n.at("type").template get<std::string>().starts_with("texture") &&
                     n.at("type") != "function_input" && n.at("type") != "function_output",
                 "Extract pure math/value nodes; leave bindings and interface nodes outside");
            body["nodes"].push_back(n);
        }
    need(body["nodes"].size() == selected.size(), "Selection contains missing nodes");
    for (const auto& e : graph.at("edges")) {
        const bool from = selected.contains(e.at("from").at("node").template get<GraphNodeId>()),
                   to = selected.contains(e.at("to").at("node").template get<GraphNodeId>());
        if (from && to)
            body["edges"].push_back(e);
        if (!from && to)
            incoming.push_back(e);
        if (from && !to) {
            need(!external_output || *external_output == e.at("from"),
                 "A function returns one value; selection has multiple outgoing values");
            external_output = e.at("from");
        }
    }
    need(bool(external_output), "Select a subgraph whose output connects to an unselected node");
    const auto source =
        std::find_if(body["nodes"].begin(), body["nodes"].end(),
                     [&](const auto& n) { return n.at("id") == external_output->at("node"); });
    const auto ports = material_graph_outputs(*source);
    need(ports.size() == 1 && ports[0].key == external_output->at("port"),
         "Invalid function output");
    const auto source_position = source->at("position");
    auto out = create_material_graph_node("function_output", {500, 80});
    out["data"]["type"] = type_name(ports[0].type);
    body["edges"].push_back({{"id", GraphEdgeId::generate()},
                             {"from", *external_output},
                             {"to", {{"node", out.at("id")}, {"port", "value"}}}});
    body["nodes"].push_back(out);
    std::map<GraphEdgeId, std::string> keys;
    for (auto e : incoming) {
        const auto target =
            std::find_if(body["nodes"].begin(), body["nodes"].end(),
                         [&](const auto& n) { return n.at("id") == e.at("to").at("node"); });
        const auto ins = material_graph_inputs(*target);
        const auto p = std::find_if(ins.begin(), ins.end(),
                                    [&](const auto& p) { return p.key == e.at("to").at("port"); });
        need(p != ins.end(), "Unknown extraction input port");
        auto in = create_material_graph_node("function_input", {0, double(keys.size() * 100)});
        auto key = "in_" + e.at("id").template get<GraphEdgeId>().str();
        keys[e.at("id").template get<GraphEdgeId>()] = key;
        in["data"] = {{"key", key}, {"type", type_name(p->type)}, {"label", p->label}};
        e["id"] = GraphEdgeId::generate();
        e["from"] = {{"node", in.at("id")}, {"port", "out"}};
        body["edges"].push_back(e);
        body["nodes"].push_back(in);
    }
    const auto id = GraphFunctionId::generate();
    Json f{{"id", id}, {"label", std::move(label)}, {"graph", body}};
    auto call = material_graph_function_call(
        f, {source_position[0].template get<double>(), source_position[1].template get<double>()});
    // source iterator could be invalidated by appended input nodes; recover original output
    // position.
    for (const auto& n : graph.at("nodes"))
        if (n.at("id") == external_output->at("node"))
            call["position"] = n.at("position");
    for (auto& e : next["edges"]) {
        if (e.at("from") == *external_output)
            e["from"] = {{"node", call.at("id")}, {"port", "out"}};
        if (keys.contains(e.at("id").template get<GraphEdgeId>()))
            e["to"] = {{"node", call.at("id")},
                       {"port", keys.at(e.at("id").template get<GraphEdgeId>())}};
    }
    erase(next["edges"], [&](const auto& e) {
        return selected.contains(e.at("from").at("node").template get<GraphNodeId>()) ||
               selected.contains(e.at("to").at("node").template get<GraphNodeId>());
    });
    erase(next["nodes"],
          [&](const auto& n) { return selected.contains(n.at("id").template get<GraphNodeId>()); });
    next["nodes"].push_back(call);
    if (!next.contains("functions"))
        next["functions"] = Json::array();
    next["functions"].push_back(f);
    validate_material_graph_document(next);
    (void)expand_material_graph_functions(next);
    graph = std::move(next);
    return id;
}
} // namespace forge
