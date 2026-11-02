#include <filesystem>
#include <forge/navigation_behavior.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace forge;
using Json = nlohmann::json;
using Status = NavigationBehaviorTreeStatus;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected tree validation failure");
}
NavigationBehaviorSource source(Json nodes) {
    auto value = NavigationBehaviorSource::create_tree(AssetId::generate());
    value.document["nodes"] = std::move(nodes);
    value.document["next_id"] = 100;
    return value;
}
Json sequence() {
    return Json::array(
        {{{"id", 1}, {"name", "Root"}, {"kind", "sequence"}, {"children", {2, 3}}},
         {{"id", 2}, {"name", "Wait"}, {"kind", "wait"}, {"seconds", .5}},
         {{"id", 3}, {"name", "Goal"}, {"kind", "move_to_point"}, {"point", {4, 0, 0}}}});
}
Json reactive() {
    return Json::array(
        {{{"id", 1}, {"name", "Root"}, {"kind", "selector"}, {"children", {2, 6}}},
         {{"id", 2}, {"name", "Chase branch"}, {"kind", "reactive_sequence"}, {"children", {3, 4}}},
         {{"id", 3}, {"name", "Near"}, {"kind", "target_near"}, {"distance", 2}},
         {{"id", 4}, {"name", "Chase"}, {"kind", "follow_target"}},
         {{"id", 6}, {"name", "Idle"}, {"kind", "wait"}, {"seconds", 100}}});
}
NavigationBehaviorCursor step(const NavigationBehaviorGraph& graph,
                              const NavigationBehaviorCursor& cursor,
                              NavigationBehaviorTreeInputs input, double dt) {
    const auto selected = select_navigation_behavior_tree(graph, cursor, input);
    input.sampled_leaf = selected.leaf;
    return advance_navigation_behavior_tree(graph, selected, input, dt).cursor;
}
void execution() {
    const auto graph = source(sequence()).compiled();
    NavigationBehaviorCursor cursor;
    const auto selected = select_navigation_behavior_tree(graph, cursor, {});
    check(selected.leaf == 2 && selected.cursor.elapsed == 0 && !selected.cursor.transitions &&
              cursor == NavigationBehaviorCursor{},
          "Selection advanced progress");
    cursor = step(graph, cursor, {}, .25);
    check(cursor.state == 2 && cursor.elapsed == .25 && cursor.transitions == 1,
          "Wait did not advance once");
    cursor = step(graph, cursor, {}, .25);
    check(cursor.state == 3 && cursor.elapsed == 0 && cursor.transitions == 2,
          "Sequence did not continue or leaked time into the next action");
    const auto saved = cursor;
    for (unsigned i = 0; i < 20; ++i)
        check(select_navigation_behavior_tree(graph, cursor, {}).cursor == saved,
              "Repeated selection advanced a running action");
    NavigationBehaviorTreeInputs arrived;
    arrived.arrived = true;
    cursor = step(graph, cursor, arrived, .25);
    check(cursor.tree_status == Status::Success && !cursor.state && !cursor.tree_active &&
              cursor.elapsed == 0 && cursor.transitions == 3,
          "Sequence did not complete");
    check(select_navigation_behavior_tree(graph, cursor, {}).leaf == 2 &&
              cursor.tree_status == Status::Success,
          "Completed root did not restart at the next selection");
    const auto twice =
        source(
            Json::array(
                {{{"id", 1}, {"name", "Root"}, {"kind", "sequence"}, {"children", {2, 3}}},
                 {{"id", 2}, {"name", "First"}, {"kind", "move_to_point"}, {"point", {1, 0, 0}}},
                 {{"id", 3}, {"name", "Second"}, {"kind", "move_to_point"}, {"point", {2, 0, 0}}}}))
            .compiled();
    const auto following = step(twice, {}, arrived, .1);
    check(following.state == 3 && following.tree_status == Status::Running,
          "One arrival completed two different actions");
}
void preemption() {
    const auto graph = source(reactive()).compiled();
    auto cursor = step(graph, {}, {}, .2);
    check(cursor.state == 6 && cursor.elapsed == .2, "Missing target did not select idle fallback");
    NavigationBehaviorTreeInputs near;
    near.target_distance = 1;
    const auto selected = select_navigation_behavior_tree(graph, cursor, near);
    check(selected.leaf == 4 && selected.cursor.elapsed == 0 && cursor.state == 6,
          "Reactive selector did not stage a higher priority branch");
    cursor = step(graph, cursor, near, .2);
    check(cursor.state == 4 && cursor.elapsed == .2, "Follow action did not become active");
    NavigationBehaviorTreeInputs far;
    far.target_distance = 3;
    const auto lower = select_navigation_behavior_tree(graph, cursor, far);
    check(lower.leaf == 6 && lower.cursor.elapsed == 0,
          "Preempted branch retained old elapsed time");
    auto stale = far;
    stale.sampled_leaf = 4;
    stale.arrived = true;
    rejects([&] {
        (void)advance_navigation_behavior_tree(
            graph, select_navigation_behavior_tree(graph, cursor, far), stale, .1);
    });
    auto failed = near;
    failed.action_failed = true;
    cursor = step(graph, cursor, failed, .1);
    check(cursor.state == 6 && cursor.elapsed == 0, "Fallback consumed the failed action's time");
    auto follow = NavigationBehaviorSource::create_tree(AssetId::generate());
    follow.document["nodes"][0] = {{"id", 1}, {"name", "Follow"}, {"kind", "follow_target"}};
    check(step(follow.compiled(), {}, {}, .1).tree_status == Status::Failure,
          "Missing target did not fail a follow leaf");
}
void validation() {
    auto valid = source(sequence());
    valid.validate();
    for (const auto mutation : {0, 1, 2, 3, 4, 5, 6}) {
        auto bad = valid;
        if (mutation == 0)
            bad.document["nodes"][0]["children"] = {2, 2};
        if (mutation == 1)
            bad.document["nodes"][0]["children"] = {2, 99};
        if (mutation == 2)
            bad.document["nodes"][1]["kind"] = "unknown";
        if (mutation == 3)
            bad.document["nodes"][1]["seconds"] = -1;
        if (mutation == 4)
            bad.document["nodes"][1]["id"] = 100;
        if (mutation == 5)
            bad.document["nodes"][0]["children"] = {1, 2};
        if (mutation == 6)
            bad.document["version"] = 2.0;
        rejects([&] { bad.validate(); });
    }
    auto detached = valid;
    detached.document["nodes"][0]["children"] = {2};
    detached.validate();
    rejects([&] { (void)detached.compiled(); });
    auto empty = valid;
    empty.document["nodes"][0]["children"] = Json::array();
    empty.validate();
    rejects([&] { (void)empty.cook(); });
    auto deep = valid;
    deep.document["nodes"] = Json::array();
    for (unsigned id = 1; id <= 9; ++id)
        deep.document["nodes"].push_back(
            id < 9 ? Json{{"id", id},
                          {"name", std::to_string(id)},
                          {"kind", "sequence"},
                          {"children", {id + 1}}}
                   : Json{{"id", id}, {"name", "Last"}, {"kind", "wait"}, {"seconds", 1}});
    rejects([&] { deep.validate(); });
    const auto graph = valid.compiled();
    const auto cursor = step(graph, {}, {}, .1);
    check(Json(cursor).get<NavigationBehaviorCursor>() == cursor, "Tree cursor JSON lost progress");
    auto wrong = cursor;
    wrong.tree_active = 0;
    rejects([&] { validate_navigation_behavior_cursor(graph, wrong); });
    wrong = cursor;
    wrong.tree_children[0] = 7;
    rejects([&] { validate_navigation_behavior_cursor(graph, wrong); });
    auto malformed = Json(cursor);
    malformed["tree"]["children"][0] = -1;
    auto unchanged = cursor;
    rejects([&] { from_json(malformed, unchanged); });
    check(unchanged == cursor, "Rejected cursor decode partially applied");
    for (double dt : {0., -1., 2., std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { (void)step(graph, cursor, {}, dt); });
    auto exhausted = cursor;
    exhausted.transitions = UINT64_MAX;
    rejects([&] { (void)step(graph, exhausted, {}, .1); });
    check(Json(NavigationBehaviorCursor{}).size() == 4, "Legacy cursor JSON changed");
    const auto legacy = NavigationBehaviorSource::create(AssetId::generate()).compiled();
    rejects([&] { validate_navigation_behavior_cursor(legacy, cursor); });
}
void bounds_and_conditions() {
    Json nodes = Json::array(
        {{{"id", 1}, {"name", "Root"}, {"kind", "sequence"}, {"children", {2, 3, 4, 5}}}});
    unsigned leaf = 6;
    for (unsigned group = 2; group <= 5; ++group) {
        Json children = Json::array();
        const unsigned count = group == 5 ? 6 : 7;
        for (unsigned i = 0; i < count; ++i)
            children.push_back(leaf++);
        nodes.push_back({{"id", group},
                         {"name", "Group " + std::to_string(group)},
                         {"kind", "sequence"},
                         {"children", children}});
    }
    for (unsigned id = 6; id <= 32; ++id)
        nodes.push_back({{"id", id},
                         {"name", "Leaf " + std::to_string(id)},
                         {"kind", "wait"},
                         {"seconds", id == 32 ? 1 : 0}});
    const auto full = source(nodes);
    const auto graph = full.compiled();
    const auto selected = select_navigation_behavior_tree(graph, {}, {});
    check(graph.nodes.size() == 32 && selected.leaf == 32,
          "Legal 32-node traversal did not reach its final action");
    const auto cursor = step(graph, {}, {}, .25);
    check(cursor.state == 32 && cursor.elapsed == .25 &&
              Json(cursor).get<NavigationBehaviorCursor>() == cursor,
          "Upper-bound tree path lost its continuation");
    auto too_many = full;
    too_many.document["nodes"].push_back(
        {{"id", 33}, {"name", "Extra"}, {"kind", "wait"}, {"seconds", 0}});
    rejects([&] { too_many.validate(); });
    auto wide = full;
    wide.document["nodes"][0]["children"] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    rejects([&] { wide.validate(); });
    auto sparse = NavigationBehaviorSource::create_tree(AssetId::generate());
    sparse.document["entry"] = 100;
    sparse.document["next_id"] = std::uint64_t(UINT32_MAX) + 1;
    sparse.document["nodes"] = Json::array(
        {{{"id", 100}, {"name", "Root"}, {"kind", "sequence"}, {"children", {UINT32_MAX}}},
         {{"id", UINT32_MAX}, {"name", "Last ID"}, {"kind", "wait"}, {"seconds", 1}}});
    check(select_navigation_behavior_tree(sparse.compiled(), {}, {}).leaf == UINT32_MAX,
          "Sparse maximum node ID was treated as an array index");
    for (const char* kind : {"target_near", "target_far", "target_missing"}) {
        auto condition = NavigationBehaviorSource::create_tree(AssetId::generate());
        condition.document["nodes"][0] = {
            {"id", 1}, {"name", "Condition"}, {"kind", kind}, {"distance", 3}};
        const auto compiled = condition.compiled();
        NavigationBehaviorTreeInputs input;
        input.target_distance = 3;
        const bool missing = std::string_view(kind) == "target_missing";
        check(select_navigation_behavior_tree(compiled, {}, input).cursor.tree_status ==
                  (missing ? Status::Failure : Status::Success),
              "Target condition boundary is incorrect");
        check(select_navigation_behavior_tree(compiled, {}, {}).cursor.tree_status ==
                  (missing ? Status::Success : Status::Failure),
              "Missing target condition is incorrect");
        input.target_distance = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { (void)select_navigation_behavior_tree(compiled, {}, input); });
    }
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing legacy fixture");
    return {std::istreambuf_iterator<char>(stream), {}};
}
void compatibility(const std::filesystem::path& root) {
    const auto catalog = Json::parse(bytes(root / "forge.assets.json"));
    unsigned checked = 0;
    for (const auto& row : catalog.at("assets"))
        if (row.at("type") == "navigation_behavior" &&
            (row.at("source") == "Assets/Navigation/Patrol.behavior.json" ||
             row.at("source") == "Assets/Navigation/Chase.behavior.json")) {
            const auto raw = bytes(root / row.at("source").get<std::string>());
            const auto old = NavigationBehaviorSource::parse(std::as_bytes(std::span(raw)));
            const auto cooked = old.cook();
            const auto cached =
                bytes(root / ".forge/cache/derived" /
                      row.at("metadata").at("forge.import").at("key").get<std::string>() /
                      "behavior.json");
            check(std::string(reinterpret_cast<const char*>(cooked.data()), cooked.size()) ==
                      cached,
                  "Legacy recipe output changed for an already admitted source");
            ++checked;
        }
    check(checked == 2, "Expected both legacy Gallery behavior fixtures");
    auto tree = source(sequence());
    tree.document["editor_layout"] = {
        {"format", navigation_behavior_layout_format},
        {"version", 1},
        {"positions", Json::array({{{"state", 1}, {"x", 2}, {"y", 3}}})}};
    const auto cooked = tree.cook();
    const auto loaded = NavigationBehaviorSource::parse_cooked(cooked);
    check(loaded.compiled().profile == NavigationBehaviorProfile::Tree &&
              !loaded.document.contains("editor_layout"),
          "Cooked tree retained layout or lost its profile");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Gallery root for exact legacy compatibility");
        execution();
        preemption();
        validation();
        bounds_and_conditions();
        compatibility(argv[1]);
        std::cout << "Behavior tree traces, preemption, validation and exact legacy recipes PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
