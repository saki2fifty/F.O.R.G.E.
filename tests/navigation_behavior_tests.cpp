#include "state_graph_layout.hpp"
#include <forge/navigation_behavior.hpp>
#include <functional>
#include <iostream>
#include <limits>

using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid behavior was admitted");
}
NavigationBehaviorSource fixture() {
    auto source = NavigationBehaviorSource::create(AssetId::generate());
    source.document["next_id"] = 32;
    source.document["states"] = Json::array(
        {{{"id", 1},
          {"name", "Wait"},
          {"action", {{"kind", "wait"}}},
          {"transitions",
           Json::array(
               {{{"id", 11}, {"to", 3}, {"condition", {{"kind", "target_near"}, {"distance", 3}}}},
                {{"id", 12}, {"to", 2}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}}})}},
         {{"id", 2},
          {"name", "Patrol"},
          {"action", {{"kind", "move_to_point"}, {"point", {6, 0, 2}}}},
          {"transitions",
           Json::array({{{"id", 21}, {"to", 1}, {"condition", {{"kind", "arrived"}}}},
                        {{"id", 22},
                         {"to", 3},
                         {"condition", {{"kind", "target_near"}, {"distance", 3}}}}})}},
         {{"id", 3},
          {"name", "Chase"},
          {"action", {{"kind", "follow_target"}}},
          {"transitions",
           Json::array({{{"id", 30}, {"to", 1}, {"condition", {{"kind", "target_missing"}}}},
                        {{"id", 31},
                         {"to", 2},
                         {"condition", {{"kind", "target_far"}, {"distance", 5}}}}})}}});
    return source;
}
void admission_and_layout() {
    auto source = fixture();
    const auto text = source.document.dump();
    check(NavigationBehaviorSource::parse(std::as_bytes(std::span(text))).document ==
              source.document,
          "Behavior source lost identity or authored priority");
    source.document["extension"] = {{"script", "opaque"}, {"scene", "not-a-reference"}};
    const auto cooked = source.cook();
    check(NavigationBehaviorSource::parse_cooked(cooked).document == source.document,
          "Cooked behavior lost opaque extensions");
    asset_detail::set_state_graph_position(source.document, 2, {450, -210},
                                           navigation_behavior_layout_format);
    source.validate();
    check(source.cook() == cooked &&
              asset_detail::state_graph_position(source.document, 2, 0,
                                                 navigation_behavior_layout_format) ==
                  std::array<double, 2>{450, -210},
          "Source-owned graph layout changed runtime semantics");
    check(!asset_detail::has_state_graph_layout(source.document),
          "Behavior positions were interpreted as animation layout");
    auto bad = source;
    bad.document["editor_layout"]["positions"][0]["state"] = 400;
    rejects([&] { bad.validate(); });
    bad = source;
    bad.document["editor_layout"]["version"] = 1.0;
    rejects([&] { bad.validate(); });
    bad = source;
    bad.document["editor_layout"]["format"] = "future.opaque.layout";
    bad.validate();
    check(NavigationBehaviorSource::parse_cooked(bad.cook()).document == bad.document,
          "Unrecognized layout was silently interpreted/stripped");
    rejects([&] {
        asset_detail::set_state_graph_position(bad.document, 2, {0, 0},
                                               navigation_behavior_layout_format);
    });
    const auto corrupt = [&](const std::function<void(Json&)>& change) {
        auto value = fixture();
        change(value.document);
        rejects([&] { value.validate(); });
    };
    corrupt([](Json& d) { d["version"] = 1.0; });
    corrupt([](Json& d) { d["entry"] = 4; });
    corrupt([](Json& d) { d["next_id"] = 0; });
    corrupt([](Json& d) { d["states"][0]["id"] = 1.0; });
    corrupt([](Json& d) { d["states"][0]["id"] = true; });
    corrupt([](Json& d) { d["states"][0]["id"] = std::uint64_t(UINT32_MAX) + 1; });
    corrupt([](Json& d) { d["states"][0]["transitions"][0]["id"] = 2; });
    corrupt([](Json& d) { d["states"][0]["transitions"][0]["to"] = 11; });
    corrupt([](Json& d) { d["states"][1]["name"] = "Wait"; });
    corrupt([](Json& d) { d["states"][1]["name"] = "Invisible\ncontrol"; });
    corrupt([](Json& d) { d["states"][1]["action"]["kind"] = "execute_script"; });
    corrupt([](Json& d) { d["states"][1]["action"]["point"] = {1, 2}; });
    corrupt([](Json& d) { d["states"][1]["action"]["point"][0] = 4097; });
    corrupt([](Json& d) {
        d["states"][1]["action"]["point"][0] = std::numeric_limits<double>::infinity();
    });
    corrupt([](Json& d) { d["states"][0]["transitions"][0]["condition"]["distance"] = -1; });
    corrupt([](Json& d) { d["states"][0]["transitions"][1]["condition"]["seconds"] = "1"; });
    corrupt([](Json& d) { d["extension"] = std::string(navigation_behavior_byte_limit, 'x'); });
    corrupt([](Json& d) {
        Json nested = 1;
        for (unsigned i = 0; i < 13; ++i)
            nested = {{"child", std::move(nested)}};
        d["extension"] = std::move(nested);
    });
    corrupt([](Json& d) { d["extension"] = std::numeric_limits<double>::quiet_NaN(); });
    auto raw = text;
    raw.insert(1, "\"entry\":1,");
    rejects([&] { NavigationBehaviorSource::parse(std::as_bytes(std::span(raw))); });
    raw = text + std::string(1, '\0');
    rejects([&] { NavigationBehaviorSource::parse(std::as_bytes(std::span(raw))); });
    auto empty = source;
    empty.document["entry"] = 0;
    empty.document["states"] = Json::array();
    empty.document.erase("editor_layout");
    empty.validate();
    rejects([&] { empty.cook(); });
    rejects([&] { NavigationBehaviorSource::parse_cooked(std::as_bytes(std::span(text))); });
}
void limits() {
    auto source = NavigationBehaviorSource::create(AssetId::generate());
    auto& d = source.document;
    d["states"] = Json::array();
    unsigned id = 33;
    for (unsigned i = 1; i <= 32; ++i) {
        Json edges = Json::array();
        for (unsigned edge = 0; edge < 4; ++edge)
            edges.push_back({{"id", id++},
                             {"to", i % 32 + 1},
                             {"condition", {{"kind", "elapsed"}, {"seconds", edge}}}});
        d["states"].push_back({{"id", i},
                               {"name", std::to_string(i)},
                               {"action", {{"kind", "wait"}}},
                               {"transitions", edges}});
    }
    d["next_id"] = id;
    check(source.compiled().states.size() == 32, "Legal bounded graph was rejected");
    auto bad = source;
    bad.document["states"][0]["transitions"].push_back(
        {{"id", id}, {"to", 2}, {"condition", {{"kind", "elapsed"}, {"seconds", 0}}}});
    bad.document["next_id"] = id + 1;
    rejects([&] { bad.validate(); });
    bad = source;
    auto extra = bad.document["states"][0];
    extra["id"] = id;
    extra["name"] = "33rd";
    extra["transitions"] = Json::array();
    bad.document["next_id"] = id + 1;
    bad.document["states"].push_back(extra);
    rejects([&] { bad.validate(); });
}
void evaluation() {
    const auto graph = fixture().compiled();
    NavigationBehaviorCursor cursor, independent;
    for (unsigned i = 0; i < 3; ++i)
        check(advance_navigation_behavior(graph, cursor, {10, false}, .25).id == 1,
              "Elapsed transition ran early");
    check(advance_navigation_behavior(graph, cursor, {2, false}, .25).id == 3 &&
              cursor.last_transition == 11 && cursor.transitions == 1 && cursor.elapsed == 0,
          "Outgoing priority or same-boundary target action failed");
    check(advance_navigation_behavior(graph, independent, {10, false}, 1).id == 2 &&
              independent.last_transition == 12,
          "Independent cursor clock/state was shared");
    check(advance_navigation_behavior(graph, cursor, {4, false}, .5).id == 3 &&
              cursor.elapsed == .5,
          "Chase hysteresis did not retain its current state");
    check(advance_navigation_behavior(graph, cursor, {6, false}, .5).point ==
                  std::array<double, 3>{6, 0, 2} &&
              cursor.last_transition == 31,
          "Far target did not choose the point action");
    check(advance_navigation_behavior(graph, cursor, {{}, true}, .25).id == 1,
          "Arrival did not enter wait");
    check(advance_navigation_behavior(graph, cursor, {0, false}, .25).id == 3 &&
              advance_navigation_behavior(graph, cursor, {{}, false}, .25).id == 1 &&
              cursor.last_transition == 30,
          "Missing target did not use its explicit transition");
    const auto saved = cursor;
    const Json encoded = saved;
    check(encoded.get<NavigationBehaviorCursor>() == saved, "Cursor recovery value lost progress");
    for (const auto* field : {"state", "last_transition", "transitions"}) {
        auto invalid_json = encoded;
        invalid_json[field] = -1;
        rejects([&] { invalid_json.get<NavigationBehaviorCursor>(); });
        invalid_json[field] = 1.0;
        rejects([&] { invalid_json.get<NavigationBehaviorCursor>(); });
    }
    for (double dt : {0., -1., 1.001, std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { advance_navigation_behavior(graph, cursor, {}, dt); });
        check(cursor == saved, "Rejected tick changed cursor progress");
    }
    rejects([&] { advance_navigation_behavior(graph, cursor, {-1, false}, .1); });
    check(cursor == saved, "Rejected target input changed progress");
    auto invalid = cursor;
    invalid.last_transition = 11;
    rejects([&] { validate_navigation_behavior_cursor(graph, invalid); });
    invalid = cursor;
    invalid.state = 400;
    rejects([&] { advance_navigation_behavior(graph, invalid, {}, .1); });
    auto cycle = fixture();
    cycle.document["states"][0]["transitions"] = Json::array(
        {{{"id", 11}, {"to", 2}, {"condition", {{"kind", "elapsed"}, {"seconds", 0}}}}});
    cycle.document["states"][1]["transitions"] = Json::array(
        {{{"id", 12}, {"to", 1}, {"condition", {{"kind", "elapsed"}, {"seconds", 0}}}}});
    const auto cyclic = cycle.compiled();
    NavigationBehaviorCursor loop;
    for (unsigned i = 1; i <= 120; ++i) {
        advance_navigation_behavior(cyclic, loop, {}, 1. / 60);
        check(loop.transitions == i && loop.state == (i % 2 ? 2u : 1u) && loop.elapsed == 0,
              "Zero-time cycle ran more than once per tick");
    }
    const auto waiting = NavigationBehaviorSource::create(AssetId::generate()).compiled();
    NavigationBehaviorCursor long_wait{1, 0, 3599.75, 0};
    advance_navigation_behavior(waiting, long_wait, {}, 1);
    advance_navigation_behavior(waiting, long_wait, {}, 1);
    check(long_wait.elapsed == 3600, "Long-running state clock did not saturate safely");
}
} // namespace
int main() {
    try {
        admission_and_layout();
        limits();
        evaluation();
        std::cout << "Behavior source/cook/admission/priority/cycles/cursor isolation PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
