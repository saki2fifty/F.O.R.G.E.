#include "ParallelMotion.hpp"
#include "native_system_access.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <forge/runtime.hpp>
#include <forge/transform_components.hpp>
#include <fstream>
#include <iostream>
#include <vector>
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
void worker_declarations() {
    WorldContext context(WorldRole::Runtime);
    auto& world = context.world();
    auto source = world.entity().set<Value>({13});
    ecs_term_t own{};
    own.id = world.id<Value>();
    own.src.id = EcsSelf;
    auto shared = own;
    shared.src.id = source.id() | EcsIsEntity | EcsSelf;
    shared.inout = EcsInOut;
    auto access = query_access(world, {own, shared});
    auto entries = rows({access});
    entries[0]["multi_threaded"] = true;
    auto observation = detail::native_system_worker_access(entries, 4);
    check(observation.at("writes").size() == 1 && observation.at("writes")[0].at("system") == 0 &&
              observation.at("writes")[0].at("term") == 1,
          "Fixed shared write was omitted or owned-row write was flagged");
    detail::validate_native_system_access(entries, nullptr);
    detail::validate_native_system_worker_access(entries, observation, 4);
    for (const char* key :
         {"workers", "system_count", "copied_terms", "total_terms", "unresolved_terms", "capacity",
          "total_writes", "truncated", "terms_truncated"}) {
        auto invalid = observation;
        invalid[key] = "bad";
        bool rejected = false;
        try {
            detail::validate_native_system_worker_access(entries, invalid, 4);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Malformed worker coverage was accepted");
    }
    auto invalid = observation;
    invalid["writes"][0]["term"] = 0;
    bool rejected = false;
    try {
        detail::validate_native_system_worker_access(entries, invalid, 4);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Worker warning was redirected to an owned field");
    check(detail::native_system_worker_access(entries, 1).at("writes").empty(),
          "Single-stage execution was labeled shared worker writing");
    entries[0]["multi_threaded"] = false;
    check(detail::native_system_worker_access(entries, 4).at("writes").empty(),
          "Serial system was labeled shared worker writing");
    entries[0]["multi_threaded"] = true;
    shared.inout = EcsInOutDefault;
    entries[0]["access"] = query_access(world, {own, shared});
    check(detail::native_system_worker_access(entries, 4).at("writes").empty(),
          "Default shared read was labeled a shared write");
    shared.inout = EcsInOut;
    shared.src.id |= EcsUp;
    entries[0]["access"] = query_access(world, {own, shared}, 1);
    observation = detail::native_system_worker_access(entries, 4);
    check(observation.at("terms_truncated") && observation.at("total_terms") == 2 &&
              observation.at("writes").empty(),
          "Omitted worker fields lost partial coverage");
    entries[0]["access"] = query_access(world, {own, shared});
    observation = detail::native_system_worker_access(entries, 4);
    check(observation.at("unresolved_terms") == 1 && observation.at("writes").empty(),
          "Traversal write was treated as resolved fixed storage");
    auto big = Json::array();
    entries[0]["access"] = access;
    for (unsigned i = 0; i < 256; ++i)
        big.push_back(entries[0]);
    observation = detail::native_system_worker_access(big, 4);
    detail::validate_native_system_access(big, nullptr);
    detail::validate_native_system_worker_access(big, observation, 4);
    check(observation.at("writes").size() == 128 && observation.at("total_writes") == 256 &&
              observation.at("truncated"),
          "Worker write bound was exceeded or hidden");
}
struct WorkerPrivate {
    int value = 0;
};
struct WorkerShared {
    int value = 13;
};
Json actual_worker_sources(unsigned stages, bool writable, bool matches_this) {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    auto& world = scene.world();
    std::array<std::atomic<std::uintptr_t>, 16> addresses{};
    std::array<std::atomic<unsigned>, 16> calls{};
    RuntimeSimulation simulation(engine.world(), scene, {stages});
    auto shared = world.entity().set<WorkerShared>({13});
    if (!matches_this)
        shared.set<WorkerPrivate>({0});
    std::vector<flecs::entity> actors;
    for (unsigned i = 0; i < 256; ++i)
        actors.push_back(world.entity().set<WorkerPrivate>({0}));
    auto builder = world.system<WorkerPrivate, WorkerShared>("WorkerSharedDeclaration");
    builder.kind(world.lookup("forge.runtime.Gameplay"))
        .term_at(0)
        .src()
        .self()
        .term_at(1)
        .src(shared)
        .self()
        .inout(writable ? flecs::InOut : flecs::In)
        .multi_threaded();
    if (!matches_this)
        builder.term_at(0).src(shared).self();
    auto system =
        builder.each([&](flecs::iter& it, size_t, WorkerPrivate& value, WorkerShared& source) {
            const auto stage = unsigned(it.world().get_stage_id());
            if (stage >= addresses.size())
                std::terminate();
            addresses[stage].store(reinterpret_cast<std::uintptr_t>(&source));
            calls[stage].fetch_add(1);
            // Shared write is declared, but this test only reads the shared field.
            if (matches_this)
                value.value += source.value;
        });
    system.add<FixedSimulation>();
    engine.services().profiling(true);
    (void)simulation.system_profile();
    simulation.tick(1.f / 60);
    const auto profile = simulation.system_profile();
    const auto& entries = profile.at("entries");
    const auto& observation = profile.at("worker_access");
    detail::validate_native_system_worker_access(entries, observation, stages);
    bool found = false;
    for (const auto& row : entries)
        if (row.at("kind") == "system" && row.at("id") == system.id()) {
            found = true;
            check(row.at("access").at("matches_this") == matches_this,
                  "Native This-source configuration was lost");
        }
    check(found, "Actual worker declaration was absent from the copied plan");
    check(observation.at("writes").size() == std::size_t(stages > 1 && writable && matches_this),
          "Actual worker/shared-write eligibility disagrees with declarations");
    const auto expected = reinterpret_cast<std::uintptr_t>(&shared.get<WorkerShared>());
    unsigned total = 0;
    for (unsigned i = 0; i < stages; ++i) {
        const auto count = calls[i].load();
        check(count == (matches_this ? 256 / stages
                        : i == 0     ? 1u
                                     : 0u),
              "Native row partition or no-This dispatch changed");
        check(!count || addresses[i].load() == expected,
              "Fixed field was not the same storage across actual worker stages");
        total += count;
    }
    check(total == (matches_this ? 256u : 1u) && shared.get<WorkerShared>().value == 13,
          "Observation changed shared state or native callback count");
    for (auto actor : actors)
        check(actor.get<WorkerPrivate>().value == (matches_this ? 13 : 0),
              "Private entity rows changed or repeated");
    return profile;
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
int main(int argc, char** argv) {
    try {
        check(argc == 1 || argc == 2, "Expected optional native worker access capture path");
        declarations();
        worker_declarations();
        Json captured;
        for (unsigned stages : {1u, 4u}) {
            actual_worker_sources(stages, false, true);
            auto profile = actual_worker_sources(stages, true, true);
            if (stages == 4)
                captured = std::move(profile);
            actual_worker_sources(stages, true, false);
        }
        runtime_observation();
        canonical_owned_motion();
        if (argc == 2) {
            const auto path = std::filesystem::u8path(argv[1]);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path);
            stream << captured.dump(2) << '\n';
            check(bool(stream), "Could not write actual native worker access capture");
        }
        std::cout << "Native access metadata, bounds and real pipeline PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
