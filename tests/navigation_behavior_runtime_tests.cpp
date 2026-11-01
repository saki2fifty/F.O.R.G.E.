#include "navigation_behavior_authoring.hpp"
#include "navigation_behavior_resource.hpp"
#include "state_graph_layout.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <iostream>
#include <set>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<NavigationRuntime> navigation;
    explicit World(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false,
                 {runtime_resources_module(root), navigation_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene),
          navigation(std::static_pointer_cast<NavigationRuntime>(engine.services().navigation())) {}
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            engine.services().resources()->synchronize();
            if (navigation->prepare_assets())
                break;
            check(std::chrono::steady_clock::now() < deadline, "Behavior readiness timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
    void tick() { simulation.tick(1.f / 60); }
    Double3 position(const char* name = "agent") {
        engine.world().evaluate_world_transforms();
        return scene.entity(name).get<WorldTransform>().affine.point({0, 0, 0});
    }
};
void republish(const std::shared_ptr<ProjectLease>& lease, NavigationBehaviorDocument& doc) {
    AssetImportService imports(lease, navigation_behavior_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(doc.locator(), {}, doc.source().asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_navigation_behavior_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(10s), "Behavior publication timeout");
    const auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes.front().published,
          outcomes.empty() ? "Missing behavior receipt" : outcomes.front().diagnostic);
}
AssetId publish(const std::shared_ptr<ProjectLease>& lease, const char* path,
                const std::function<NavigationBehaviorSource(AssetId)>& initialize) {
    auto doc = NavigationBehaviorDocument::create(lease, path, initialize);
    const auto asset = doc->source().asset();
    republish(lease, *doc);
    return asset;
}
NavigationBehaviorSource patrol(AssetId id) {
    auto source = NavigationBehaviorSource::create(id);
    auto& d = source.document;
    d["next_id"] = 9;
    d["states"] = Json::array();
    for (unsigned i = 1; i <= 4; ++i) {
        const bool move = i % 2 != 0;
        const Json action =
            move ? Json{{"kind", "move_to_point"}, {"point", {i == 1 ? 4 : -4, 0, 0}}}
                 : Json{{"kind", "wait"}};
        const Json condition =
            move ? Json{{"kind", "arrived"}} : Json{{"kind", "elapsed"}, {"seconds", .2}};
        d["states"].push_back(
            {{"id", i},
             {"name", "State " + std::to_string(i)},
             {"action", action},
             {"transitions",
              Json::array({{{"id", i + 4}, {"to", i % 4 + 1}, {"condition", condition}}})}});
    }
    return source;
}
NavigationBehaviorSource chase(AssetId id) {
    auto source = NavigationBehaviorSource::create(id);
    source.document["next_id"] = 6;
    source.document["states"][0]["transitions"] = Json::array(
        {{{"id", 3}, {"to", 2}, {"condition", {{"kind", "target_near"}, {"distance", 3}}}}});
    source.document["states"].push_back(
        {{"id", 2},
         {"name", "Chase"},
         {"action", {{"kind", "follow_target"}}},
         {"transitions",
          Json::array({{{"id", 4}, {"to", 1}, {"condition", {{"kind", "target_missing"}}}},
                       {{"id", 5},
                        {"to", 1},
                        {"condition", {{"kind", "target_far"}, {"distance", 5}}}}})}});
    return source;
}
Json initial() {
    Json rows = Json::array();
    for (const char* name : {"floor", "agent", "target", "anchor"})
        rows.push_back({{"id", name},
                        {"name", name},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}});
    return {{"version", 1}, {"entities", rows}};
}
void exercise(const std::filesystem::path& parent, const std::filesystem::path& worker, bool xy) {
    const auto root = parent / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    atomic_write(root / "forge.assets.json",
                 Json{{"version", 1}, {"assets", Json::array()}}.dump());
    EngineContext authoring;
    Scene source(authoring.world());
    source.restore_snapshot(initial());
    auto floor = source.entity("floor");
    if (xy) {
        floor.set<NavigationRegion2D>({true, false, 20, 20});
    } else {
        floor.set<Primitive>({3});
        floor.set<LocalScale>({20, 1, 20});
        floor.set<NavigationSurface>({true});
    }
    source.entity("agent").set<LocalTranslation>({-4, xy ? 0.f : .1f, xy ? 37.125f : 0.f});
    source.entity("target").set<LocalTranslation>({2, xy ? 0.f : .1f, xy ? -19.f : 0.f});
    NavigationSettings settings;
    settings.plane = xy ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
    const auto nav = prepare_navigation(root, source.effective_document(), settings, worker)
                         .publish(source.effective_document());
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto patrol_id = publish(lease, "Assets/Patrol.behavior.json", patrol);
    const auto chase_id = publish(lease, "Assets/Chase.behavior.json", chase);
    source.entity("agent").set<NavigationAgent>({{nav.id}, true, false, 4, .05f, 0, 0, 0});
    source.entity("agent").set<NavigationBehavior>({{patrol_id}, {}, {}, true});
    const auto snapshot = source.snapshot();
    World world(root), other(root);
    world.scene.restore_snapshot(snapshot);
    other.scene.restore_snapshot(snapshot);
    check(world.navigation->checkpoint().is_null(),
          "Pending behavior resources produced a partial recovery checkpoint");
    world.ready();
    other.ready();
    const auto actor = world.scene.entity("agent");
    check(actor.owns<NavigationBehaviorCursor>() &&
              actor.get<NavigationBehaviorCursor>().state == 0,
          "Readiness advanced the fixed behavior clock");
    const auto frozen_other = other.position();
    std::set<std::uint32_t> states;
    double minimum = -4, maximum = -4;
    bool arrival_recovered = false;
    for (unsigned tick = 0; tick < 960; ++tick) {
        world.tick();
        states.insert(actor.get<NavigationBehaviorCursor>().state);
        const auto p = world.position();
        minimum = std::min(minimum, p[0]);
        maximum = std::max(maximum, p[0]);
        if (xy)
            check(p[2] == 37.125, "XY behavior changed visual depth");
        const auto observation =
            world.navigation->agent_state(*world.engine.world().reference(actor));
        const auto cursor = actor.get<NavigationBehaviorCursor>();
        const bool arrived =
            observation && observation->phase == NavigationAgentPhase::Arrived && cursor.state == 1;
        const bool entered_wait =
            cursor.state == 2 && cursor.elapsed == 0 && cursor.last_transition == 5;
        if (!arrival_recovered && (arrived || entered_wait)) {
            const auto checkpoint = world.navigation->checkpoint();
            check(checkpoint.at("version") == 3,
                  "Behavior recovery omitted its explicit extension");
            if (!xy) {
                const auto saved_scene = world.scene.snapshot();
                const auto rejects_recovery = [&](const std::function<void(Json&)>& change) {
                    auto bad = checkpoint;
                    change(bad);
                    World candidate(root);
                    candidate.scene.restore_snapshot(saved_scene);
                    bool rejected = false;
                    try {
                        candidate.navigation->restore(bad);
                    } catch (const std::exception&) {
                        rejected = true;
                    }
                    check(rejected, "Malformed behavior recovery was admitted");
                };
                rejects_recovery([](Json& value) {
                    value["version"] = 2;
                    value.erase("behaviors");
                });
                rejects_recovery([](Json& value) { value["behaviors"] = Json::array(); });
                rejects_recovery(
                    [](Json& value) { value["behaviors"][0]["cursor"]["state"] = 999; });
                rejects_recovery(
                    [](Json& value) { value["behaviors"][0]["cursor"]["state"] = 1.0; });
                rejects_recovery(
                    [](Json& value) { value["behaviors"][0]["cursor"]["transitions"] = -1; });
                rejects_recovery([](Json& value) {
                    value["behaviors"][0]["cooked_digest"] = std::string(64, '0');
                });
                rejects_recovery(
                    [](Json& value) { value["behaviors"][0]["revision"] = std::string(64, '0'); });
                rejects_recovery([](Json& value) {
                    value["behaviors"][0]["entity"]["scene"] = AssetId::generate();
                });
                rejects_recovery([](Json& value) { value["behaviors"][0]["owned"] = false; });
                rejects_recovery([](Json& value) {
                    value["behaviors"][0]["progress_present"] = false;
                    value["behaviors"][0]["cursor"] = nullptr;
                });
            }
            World recovered(root);
            recovered.scene.restore_snapshot(world.scene.snapshot());
            recovered.navigation->restore(checkpoint);
            for (unsigned i = 0; i < 120; ++i) {
                world.tick();
                recovered.tick();
                const auto expected = actor.get<NavigationBehaviorCursor>();
                const auto actual = recovered.scene.entity("agent").get<NavigationBehaviorCursor>();
                check(expected == actual,
                      "Behavior recovery lost current state/timer at an arrival boundary");
                const auto a = world.position(), b = recovered.position();
                check(std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) < 1e-5,
                      "Behavior recovery changed actual movement continuation");
                states.insert(expected.state);
                minimum = std::min(minimum, a[0]);
                maximum = std::max(maximum, a[0]);
            }
            tick += 120;
            arrival_recovered = true;
        }
    }
    check(arrival_recovered && states == std::set<std::uint32_t>{1, 2, 3, 4} && maximum > 3.8 &&
              minimum < -3.8 && actor.get<NavigationBehaviorCursor>().transitions >= 6,
          "Actual agent did not patrol, arrive, wait and return");
    check(other.position() == frozen_other &&
              other.scene.entity("agent").get<NavigationBehaviorCursor>().state == 0,
          "Behavior progress or transforms leaked between Worlds");
    auto config = actor.get<NavigationBehavior>();
    config.enabled = false;
    const auto ref = *world.engine.world().reference(actor);
    check(world.navigation->tune(ref, "forge.navigation_behavior", "enabled", false) == false,
          "Existing live navigation owner did not disable behavior");
    for (const auto* property : {"target", "anchor", "graph"}) {
        bool rejected = false;
        try {
            world.navigation->tune(ref, "forge.navigation_behavior", property, nullptr);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Live behavior tuning allowed identity/reference replacement");
    }
    const auto held = actor.get<NavigationBehaviorCursor>();
    const auto held_position = world.position();
    for (unsigned i = 0; i < 60; ++i)
        world.tick();
    check(actor.get<NavigationBehaviorCursor>() == held && world.position() == held_position &&
              !actor.get<NavigationAgent>().has_destination,
          "Disabled behavior advanced progress/movement or retained its issued intent");
    auto native = actor.get<NavigationAgent>();
    native.has_destination = true;
    native.destination_x = 0;
    native.destination_y = 0;
    native.destination_z = xy ? 37.125 : 0;
    actor.set<NavigationAgent>(native);
    world.tick();
    check(actor.get<NavigationAgent>().has_destination,
          "Disabled behavior overwrote a later native destination");
    config.enabled = true;
    check(world.navigation->tune(ref, "forge.navigation_behavior", "enabled", true) == true,
          "Existing live navigation owner did not reenable behavior");
    world.tick();
    check(actor.get<NavigationBehaviorCursor>().elapsed != held.elapsed ||
              actor.get<NavigationBehaviorCursor>().transitions != held.transitions,
          "Reenabled behavior did not resume its fixed clock");
    actor.remove<NavigationBehavior>();
    world.tick();
    check(!actor.has<NavigationBehaviorCursor>() && !actor.get<NavigationAgent>().has_destination,
          "Removing behavior retained progress or its issued movement");
    actor.set<LocalTranslation>({0, xy ? 0.f : .1f, xy ? 37.125f : 0.f});
    config = {{chase_id}, *world.engine.world().reference(world.scene.entity("target")), {}, true};
    actor.set<NavigationBehavior>(config);
    world.ready();
    world.tick();
    check(actor.get<NavigationBehaviorCursor>().state == 2 &&
              actor.get<NavigationAgent>().destination_x == 2,
          "Nearby target did not trigger chase on the current boundary");
    world.scene.entity("target").set<LocalTranslation>({3, xy ? 0.f : .1f, xy ? 999.f : 0.f});
    world.tick();
    check(actor.get<NavigationAgent>().destination_x == 3 && (!xy || world.position()[2] == 37.125),
          "Follow target did not use current pose or ignored XY depth policy");
    world.scene.entity("target").destruct();
    const auto stopped = world.position();
    world.tick();
    check(actor.get<NavigationBehaviorCursor>().state == 1 &&
              !actor.get<NavigationAgent>().has_destination && world.position() == stopped,
          "Missing target did not transition to wait before movement");
    const auto presentation = world.simulation.presentation(1);
    bool observed = false;
    for (const auto& row : presentation.at("entities")) {
        if (row.contains("navigation_behavior_debug"))
            observed |= row.at("navigation_behavior_debug").at("phase") == "Running";
        check(!row.at("components").contains("forge.runtime.navigation_behavior_cursor"),
              "Runtime cursor became authored Scene data");
    }
    check(observed, "Behavior observation did not reach actual presentation");
    auto prefab = world.engine.world().world().prefab().set<NavigationBehaviorCursor>({1, 0, 2, 0});
    auto instance = world.engine.world().world().entity().is_a(prefab);
    check(!instance.has<NavigationBehaviorCursor>(),
          "Runtime behavior progress inherited from a template");
    const auto anchor = world.scene.entity("anchor");
    const float half = std::sqrt(.5f);
    anchor.set<LocalTranslation>({1, xy ? 2.f : .1f, xy ? 999.f : 2.f});
    anchor.set<LocalRotation>(xy ? LocalRotation{0, 0, half, half}
                                 : LocalRotation{0, half, 0, half});
    anchor.set<LocalScale>({.5f, .5f, .5f});
    config = {{patrol_id}, {}, *world.engine.world().reference(anchor), true};
    actor.set<NavigationBehavior>(config);
    world.ready();
    world.tick();
    const auto anchored = actor.get<NavigationAgent>();
    check(std::abs(anchored.destination_x - 1) < 1e-5 &&
              (xy ? std::abs(anchored.destination_y - 4) < 1e-5 && anchored.destination_z == 37.125
                  : std::abs(anchored.destination_z) < 1e-5),
          "Point action ignored Anchor rotation/scale or XY depth policy");
    const auto cursor_before_error = actor.get<NavigationBehaviorCursor>();
    config.anchor.entity = EntityId::generate();
    actor.set<NavigationBehavior>(config);
    const auto held_anchor_position = world.position();
    const auto neighbor_id = authoring_command(world.scene, "entity.create", {{"name", "Neighbor"}})
                                 .at("selected")
                                 .get<std::string>();
    const auto neighbor = world.scene.entity(neighbor_id);
    neighbor.set<LocalTranslation>({-4, xy ? 0.f : .1f, xy ? -23.f : 0.f});
    neighbor.set<NavigationAgent>({{nav.id}, true, false, 2, .05f, 0, 0, 0});
    neighbor.set<NavigationBehavior>({{patrol_id}, {}, {}, true});
    const auto neighbor_start = world.position(neighbor_id.c_str());
    for (unsigned i = 0; i < 30; ++i)
        world.tick();
    check(actor.get<NavigationBehaviorCursor>() == cursor_before_error &&
              world.position() == held_anchor_position &&
              world.navigation->debug_behavior(actor).at("phase") == "Failed" &&
              world.position(neighbor_id.c_str())[0] > neighbor_start[0] + .5 &&
              neighbor.get<NavigationBehaviorCursor>() != actor.get<NavigationBehaviorCursor>(),
          "Missing Anchor advanced its controller or stopped/shared progress with a healthy "
          "neighbor");
    config.anchor = *world.engine.world().reference(anchor);
    actor.set<NavigationBehavior>(config);
    world.tick();
    check(world.navigation->debug_behavior(actor).at("phase") == "Running" &&
              actor.get<NavigationBehaviorCursor>() != cursor_before_error,
          "Repairing Anchor did not resume held progress");

    if (!xy) {
        auto resources = world.engine.services().resources();
        const auto token = resources->request(RuntimeResourceKind::NavigationBehavior, patrol_id);
        const auto duplicate_token =
            resources->request(RuntimeResourceKind::NavigationBehavior, patrol_id);
        resources->synchronize();
        const auto retained = resources->navigation_behavior(token);
        check(retained && &retained.get() == &resources->navigation_behavior(duplicate_token).get(),
              "Matching subscriptions created separate behavior graphs");
        check(resources->release(duplicate_token) && resources->navigation_behavior(token),
              "Releasing one subscription retired another subscriber's graph");
        const auto wait_revision = [&](const std::string& revision, bool fail = false) {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            for (;;) {
                resources->synchronize();
                (void)world.navigation->prepare_assets();
                const auto status = resources->inspect(token);
                if (status.requested_revision == revision &&
                    (fail ? status.state == "failed"
                          : status.state == "ready" && status.retained_revision == revision))
                    break;
                check(std::chrono::steady_clock::now() < deadline,
                      "Behavior resource refresh timed out: " + status.diagnostic);
                std::this_thread::sleep_for(1ms);
            }
        };
        NavigationBehaviorDocument updated(lease, "Assets/Patrol.behavior.json");
        updated.edit(updated.revision(), "Layout", [](Json& d) {
            asset_detail::set_state_graph_position(d, 1, {60, 80},
                                                   navigation_behavior_layout_format);
        });
        updated.save();
        republish(lease, updated);
        const auto current = actor.get<NavigationBehaviorCursor>();
        const auto layout_revision = AssetCatalog::open_project(root)
                                         .records()
                                         .at(patrol_id)
                                         .metadata.at("forge.import")
                                         .at("key")
                                         .get<std::string>();
        resources->refresh();
        wait_revision(layout_revision);
        check(actor.get<NavigationBehaviorCursor>() == current,
              "Layout-only publication reset live behavior progress");
        updated.edit(updated.revision(), "Point",
                     [](Json& d) { d["states"][0]["action"]["point"][0] = 5; });
        updated.save();
        republish(lease, updated);
        const auto good_catalog = AssetCatalog::open_project(root);
        const auto changed_revision = good_catalog.records()
                                          .at(patrol_id)
                                          .metadata.at("forge.import")
                                          .at("key")
                                          .get<std::string>();
        resources->refresh();
        wait_revision(changed_revision);
        check(actor.get<NavigationBehaviorCursor>() == NavigationBehaviorCursor{} &&
                  retained->state(1).point[0] == 4,
              "Changed cooked graph failed to reset progress or mutated its predecessor lease");
        world.tick();
        auto bad_catalog = good_catalog;
        auto bad_record = bad_catalog.records().at(patrol_id);
        bad_record.metadata["forge.import"]["key"] = std::string(64, '0');
        bad_record.metadata["forge.import"]["generation"] =
            bad_record.metadata["forge.import"]["generation"].get<std::uint64_t>() + 1;
        bad_catalog.replace(bad_record);
        atomic_write(root / "forge.assets.json", bad_catalog.document().dump());
        resources->refresh();
        wait_revision(std::string(64, '0'), true);
        world.tick();
        check(world.navigation->debug_behavior(actor).at("retained_last_good") == true &&
                  actor.get<NavigationAgent>().has_destination &&
                  std::abs(actor.get<NavigationAgent>().destination_x - 1) < 1e-5,
              "Failed graph refresh lost its complete predecessor or warning");
        check(world.navigation->checkpoint().is_null(),
              "Failed graph selection produced an unrestorable last-good recovery checkpoint");
        // Repair through publication so source generations remain monotonic.
        // Replacing the catalog with an older generation is deliberately stale.
        republish(lease, updated);
        resources->refresh();
        wait_revision(changed_revision);
        check(!world.navigation->checkpoint().is_null(),
              "Repaired graph selection did not restore optional recovery");
        check(resources->release(token), "Behavior subscription release failed");
        std::vector<AssetId> released_graphs;
        for (unsigned i = 0; i < 9; ++i) {
            const auto name = "Assets/Released-" + std::to_string(i) + ".behavior.json";
            released_graphs.push_back(
                publish(lease, name.c_str(), NavigationBehaviorSource::create));
        }
        World lifecycle(root);
        const auto slots = lifecycle.engine.services().resources();
        for (const auto id : released_graphs) {
            const auto subscription = slots->request(RuntimeResourceKind::NavigationBehavior, id);
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            for (;;) {
                slots->synchronize();
                const auto status = slots->inspect(subscription);
                check(status.state != "failed", status.diagnostic);
                if (status.state == "ready")
                    break;
                check(std::chrono::steady_clock::now() < deadline,
                      "Released graph slot was not reclaimed");
                std::this_thread::sleep_for(1ms);
            }
            check(slots->navigation_behavior(subscription) && slots->release(subscription),
                  "Graph subscription failed or could not release");
        }
        World bounded(root);
        bounded.scene.restore_snapshot(snapshot);
        std::vector<std::string> extra;
        for (unsigned i = 0; i < 63; ++i) {
            const auto id =
                authoring_command(bounded.scene, "entity.create", {{"name", "Disabled controller"}})
                    .at("selected")
                    .get<std::string>();
            bounded.scene.entity(id).set<NavigationBehavior>({{patrol_id}, {}, {}, false});
            extra.push_back(id);
        }
        bounded.ready();
        bounded.tick();
        const auto too_many =
            authoring_command(bounded.scene, "entity.create", {{"name", "Excess controller"}})
                .at("selected")
                .get<std::string>();
        bounded.scene.entity(too_many).set<NavigationBehavior>({{patrol_id}, {}, {}, false});
        bool rejected = false;
        try {
            (void)bounded.navigation->prepare_assets();
        } catch (const std::exception&) {
            rejected = true;
        }
        const auto position = bounded.position();
        bounded.tick();
        check(rejected && bounded.position() == position &&
                  bounded.navigation->debug_behavior(bounded.scene.entity("agent")).at("phase") ==
                      "Failed",
              "Controller capacity did not reject/stop the whole navigation step");
        bounded.scene.entity(too_many).remove<NavigationBehavior>();
        for (unsigned i = 0; i < 7; ++i)
            bounded.scene.entity(extra[i]).set<NavigationBehavior>(
                {{AssetId::generate()}, {}, {}, false});
        bounded.ready();
        bounded.tick();
        bounded.scene.entity(extra[7]).set<NavigationBehavior>(
            {{AssetId::generate()}, {}, {}, false});
        rejected = false;
        try {
            (void)bounded.navigation->prepare_assets();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Ninth configured graph bypassed the Scene asset bound");

        const auto prototype =
            authoring_command(source, "entity.create", {{"name", "Behavior prototype"}})
                .at("selected")
                .get<std::string>();
        for (const char* child : {"agent", "target", "anchor"})
            source.reparent_entity(source.canonical_id(child), prototype, ReparentMode::KeepLocal);
        source.entity("target").set<LocalTranslation>({-2, .1f, 0});
        source.entity("agent").set<NavigationBehavior>(
            {{chase_id}, source.reference("target"), source.reference("anchor"), true});
        const auto prefab = create_prefab_source(source, prototype);
        source.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto first = instantiate_prefab(source, prefab.asset());
        const auto second = instantiate_prefab(source, prefab.asset());
        source.entity(first).set<LocalTranslation>({0, 0, -3});
        source.entity(second).set<LocalTranslation>({0, 0, 3});
        const auto member = [&](const char* name) {
            for (const auto& row : prefab.source.at("members"))
                if (row.at("name") == name)
                    return row.at("id").get<std::string>();
            throw std::runtime_error("Missing behavior prefab member");
        };
        const auto mapped = [&](const std::string& root_id, const char* name) {
            const auto document = source.document();
            for (const auto& row : document.at("entities"))
                if (row.at("id") == root_id)
                    return row.at("prefab_instance")
                        .at("members")
                        .at(member(name))
                        .get<std::string>();
            throw std::runtime_error("Missing behavior prefab instance");
        };
        const auto first_actor = mapped(first, "agent"), second_actor = mapped(second, "agent");
        const auto first_target = mapped(first, "target"), second_target = mapped(second, "target");
        const auto first_anchor = mapped(first, "anchor");
        const auto first_values = source.entity(first_actor).get<NavigationBehavior>();
        check(first_values.target == source.reference(first_target) &&
                  first_values.anchor == source.reference(first_anchor) &&
                  source.entity(second_actor).get<NavigationBehavior>().target ==
                      source.reference(second_target),
              "Behavior prefab internal Target/Anchor references did not remap per instance");
        World realized(root);
        realized.scene.restore_snapshot(source.snapshot());
        realized.ready();
        for (unsigned i = 0; i < 20; ++i)
            realized.tick();
        const auto instance_actor = realized.scene.entity(first_actor);
        check(instance_actor.get<NavigationBehaviorCursor>().state == 2 &&
                  realized.scene.entity(second_actor).get<NavigationBehaviorCursor>().state == 2,
              "Behavior prefab instances did not run their remapped target actions");
        auto native_config = instance_actor.get<NavigationBehavior>();
        native_config.enabled = false;
        native_config.target = realized.scene.reference(second_target);
        instance_actor.set<NavigationBehavior>(native_config);
        realized.tick();
        const auto held_cursor = instance_actor.get<NavigationBehaviorCursor>();
        const bool owned = instance_actor.owns<NavigationBehavior>();
        const auto checkpoint = realized.navigation->checkpoint();
        World restored(root);
        restored.scene.restore_snapshot(realized.scene.snapshot());
        restored.navigation->restore(checkpoint);
        const auto restored_actor = restored.scene.entity(first_actor);
        check(restored_actor.get<NavigationBehavior>() == native_config &&
                  restored_actor.owns<NavigationBehavior>() == owned &&
                  restored_actor.get<NavigationBehaviorCursor>() == held_cursor &&
                  restored.scene.entity(second_actor).get<NavigationBehavior>().target ==
                      restored.scene.reference(second_target),
              "Cold prefab recovery lost native behavior config, progress or per-instance "
              "reference scope");
        for (unsigned i = 0; i < 30; ++i)
            restored.tick();
        check(restored_actor.get<NavigationBehaviorCursor>() == held_cursor,
              "Recovered disabled prefab behavior advanced its held cursor");
    }
    std::cout << (xy ? "XY" : "3D")
              << " actual patrol/wait/chase/World/disable/removal/observation PASS\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need behavior runtime root and navigation worker");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto worker = std::filesystem::absolute(argv[2]);
        exercise(root, worker, false);
        exercise(root, worker, true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
