#include "ParallelMotion.hpp"
#include "native_system_access.hpp"
#include <cmath>
#include <forge/runtime.hpp>
#include <forge/transform_components.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Value {
    int value = 0;
};
struct Other {
    int value = 0;
};
Json query_access(flecs::world& world, std::initializer_list<ecs_term_t> terms,
                  std::size_t budget = detail::native_access_term_capacity) {
    ecs_query_desc_t desc{};
    std::copy(terms.begin(), terms.end(), desc.terms);
    std::unique_ptr<ecs_query_t, decltype(&ecs_query_fini)> query(ecs_query_init(world, &desc),
                                                                  ecs_query_fini);
    check(bool(query), "Native access fixture query failed");
    return detail::native_system_access(world, query.get(), budget);
}
Json rows(std::initializer_list<Json> accesses) {
    Json result = Json::array();
    for (const auto& access : accesses)
        result.push_back({{"kind", "system"}, {"access", access}});
    return result;
}
void declarations() {
    WorldContext context(WorldRole::Runtime);
    auto& world = context.world();
    const auto id = world.id<Value>();
    world.component<Value>().add(flecs::OnInstantiate, flecs::Inherit);
    const auto source = world.entity().set<Value>({1});
    const auto other_source = world.entity().set<Value>({2});
    ecs_term_t own{};
    own.id = id;
    own.src.id = EcsSelf;
    const auto owned = query_access(world, {own});
    const auto& value = owned.at("terms").at(0);
    check(value.at("access") == "default" && value.at("reads") && value.at("writes") &&
              value.at("analysis") == "included" && value.at("source") == "self",
          ("Owned default field access disagrees with native metadata: " + value.dump()).c_str());
    auto read = own;
    auto optional_default = own;
    optional_default.oper = EcsOptional;
    const auto default_slow = query_access(world, {optional_default}).at("terms").at(0);
    check(default_slow.at("reads") && default_slow.at("writes") &&
              default_slow.at("source") == "self",
          "Simple and optional default owned declarations disagree");
    read.inout = EcsIn;
    auto write = own;
    write.inout = EcsOut;
    const auto reader = query_access(world, {read});
    const auto writer = query_access(world, {write});
    check(reader.at("terms").at(0).at("reads") &&
              !reader.at("terms").at(0).at("writes").get<bool>() &&
              writer.at("terms").at(0).at("writes") &&
              !writer.at("terms").at(0).at("reads").get<bool>(),
          "Explicit native access modes were not preserved");
    auto fixed = own;
    fixed.src.id = source.id() | EcsIsEntity;
    const auto inherited_fixed = query_access(world, {fixed}).at("terms").at(0);
    check(inherited_fixed.at("source") == "traversal" &&
              inherited_fixed.at("source_id") == source.id() &&
              inherited_fixed.at("analysis") == "unresolved" &&
              !inherited_fixed.at("writes").get<bool>(),
          "Fixed-source inheritance lost its origin or claimed resolved writes");
    fixed.src.id = source.id() | EcsIsEntity | EcsSelf;
    const auto fixed_read = query_access(world, {fixed});
    check(fixed_read.at("terms").at(0).at("source_id") == source.id() &&
              !fixed_read.at("terms").at(0).at("writes").get<bool>(),
          ("Default shared field/source disagrees with native metadata: " + fixed_read.dump() +
           " expected source " + std::to_string(source.id()))
              .c_str());
    fixed.inout = EcsOut;
    const auto fixed_write = query_access(world, {fixed});
    fixed.src.id = other_source.id() | EcsIsEntity | EcsSelf;
    const auto disjoint_write = query_access(world, {fixed});
    auto same = rows({fixed_read, fixed_write, disjoint_write});
    const auto same_diagnostics = detail::native_system_access_overlaps(same);
    check(same_diagnostics.at("pairs").size() == 1 &&
              same_diagnostics.at("pairs").at(0).at("scope") == "same_entity",
          "Known distinct fixed sources were treated as shared storage");
    auto traversal = read;
    traversal.src.id |= EcsUp;
    traversal.trav = EcsIsA;
    auto filter = read;
    filter.inout = EcsInOutFilter;
    auto optional = read;
    optional.oper = EcsOptional;
    auto annotation = write;
    annotation.src.id = EcsIsEntity;
    const auto unresolved = query_access(world, {traversal});
    const auto filtered = query_access(world, {filter});
    const auto annotated = query_access(world, {annotation});
    check(unresolved.at("terms").at(0).at("analysis") == "unresolved" &&
              filtered.at("terms").at(0).at("analysis") == "ignored" &&
              annotated.at("terms").at(0).at("analysis") == "unresolved" &&
              query_access(world, {optional}).at("terms").at(0).at("operator") == "optional",
          "Traversal/filter/annotation coverage was misstated");
    auto either = own;
    either.oper = EcsOr;
    auto second = own;
    second.id = world.id<Other>();
    const auto complex = query_access(world, {either, second});
    for (const auto& term : complex.at("terms"))
        check(term.at("analysis") != "included", "OR expression was claimed as a resolved overlap");
    auto wildcard = own;
    wildcard.id = ecs_pair(id, EcsWildcard);
    const auto wildcard_access = query_access(world, {wildcard});
    check(wildcard_access.at("terms").at(0).at("analysis") != "included",
          "Wildcard declaration was claimed as resolved storage");
    const auto clipped = query_access(world, {own, second}, 1);
    check(clipped.at("truncated") && clipped.at("total_terms") == 2 &&
              clipped.at("terms").size() == 1 && query_access(world, {own}, 0).at("terms").empty(),
          "Native term copy budget was not enforced");
    auto entries =
        rows({owned, reader, unresolved, filtered, annotated, complex, wildcard_access, clipped});
    const auto diagnostics = detail::native_system_access_overlaps(entries);
    detail::validate_native_system_access(entries, &diagnostics);
    check(diagnostics.at("terms_truncated") && diagnostics.at("unresolved_terms") > 0,
          "Unresolved or omitted coverage was hidden");
    auto invalid = entries;
    invalid[0]["access"]["terms"][0]["analysis"] = "safe";
    bool rejected = false;
    try {
        detail::validate_native_system_access(invalid, &diagnostics);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Invalid access declaration was accepted");
    for (const char* key : {"first", "second", "first_term", "id", "scope", "component", "kind"}) {
        auto broken = diagnostics;
        broken["pairs"][0][key] = "invalid";
        rejected = false;
        try {
            detail::validate_native_system_access(entries, &broken);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid overlap reference was accepted");
    }
    auto big = Json::array();
    for (unsigned i = 0; i < 20; ++i)
        big.push_back(rows({owned})[0]);
    auto bounded = detail::native_system_access_overlaps(big);
    check(bounded.at("pairs").size() == detail::native_access_overlap_capacity &&
              bounded.at("truncated"),
          "Overlap observation exceeded or hid its row bound");
    detail::validate_native_system_access(big, &bounded);
    big = Json::array();
    for (unsigned i = 0; i < 130; ++i)
        big.push_back(rows({reader})[0]);
    bounded = detail::native_system_access_overlaps(big);
    check(bounded.at("pairs").empty() && bounded.at("truncated") &&
              bounded.at("comparisons") == detail::native_access_comparison_capacity,
          "Read-only comparison budget was not bounded or fabricated writes");
    detail::validate_native_system_access(big, &bounded);
    auto malformed = bounded;
    malformed["comparisons"] = 8192.;
    rejected = false;
    try {
        detail::validate_native_system_access(big, &malformed);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Floating-point diagnostic counter was accepted");
}
void runtime_observation() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    auto& world = scene.world();
    auto actor = world.entity().set<Value>({0});
    RuntimeSimulation simulation(engine.world(), scene);
    int observed = -1;
    auto writer = world.system<Value>("AccessWriter")
                      .kind(world.lookup("forge.runtime.Gameplay"))
                      .term_at(0)
                      .src()
                      .self()
                      .each([](Value& value) { ++value.value; })
                      .add<FixedSimulation>();
    auto reader = world.system<const Value>("AccessReader")
                      .kind(world.lookup("forge.runtime.Gameplay"))
                      .term_at(0)
                      .src()
                      .self()
                      .each([&](const Value& value) { observed = value.value; })
                      .add<FixedSimulation>();
    check(!simulation.system_profile().at("available").get<bool>() && actor.get<Value>().value == 0,
          "Access inspection progressed gameplay");
    engine.services().profiling(true);
    simulation.tick(1.f / 60);
    const auto profile = simulation.system_profile();
    check(profile.at("available") && observed == 1 && actor.get<Value>().value == 1,
          "Actual native order/numeric results changed under access inspection");
    detail::validate_native_system_access(profile.at("entries"), &profile.at("access_diagnostics"));
    bool found = false;
    for (const auto& pair : profile.at("access_diagnostics").at("pairs")) {
        const auto& first = profile.at("entries").at(pair.at("first").get<std::size_t>());
        const auto& second = profile.at("entries").at(pair.at("second").get<std::size_t>());
        if (first.at("id") == writer.id() && second.at("id") == reader.id())
            found = true;
    }
    check(found, "Actual native write/read declaration overlap was not observable");
    check(simulation.system_profile() == profile && actor.get<Value>().value == 1,
          "Repeated copied access inspection progressed or resampled gameplay");
    check(profile.dump().size() < 2 * 1024 * 1024,
          "Bounded access fixture exceeded runtime profile budget");
    engine.services().clear_profiles();
    check(!simulation.system_profile().at("available").get<bool>(),
          "Clear retained stale access observations");
}
void canonical_owned_motion() {
    for (unsigned stages : {1u, 4u}) {
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        auto& world = scene.world();
        RuntimeSimulation simulation(engine.world(), scene, {stages});
        world.component<ParallelMotion>("gameplay.ParallelMotion");
        auto prototype = world.prefab("AccessPrototype").set<LocalTranslation>({10, 0, 0});
        auto inherited = world.entity().is_a(prototype).set<ParallelMotion>({1});
        auto owned = world.entity().set<LocalTranslation>({0, 0, 0}).set<ParallelMotion>({1});
        ForgeSdkWorldV1 host{};
        host.world = world.c_ptr();
        host.fixed_phase = world.lookup("forge.runtime.Gameplay").id();
        host.fixed_tag = world.id<FixedSimulation>();
        forge_register_system_ParallelMotion(&host);
        engine.services().profiling(true);
        (void)simulation.system_profile();
        simulation.tick(1.f / 60);
        simulation.tick(1.f / 60);
        check(prototype.get<LocalTranslation>().x == 10 &&
                  inherited.get<LocalTranslation>().x == 10 &&
                  !ecs_owns_id(world, inherited, world.id<LocalTranslation>()) &&
                  std::abs(owned.get<LocalTranslation>().x - 2 * double(1.f / 60)) < 1e-12,
              "Canonical parallel motion modified inherited/shared translation storage");
        bool found = false;
        const auto profile = simulation.system_profile();
        for (const auto& row : profile.at("entries"))
            if (row.value("name", "").ends_with("ParallelMotionUpdate")) {
                const auto& term = row.at("access").at("terms").at(1);
                check(term.at("source") == "self" && term.at("analysis") == "included" &&
                          term.at("writes"),
                      "Canonical native query did not require owned translation");
                found = true;
            }
        check(found, "Canonical native motion was absent from the real pipeline");
    }
}
} // namespace
int main() {
    try {
        declarations();
        runtime_observation();
        canonical_owned_motion();
        std::cout << "Native access metadata, bounds and real pipeline PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
