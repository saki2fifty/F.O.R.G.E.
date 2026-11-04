#include "native_system_access.hpp"
#include "native_system_dependencies.hpp"
#include <array>
#include <forge/runtime.hpp>
#include <iostream>
#include <vector>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
const Json& node(const Json& data, ecs_entity_t id) {
    for (const auto& item : data.at("nodes"))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("Expected native lookup node missing");
}
Json copy(flecs::world& world, std::initializer_list<ecs_entity_t> roots) {
    const std::span<const ecs_entity_t> ids(roots.begin(), roots.size());
    auto data = detail::native_system_dependencies(world, ids);
    detail::validate_native_system_dependencies(ids, data);
    return data;
}
void lookup_and_validation() {
    WorldContext context(WorldRole::Runtime);
    auto& world = context.world();
    auto phase = world.entity("LookupPhase").add(flecs::Phase);
    auto root = world.entity("LookupRoot").depends_on(phase);
    const auto owned = copy(world, {root});
    check(owned.at("nodes").size() == 2 && owned.at("links").size() == 1 &&
              owned.at("links")[0].at("source") == root.id() &&
              owned.at("links")[0].at("target") == phase.id() &&
              owned.at("links")[0].at("slot") == 0 && owned.at("links")[0].at("pair_owned") &&
              node(owned, phase).at("phase_tag") && !node(owned, phase).at("root").get<bool>(),
          "Native target identity, marker, root or pair presence was lost");
    auto prototype = world.prefab("LookupPrototype").depends_on(phase);
    auto inherited = world.entity("LookupInherited").is_a(prototype);
    const auto resolved = copy(world, {inherited});
    check(resolved.at("links").size() == 1 &&
              !resolved.at("links")[0].at("pair_owned").get<bool>() &&
              resolved.at("links")[0].at("target") == phase.id(),
          "Native inherited lookup was mislabeled an owned pair");
    auto second = world.entity("LookupSecondPhase").add(flecs::Phase).depends_on(phase);
    root.depends_on(second);
    const auto multiple = copy(world, {root});
    for (const auto& link : multiple.at("links"))
        if (link.at("source") == root.id())
            check(link.at("target") ==
                      ecs_get_target(world, root, EcsDependsOn, link.at("slot").get<int>()),
                  "Copied lookup slots disagree with native target enumeration");
    check(node(multiple, second).at("phase_tag") && multiple.at("links").size() == 3,
          "Referenced phase chain was omitted");
    const std::array<ecs_entity_t, 1> roots{root};
    for (const auto& key : {"id", "root", "phase_tag", "truncated", "name"}) {
        auto invalid = multiple;
        invalid["nodes"][0][key] = 1.5;
        bool rejected = false;
        try {
            detail::validate_native_system_dependencies(roots, invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Malformed native dependency node was accepted");
    }
    for (const auto& key : {"source", "target", "slot", "pair_owned"}) {
        auto invalid = multiple;
        invalid["links"][0][key] = "bad";
        bool rejected = false;
        try {
            detail::validate_native_system_dependencies(roots, invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Malformed native dependency link was accepted");
    }
    auto invalid = multiple;
    invalid["nodes"].push_back({{"id", std::uint64_t(999999)},
                                {"name", "Unrelated"},
                                {"root", false},
                                {"phase_tag", false},
                                {"truncated", false}});
    bool rejected = false;
    try {
        detail::validate_native_system_dependencies(roots, invalid);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Unrelated nodes escaped the captured root scope");
    invalid = multiple;
    invalid["links"][0]["target"] = std::uint64_t(999999);
    rejected = false;
    try {
        detail::validate_native_system_dependencies(roots, invalid);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Dangling native target reference was accepted");
}
void budgets() {
    WorldContext context(WorldRole::Runtime);
    auto& world = context.world();
    auto root = world.entity();
    for (unsigned i = 0; i < 10; ++i)
        root.depends_on(world.entity());
    const auto limited = copy(world, {root});
    check(limited.at("links").size() == 8 && limited.at("nodes").size() == 9 &&
              limited.at("truncated") && node(limited, root).at("truncated"),
          "Per-node native lookup limit was exceeded or hidden");
    std::vector<ecs_entity_t> roots;
    for (unsigned i = 0; i < 256; ++i) {
        auto source = world.entity().depends_on(world.entity()).depends_on(world.entity());
        roots.push_back(source);
    }
    const auto nodes = detail::native_system_dependencies(world, roots);
    detail::validate_native_system_dependencies(roots, nodes);
    check(nodes.at("nodes").size() == 512 && nodes.at("links").size() == 256 &&
              nodes.at("truncated"),
          "Global native node budget was not enforced independently");
    roots.clear();
    auto end = world.entity();
    auto start = world.entity().depends_on(end);
    for (unsigned i = 0; i < 256; ++i)
        roots.push_back(world.entity().depends_on(start).depends_on(end));
    const auto links = detail::native_system_dependencies(world, roots);
    detail::validate_native_system_dependencies(roots, links);
    check(links.at("nodes").size() == 258 && links.at("links").size() == 512 &&
              links.at("truncated") && node(links, start).at("truncated"),
          "Global native link budget was not enforced independently");
    roots.push_back(world.entity());
    bool rejected = false;
    try {
        (void)detail::native_system_dependencies(world, roots);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Excessive native root set was accepted");
}
struct Counter {
    int value = 0;
};
void runtime(unsigned stages) {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    auto& world = scene.world();
    RuntimeSimulation simulation(engine.world(), scene, {stages});
    std::vector<flecs::entity> actors;
    for (unsigned i = 0; i < 256; ++i)
        actors.push_back(world.entity().set<Counter>({0}));
    auto before = world.system<Counter>("DependencyBeforePhase")
                      .kind(world.lookup("forge.runtime.Gameplay"))
                      .term_at(0)
                      .src()
                      .self()
                      .multi_threaded()
                      .each([](Counter& value) { ++value.value; });
    before.add<FixedSimulation>();
    auto phase = world.entity("DependencyLatePhase")
                     .add(flecs::Phase)
                     .depends_on(world.lookup("forge.runtime.Gameplay"));
    auto after = world.system<Counter>("DependencyConsumer")
                     .kind(phase)
                     .term_at(0)
                     .src()
                     .self()
                     .multi_threaded()
                     .each([](Counter& value) { value.value *= 2; });
    after.add<FixedSimulation>();
    after.depends_on(before);
    engine.services().profiling(true);
    (void)simulation.system_profile();
    simulation.tick(1.f / 60);
    const auto snapshot = simulation.system_profile();
    std::vector<ecs_entity_t> roots;
    std::size_t a = 999, b = 999;
    for (std::size_t i = 0; i < snapshot.at("entries").size(); ++i) {
        const auto& row = snapshot.at("entries")[i];
        if (row.at("kind") != "system")
            continue;
        roots.push_back(row.at("id").get<ecs_entity_t>());
        if (row.at("id") == before.id())
            a = i;
        if (row.at("id") == after.id()) {
            b = i;
            check(row.at("phase").get<std::string>().find("DependencyBeforePhase") !=
                      std::string::npos,
                  "Legacy first-target field no longer matches the native API");
        }
    }
    check(a < b && b != 999, "Native actual plan lost producer/consumer order");
    for (auto actor : actors)
        check(actor.get<Counter>().value == 2,
              "Dependency observation changed native numeric order");
    const auto& data = snapshot.at("dependencies");
    detail::validate_native_system_dependencies(roots, data);
    check(!node(data, before).at("phase_tag").get<bool>() && node(data, phase).at("phase_tag"),
          "Another system was mislabeled as a phase");
    check(simulation.system_profile() == snapshot, "Repeated dependency reads resampled gameplay");
    engine.services().clear_profiles();
    check(!simulation.system_profile().at("available").get<bool>(),
          "Clear retained stale dependency targets");
    simulation.tick(1.f / 60);
    for (auto actor : actors)
        check(actor.get<Counter>().value == 6,
              "Second native dependency tick lost numeric equivalence");
}
} // namespace
int main() {
    try {
        lookup_and_validation();
        budgets();
        runtime(1);
        runtime(4);
        std::cout << "Native dependency lookup, bounds and actual runtime order PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
