#include "editor/animation_graph_canvas_model.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_graph.hpp>
#include <iostream>
#include <limits>
#include <set>

using namespace forge;
namespace {
using Json = nlohmann::json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected Animation Graph rejection");
}
Json clip(AssetId id) {
    return {{"id", id}, {"name", "Motion"}, {"kind", "clip"}, {"clip", AssetId::generate()}};
}
Json mix(AssetId id, AssetId first, AssetId second, bool additive = false) {
    return {{"id", id},       {"name", "Layer"},  {"kind", additive ? "additive" : "blend"},
            {"first", first}, {"second", second}, {"mask", nullptr},
            {"weight", .5}};
}
AnimationGraphSource fixture() {
    auto source = AnimationGraphSource::create(AssetId::generate());
    auto& d = source.document;
    const auto a = AssetId::generate(), b = AssetId::generate(), blend = AssetId::generate(),
               add = AssetId::generate();
    d["skeleton"] = AssetId::generate();
    d["cycle_seconds"] = 2.5;
    d["inputs"] = {{{"slot", 4}, {"name", "Upper body"}, {"initial", .25}},
                   {{"slot", 1}, {"name", "Speed"}, {"initial", -1}}};
    // Authored row order deliberately differs from dependency order. Both the
    // intermediate blend and the output share leaf B; it must evaluate once.
    d["nodes"] = {mix(add, blend, b, true), clip(b), mix(blend, a, b), clip(a)};
    d["nodes"][0]["weight"] = {{"input", 4}, {"scale", 2}, {"bias", -.125}};
    d["nodes"][0]["mask"] = AssetId::generate();
    d["output"] = add;
    return source;
}
void roundtrip() {
    auto source = fixture();
    source.document["opaque"] = {{"kept", true}};
    source.document["nodes"][1]["extension"] = "retained by source";
    const auto text = source.document.dump();
    auto parsed = AnimationGraphSource::parse(std::as_bytes(std::span(text)));
    check(parsed.document == source.document, "Source parse discarded unknown authoring fields");
    const auto graph = parsed.compiled();
    check(graph.asset == source.asset() &&
              graph.skeleton == source.document.at("skeleton").get<AssetId>() &&
              graph.cycle_seconds == 2.5 && graph.nodes.size() == 4 && graph.output == 3,
          "Graph identity, timing, shared-node count or output changed");
    check(graph.nodes[0].kind == AnimationGraphNodeKind::Clip &&
              graph.nodes[1].kind == AnimationGraphNodeKind::Clip && graph.nodes[2].first == 0 &&
              graph.nodes[2].second == 1 &&
              graph.nodes[3].kind == AnimationGraphNodeKind::Additive &&
              graph.nodes[3].first == 2 && graph.nodes[3].second == 1 && graph.nodes[3].mask.id,
          "Topological compilation lost operands, mask or shared dependency");
    auto values = graph.initial_inputs();
    check(values == std::array<double, 4>{-1, 0, 0, .25} && graph.inputs[0].slot == 1 &&
              graph.inputs[1].slot == 4 && graph.nodes[2].weight.evaluate(values) == .5 &&
              graph.nodes[3].weight.evaluate(values) == .375,
          "Stable input slots/defaults or constant/bound weights changed");
    values[3] = -1000000;
    check(graph.nodes[3].weight.evaluate(values) == 0, "Input weight did not clamp below zero");
    values[3] = 1000000;
    check(graph.nodes[3].weight.evaluate(values) == 1, "Input weight did not clamp above one");
    values[1] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)graph.nodes[2].weight.evaluate(values); });
    const auto cooked = source.cook();
    const auto loaded = decode_animation_graph(cooked, source.asset());
    const auto wrapper = Json::parse(cooked);
    check(loaded.nodes.size() == graph.nodes.size() &&
              loaded.nodes[3].mask.id == graph.nodes[3].mask.id &&
              !wrapper.at("document").contains("opaque"),
          "Cooked graph lost its mask or retained opaque source-only payload");
    auto reordered = source;
    std::reverse(reordered.document["nodes"].begin(), reordered.document["nodes"].end());
    std::reverse(reordered.document["inputs"].begin(), reordered.document["inputs"].end());
    check(reordered.cook() == cooked, "Authored row reordering changed canonical cooked bytes");
    rejects([&] { (void)decode_animation_graph(cooked, AssetId::generate()); });
    rejects([&] { (void)decode_animation_graph(cooked, {}); });
    check(loaded.resident_bytes() >=
              sizeof(loaded) + loaded.nodes.size() * sizeof(AnimationGraphNode),
          "Graph retained estimate omitted node storage");
    auto malformed = wrapper;
    malformed["document"]["nodes"][0]["extra"] = true;
    const auto malformed_text = malformed.dump();
    rejects([&] {
        (void)AnimationGraphSource::parse_cooked(std::as_bytes(std::span(malformed_text)));
    });
    malformed = wrapper;
    malformed["version"] = 1.0;
    const auto fractional_version = malformed.dump();
    rejects([&] {
        (void)AnimationGraphSource::parse_cooked(std::as_bytes(std::span(fractional_version)));
    });
    malformed = wrapper;
    malformed["unexpected"] = true;
    const auto extra_wrapper = malformed.dump();
    rejects(
        [&] { (void)AnimationGraphSource::parse_cooked(std::as_bytes(std::span(extra_wrapper))); });
}
void drafts_and_topology() {
    auto draft = AnimationGraphSource::create(AssetId::generate());
    draft.validate();
    rejects([&] { (void)draft.cook(); });
    const auto a = AssetId::generate(), b = AssetId::generate();
    draft.document["nodes"] = {clip(a), mix(b, a, a)};
    draft.document["nodes"][0]["clip"] = nullptr;
    draft.document["nodes"][1]["first"] = nullptr;
    draft.validate();
    rejects([&] { (void)draft.compiled(); });
    auto source = fixture();
    const auto original = source.document;
    const auto bad = [&](auto edit) {
        source.document = original;
        edit(source.document);
        rejects([&] { source.validate(); });
    };
    bad([](auto& d) { d["output"] = AssetId::generate(); });
    bad([](auto& d) { d["nodes"][0]["first"] = AssetId::generate(); });
    bad([](auto& d) { d["nodes"][2]["first"] = d["nodes"][0]["id"]; });
    bad([](auto& d) { d["nodes"][0]["second"] = d["nodes"][0]["id"]; });
    bad([](auto& d) { d["nodes"][1]["id"] = d["nodes"][0]["id"]; });
    bad([](auto& d) { d["nodes"][0]["kind"] = "unknown"; });
    bad([](auto& d) { d["inputs"][0]["slot"] = 4.0; });
    bad([](auto& d) { d["inputs"][0]["slot"] = true; });
    bad([](auto& d) { d["inputs"][0]["slot"] = UINT64_MAX; });
    bad([](auto& d) { d["inputs"][0]["slot"] = 0; });
    bad([](auto& d) { d["inputs"][0]["slot"] = -1; });
    bad([](auto& d) { d["inputs"][0]["slot"] = 1; });
    bad([](auto& d) { d["inputs"][0]["name"] = "Speed"; });
    bad([](auto& d) { d["inputs"][0]["initial"] = std::numeric_limits<double>::infinity(); });
    bad([](auto& d) { d["nodes"][0]["weight"]["input"] = 3; });
    bad([](auto& d) { d["nodes"][0]["weight"]["scale"] = 1000001; });
    bad([](auto& d) { d["nodes"][0]["weight"]["bias"] = "0"; });
    bad([](auto& d) { d["nodes"][0]["weight"] = -.1; });
    bad([](auto& d) { d["nodes"][0]["weight"] = 1.1; });
    bad([](auto& d) { d["cycle_seconds"] = 0; });
    bad([](auto& d) { d["cycle_seconds"] = 3601; });
    bad([](auto& d) { d["nodes"][0]["name"] = "Bad\nname"; });
    bad([](auto& d) { d["nodes"][0]["name"] = std::string(65, 'x'); });
    source.document = original;
    source.document["nodes"].push_back(clip(AssetId::generate()));
    source.validate();
    rejects([&] { (void)source.compiled(); });
    // A disconnected self-cycle must reject even while the output remains null.
    draft.document["nodes"][1]["first"] = b;
    rejects([&] { draft.validate(); });
}
void canvas_identity_and_layout() {
    auto source = fixture();
    const auto id = source.document.at("nodes")[2].at("id").get<AssetId>();
    asset_detail::set_animation_graph_position(source.document, id, {-200, 125});
    source.document["editor_layout"]["extension"] = "preserved";
    source.document["editor_layout"]["positions"][0]["extension"] = true;
    source.validate();
    const auto original = source.document;
    ui::AnimationGraphCanvasModel model(original);
    check(model.identity(model.handle(id)) == id && !model.identity(0),
          "Canvas mapping changed a graph-local identity");
    auto projected = model.projected;
    asset_detail::set_state_graph_position(projected, model.handle(id), {320, 180});
    model.apply(source.document, projected);
    source.validate();
    check(asset_detail::animation_graph_position(source.document, id, 2) ==
                  std::array<double, 2>{320, 180} &&
              source.document.at("nodes") == original.at("nodes") &&
              source.document.at("editor_layout").at("extension") == "preserved" &&
              source.document.at("editor_layout").at("positions")[0].at("extension") == true,
          "Canvas drag changed identities, connections or opaque metadata");
    check(!Json::parse(source.cook()).at("document").contains("editor_layout"),
          "Source-only positions entered the cooked graph");
    const auto leaf = source.document.at("nodes")[3].at("id").get<AssetId>();
    auto linked = model.projected;
    linked["states"][0]["second"] = model.handle(leaf);
    auto connected = original;
    model.apply(connected, linked);
    AnimationGraphSource{connected}.validate();
    check(connected.at("nodes")[0].at("second").get<AssetId>() == leaf &&
              connected.at("nodes")[0].at("first") == original.at("nodes")[0].at("first"),
          "Canvas input2 rewired the wrong UUID port");
    auto reordered = original;
    std::reverse(reordered["nodes"].begin(), reordered["nodes"].end());
    rejects([&] { model.apply(reordered, projected); });
    auto invalid = original;
    invalid["editor_layout"]["positions"].push_back(invalid["editor_layout"]["positions"][0]);
    rejects([&] { AnimationGraphSource{invalid}.validate(); });
    invalid = original;
    invalid["editor_layout"]["positions"][0]["node"] = AssetId::generate();
    rejects([&] { AnimationGraphSource{invalid}.validate(); });
    invalid = original;
    invalid["editor_layout"]["version"] = 1.0;
    rejects([&] { AnimationGraphSource{invalid}.validate(); });
    invalid = original;
    invalid["editor_layout"]["positions"][0]["x"] = std::numeric_limits<double>::infinity();
    rejects([&] { AnimationGraphSource{invalid}.validate(); });
    invalid = original;
    invalid["editor_layout"]["positions"][0]["y"] = 1000001;
    rejects([&] { AnimationGraphSource{invalid}.validate(); });
    auto opaque = original;
    opaque["editor_layout"] = {{"format", "project.custom-layout"}, {"opaque", true}};
    AnimationGraphSource{opaque}.validate();
    rejects([&] { asset_detail::set_animation_graph_position(opaque, id, {0, 0}); });
    check(opaque.at("editor_layout").at("opaque") == true,
          "Canvas overwrote an unrecognized layout extension");
    asset_detail::remove_animation_graph_position(source.document, id);
    check(source.document.at("editor_layout").at("positions").empty(),
          "Removing a node left a dangling source position");
}
void bounds() {
    auto source = AnimationGraphSource::create(AssetId::generate());
    source.document["skeleton"] = AssetId::generate();
    std::array<AssetId, 8> leaves;
    for (auto& id : leaves) {
        id = AssetId::generate();
        source.document["nodes"].push_back(clip(id));
    }
    auto output = leaves[0];
    for (unsigned i = 1; source.document["nodes"].size() < animation_graph_node_limit; ++i) {
        const auto id = AssetId::generate();
        source.document["nodes"].push_back(mix(id, output, leaves[i % leaves.size()], i % 2));
        output = id;
    }
    source.document["output"] = output;
    const auto graph = decode_animation_graph(source.cook(), source.asset());
    check(graph.nodes.size() == 32 && graph.output == 31,
          "Maximum graph did not admit exactly32 nodes");
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].kind != AnimationGraphNodeKind::Clip)
            check(graph.nodes[i].first < i && graph.nodes[i].second < i,
                  "Maximum graph dependency order contains a forward reference");
    auto excessive = source;
    excessive.document["nodes"].push_back(mix(AssetId::generate(), leaves[0], output));
    rejects([&] { excessive.validate(); });
    excessive = source;
    excessive.document["nodes"].erase(excessive.document["nodes"].end() - 1);
    excessive.document["output"] = nullptr;
    excessive.document["nodes"].push_back(clip(AssetId::generate()));
    rejects([&] { excessive.validate(); });
    auto small = fixture();
    auto text = small.document.dump();
    text.insert(1, "\"version\":1,");
    rejects([&] { (void)AnimationGraphSource::parse(std::as_bytes(std::span(text))); });
    text = small.document.dump();
    text.push_back('\0');
    rejects([&] { (void)AnimationGraphSource::parse(std::as_bytes(std::span(text))); });
    text.assign(animation_graph_byte_limit + 1, ' ');
    rejects([&] { (void)AnimationGraphSource::parse(std::as_bytes(std::span(text))); });
    Json nested = 1;
    for (unsigned i = 0; i < 14; ++i)
        nested = {{"next", std::move(nested)}};
    small.document["opaque"] = nested;
    text = small.document.dump();
    rejects([&] { (void)AnimationGraphSource::parse(std::as_bytes(std::span(text))); });
    small = fixture();
    small.document["opaque"] = Json::array();
    for (unsigned i = 0; i < 12000; ++i)
        small.document["opaque"].push_back(0);
    text = small.document.dump();
    check(text.size() < animation_graph_byte_limit, "Event-limit fixture exceeds byte limit first");
    rejects([&] { (void)AnimationGraphSource::parse(std::as_bytes(std::span(text))); });
}
} // namespace
int main() {
    try {
        roundtrip();
        drafts_and_topology();
        canvas_identity_and_layout();
        bounds();
        std::cout << "Animation Graph source/topology/cook/weights/bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
