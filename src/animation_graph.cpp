#include "animation_graph_layout.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_graph.hpp>
#include <functional>
#include <map>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
double number(const Json& value, double low, double high, const char* why) {
    require(value.is_number(), why);
    const auto result = value.get<double>();
    require(std::isfinite(result) && result >= low && result <= high, why);
    return result;
}
std::uint32_t slot(const Json& value) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() > 0),
            "Animation Graph input requires an integer slot within 1–4");
    const auto result = value.get<std::uint64_t>();
    require(result >= 1 && result <= animation_graph_input_limit,
            "Animation Graph input requires an integer slot within 1–4");
    return std::uint32_t(result);
}
std::string label(const Json& value) {
    require(value.is_string(), "Animation Graph names must be text");
    auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= 64 &&
                std::none_of(result.begin(), result.end(),
                             [](unsigned char c) { return c < 32 || c == 127; }),
            "Animation Graph names need 1–64 bytes without control characters");
    return result;
}
AssetId optional_id(const Json& value) {
    if (value.is_null())
        return {};
    auto result = value.get<AssetId>();
    require(bool(result), "Animation Graph reference identity is empty");
    return result;
}
AnimationGraph admit(const Json& d, bool complete) {
    require(d.is_object() && d.at("format") == "forge.animation-graph" &&
                d.at("version").is_number_integer() && d.at("version") == 1,
            "Unsupported Animation Graph format/version");
    AnimationGraph graph;
    graph.asset = d.at("asset_id").get<AssetId>();
    require(bool(graph.asset), "Animation Graph asset identity is empty");
    graph.skeleton = optional_id(d.at("skeleton"));
    graph.cycle_seconds = number(d.at("cycle_seconds"), .0001, 3600,
                                 "Animation Graph cycle must be finite within 0.0001–3600 seconds");
    const auto& inputs = d.at("inputs");
    require(inputs.is_array() && inputs.size() <= animation_graph_input_limit,
            "Animation Graph supports at most 4 inputs");
    std::set<std::uint32_t> slots;
    std::set<std::string> names;
    for (const auto& row : inputs) {
        AnimationGraphInput input{
            slot(row.at("slot")), label(row.at("name")),
            number(row.at("initial"), -1000000, 1000000,
                   "Animation Graph initial input must be finite within ±1000000")};
        require(slots.insert(input.slot).second && names.insert(input.name).second,
                "Animation Graph input slots and names must be unique");
        graph.inputs.push_back(std::move(input));
    }
    std::sort(graph.inputs.begin(), graph.inputs.end(),
              [](const auto& a, const auto& b) { return a.slot < b.slot; });
    const auto& rows = d.at("nodes");
    require(rows.is_array() && rows.size() <= animation_graph_node_limit,
            "Animation Graph supports at most 32 nodes");
    std::map<AssetId, std::size_t> indices;
    std::vector<AnimationGraphNode> nodes;
    std::vector<std::array<AssetId, 2>> links;
    std::size_t leaves = 0;
    for (const auto& row : rows) {
        AnimationGraphNode node;
        node.id = row.at("id").get<AssetId>();
        require(bool(node.id) && indices.emplace(node.id, nodes.size()).second,
                "Animation Graph node identity is empty or duplicated");
        node.name = label(row.at("name"));
        const auto& kind = row.at("kind");
        std::array<AssetId, 2> dependencies{};
        if (kind == "clip") {
            node.clip.id = optional_id(row.at("clip"));
            require(++leaves <= animation_graph_clip_limit,
                    "Animation Graph supports at most 8 clip nodes");
            require(!complete || bool(node.clip.id),
                    "Choose a clip for every Animation Graph leaf");
        } else {
            require(kind == "blend" || kind == "additive", "Unknown Animation Graph node kind");
            node.kind =
                kind == "blend" ? AnimationGraphNodeKind::Blend : AnimationGraphNodeKind::Additive;
            dependencies = {optional_id(row.at("first")), optional_id(row.at("second"))};
            node.mask.id = optional_id(row.at("mask"));
            const auto& weight = row.at("weight");
            if (weight.is_number())
                node.weight.constant =
                    number(weight, 0, 1, "Animation Graph weight must be finite within 0–1");
            else {
                require(weight.is_object(),
                        "Animation Graph weight requires a number or input binding");
                node.weight.input = slot(weight.at("input"));
                require(slots.contains(node.weight.input),
                        "Animation Graph weight references an undeclared input");
                node.weight.scale =
                    number(weight.at("scale"), -1000000, 1000000,
                           "Animation Graph input scale must be finite within ±1000000");
                node.weight.bias =
                    number(weight.at("bias"), -1000000, 1000000,
                           "Animation Graph input bias must be finite within ±1000000");
            }
            require(!complete || (bool(dependencies[0]) && bool(dependencies[1])),
                    "Connect both inputs of every Animation Graph blend/additive node");
        }
        nodes.push_back(std::move(node));
        links.push_back(dependencies);
    }
    asset_detail::validate_animation_graph_layout(d);
    const auto output = optional_id(d.at("output"));
    require(!complete || (bool(graph.skeleton) && bool(output) && !nodes.empty()),
            "Animation Graph needs a skeleton, nodes and an output before publication");
    require(!output || indices.contains(output),
            "Animation Graph output references a missing node");
    for (const auto& dependencies : links)
        for (const auto id : dependencies)
            require(!id || indices.contains(id), "Animation Graph link references a missing node");
    std::vector<unsigned> marks(nodes.size());
    std::vector<std::uint32_t> positions(nodes.size(), animation_graph_no_node);
    // Bounded by32 nodes; dependency-first traversal also evaluates shared paths once.
    const std::function<void(std::size_t)> visit = [&](std::size_t at) {
        require(marks[at] != 1, "Animation Graph contains a cycle");
        if (marks[at] == 2)
            return;
        marks[at] = 1;
        auto node = nodes[at];
        for (std::size_t port = 0; port < 2; ++port)
            if (links[at][port]) {
                const auto source = indices.at(links[at][port]);
                visit(source);
                (port == 0 ? node.first : node.second) = positions[source];
            }
        positions[at] = std::uint32_t(graph.nodes.size());
        graph.nodes.push_back(std::move(node));
        marks[at] = 2;
    };
    if (output) {
        visit(indices.at(output));
        graph.output = positions[indices.at(output)];
    }
    require(!complete || graph.nodes.size() == nodes.size(),
            "Every Animation Graph node must contribute to its output before publication");
    for (const auto& [id, at] : indices) {
        (void)id;
        visit(at); // Drafts still reject cycles in disconnected subgraphs.
    }
    return graph;
}
Json canonical(const AnimationGraph& graph) {
    Json d{{"format", "forge.animation-graph"},
           {"version", 1},
           {"asset_id", graph.asset},
           {"skeleton", graph.skeleton},
           {"cycle_seconds", graph.cycle_seconds},
           {"inputs", Json::array()},
           {"nodes", Json::array()},
           {"output", graph.nodes.at(graph.output).id}};
    for (const auto& input : graph.inputs)
        d["inputs"].push_back(
            {{"slot", input.slot}, {"name", input.name}, {"initial", input.initial}});
    for (const auto& node : graph.nodes) {
        Json row{{"id", node.id}, {"name", node.name}};
        if (node.kind == AnimationGraphNodeKind::Clip) {
            row["kind"] = "clip";
            row["clip"] = node.clip.id;
        } else {
            row["kind"] = node.kind == AnimationGraphNodeKind::Blend ? "blend" : "additive";
            row["first"] = graph.nodes.at(node.first).id;
            row["second"] = graph.nodes.at(node.second).id;
            row["mask"] = node.mask.id ? Json(node.mask.id) : Json(nullptr);
            row["weight"] = node.weight.input ? Json{{"input", node.weight.input},
                                                     {"scale", node.weight.scale},
                                                     {"bias", node.weight.bias}}
                                              : Json(node.weight.constant);
        }
        d["nodes"].push_back(std::move(row));
    }
    return d;
}
} // namespace
double
AnimationGraphWeight::evaluate(std::span<const double, animation_graph_input_limit> values) const {
    require(input <= animation_graph_input_limit && std::isfinite(constant) && constant >= 0 &&
                constant <= 1 && std::isfinite(scale) && std::abs(scale) <= 1000000 &&
                std::isfinite(bias) && std::abs(bias) <= 1000000,
            "Animation Graph weight was not admitted");
    for (const auto value : values)
        require(std::isfinite(value) && std::abs(value) <= 1000000,
                "Animation Graph input must be finite within ±1000000");
    return input ? std::clamp(values[input - 1] * scale + bias, 0., 1.) : constant;
}
std::array<double, animation_graph_input_limit> AnimationGraph::initial_inputs() const {
    std::array<double, animation_graph_input_limit> result{};
    for (const auto& input : inputs) {
        require(input.slot >= 1 && input.slot <= animation_graph_input_limit,
                "Animation Graph input slot was not admitted");
        result[input.slot - 1] = input.initial;
    }
    return result;
}
std::size_t AnimationGraph::resident_bytes() const {
    auto result = sizeof(*this) + inputs.capacity() * sizeof(AnimationGraphInput) +
                  nodes.capacity() * sizeof(AnimationGraphNode);
    for (const auto& input : inputs)
        result += input.name.capacity() + 1;
    for (const auto& node : nodes)
        result += node.name.capacity() + 1;
    return result;
}
AnimationGraphSource AnimationGraphSource::create(AssetId id) {
    require(bool(id), "Animation Graph needs an asset identity");
    return {{{"format", "forge.animation-graph"},
             {"version", 1},
             {"asset_id", id},
             {"skeleton", nullptr},
             {"cycle_seconds", 1},
             {"inputs", Json::array()},
             {"nodes", Json::array()},
             {"output", nullptr}}};
}
AnimationGraphSource AnimationGraphSource::parse(std::span<const std::byte> bytes) {
    AnimationGraphSource source{
        asset_detail::parse_bounded_json(bytes, animation_graph_byte_limit, 12000, 12)};
    source.validate();
    return source;
}
AssetId AnimationGraphSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void AnimationGraphSource::validate() const { (void)admit(document, false); }
AnimationGraph AnimationGraphSource::compiled() const { return admit(document, true); }
std::vector<std::byte> AnimationGraphSource::cook() const {
    const auto d = canonical(compiled());
    const auto text = d.dump();
    (void)parse(std::as_bytes(std::span(text)));
    const auto cooked =
        Json{{"format", "forge.animation-graph"}, {"version", 1}, {"document", d}}.dump();
    require(cooked.size() <= animation_graph_cooked_byte_limit,
            "Cooked Animation Graph exceeds byte limit");
    const auto bytes = std::as_bytes(std::span(cooked));
    return {bytes.begin(), bytes.end()};
}
AnimationGraphSource AnimationGraphSource::parse_cooked(std::span<const std::byte> bytes) {
    const auto wrapper =
        asset_detail::parse_bounded_json(bytes, animation_graph_cooked_byte_limit, 12016, 13);
    require(wrapper.is_object() && wrapper.size() == 3 &&
                wrapper.at("format") == "forge.animation-graph" &&
                wrapper.at("version").is_number_integer() && wrapper.at("version") == 1,
            "Unsupported cooked Animation Graph format/version");
    const auto text = wrapper.at("document").dump();
    auto source = parse(std::as_bytes(std::span(text)));
    require(source.document == canonical(source.compiled()),
            "Cooked Animation Graph is not a canonical complete graph");
    return source;
}
AnimationGraph decode_animation_graph(std::span<const std::byte> bytes, AssetId expected) {
    auto source = AnimationGraphSource::parse_cooked(bytes);
    require(bool(expected) && source.asset() == expected,
            "Cooked Animation Graph identity mismatch");
    return source.compiled();
}
} // namespace forge
