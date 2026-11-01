#include "navigation_behavior_authoring.hpp"
#include "runtime_package.hpp"
#include "state_graph_layout.hpp"
#include "transform_animation_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
constexpr const char* scenes[]{"Scenes/131-patrol-behavior.scene.json",
                               "Scenes/132-chase-behavior-xy.scene.json"};
constexpr const char* graphs[]{"Assets/Navigation/Patrol.behavior.json",
                               "Assets/Navigation/Chase.behavior.json"};
constexpr const char* target_clip = "Assets/Navigation/ChaseTarget.animation.json";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
template <class Document, class Initialize, class Prepare>
AssetId publish(const std::shared_ptr<ProjectLease>& lease, const std::filesystem::path& path,
                Initialize initialize, std::shared_ptr<const AssetImporterRegistry> registry,
                Prepare prepare) {
    auto doc = std::filesystem::exists(lease->root() / path)
                   ? std::make_unique<Document>(lease, path)
                   : Document::create(lease, path, initialize);
    AssetImportService imports(lease, std::move(registry), {"windows", "d3d12", "desktop"});
    const auto id = doc->source().asset();
    imports.submit(
        imports.prepare(path, {}, id),
        [prepare](auto& c, const auto& p, const auto&) { prepare(c, p); },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(15s), "Gallery source publication timed out");
    const auto receipts = imports.poll();
    check(receipts.size() == 1 && receipts.front().published,
          receipts.empty() ? "Missing Gallery publication" : receipts.front().diagnostic);
    return id;
}
NavigationBehaviorSource patrol(AssetId id) {
    auto result = NavigationBehaviorSource::create(id);
    auto& d = result.document;
    for (unsigned i = 0; i < 3; ++i)
        add_navigation_behavior_state(d);
    const char* names[]{"Go right", "Pause right", "Go left", "Pause left"};
    for (unsigned i = 0; i < 4; ++i) {
        auto& s = d["states"][i];
        s["name"] = names[i];
        s["action"] = i % 2 ? Json{{"kind", "wait"}}
                            : Json{{"kind", "move_to_point"}, {"point", {i == 0 ? 4 : -4, 0, 0}}};
        connect_navigation_behavior_states(d, i + 1, (i + 1) % 4 + 1);
        s["transitions"][0]["condition"] =
            i % 2 ? Json{{"kind", "elapsed"}, {"seconds", .4}} : Json{{"kind", "arrived"}};
        asset_detail::set_state_graph_position(d, i + 1, {double(i % 2) * 330, double(i / 2) * 180},
                                               navigation_behavior_layout_format);
    }
    return result;
}
NavigationBehaviorSource chase(AssetId id) {
    auto result = NavigationBehaviorSource::create(id);
    auto& d = result.document;
    add_navigation_behavior_state(d);
    d["states"][0]["name"] = "Watch target";
    d["states"][1]["name"] = "Follow target";
    d["states"][1]["action"] = {{"kind", "follow_target"}};
    connect_navigation_behavior_states(d, 1, 2);
    d["states"][0]["transitions"][0]["condition"] = {{"kind", "target_near"}, {"distance", 4}};
    connect_navigation_behavior_states(d, 2, 1);
    connect_navigation_behavior_states(d, 2, 1);
    d["states"][1]["transitions"][0]["condition"] = {{"kind", "target_missing"}};
    d["states"][1]["transitions"][1]["condition"] = {{"kind", "target_far"}, {"distance", 5}};
    return result;
}
TransformAnimationSource moving_target(AssetId id) {
    auto result = TransformAnimationSource::create(id);
    auto& d = result.document;
    d["version"] = 2;
    d["channels"] = {"forge.local_translation"};
    d["duration_us"] = 8000000;
    d["keys"] = Json::array();
    const std::array<Double3, 5> points{{{2, 0, 0}, {8, 4, 0}, {-8, -4, 0}, {-2, 3, 0}, {2, 0, 0}}};
    for (unsigned i = 0; i < points.size(); ++i)
        d["keys"].push_back({{"id", AssetId::generate()},
                             {"time_us", i * 2000000},
                             {"translation", points[i]},
                             {"rotation", {0, 0, 0, 1}},
                             {"scale", {1, 1, 1}},
                             {"interpolation", "linear"}});
    return result;
}
flecs::entity create(Scene& scene, const char* name) {
    const auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                         .at("selected")
                                         .get<std::string>());
    entity.set<Primitive>({no_primitive});
    return entity;
}
void body(Scene& scene, flecs::entity parent, Tint tint, bool xy) {
    const auto mesh = create(scene, "Agent visual");
    mesh.set<Primitive>({0});
    mesh.set<Tint>(tint);
    mesh.set<LocalScale>({.7f, .7f, .7f});
    scene.reparent_entity(mesh.get<PersistentEntityId>().value.str(),
                          parent.get<PersistentEntityId>().value.str(), ReparentMode::KeepLocal);
    mesh.set<LocalTranslation>({0, xy ? 0.f : .4f, 0});
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    auto lease = std::make_shared<ProjectLease>(root);
    std::array<AssetId, 2> graph_ids{
        publish<NavigationBehaviorDocument>(lease, graphs[0], patrol,
                                            navigation_behavior_import_registry(),
                                            prepare_navigation_behavior_publication),
        publish<NavigationBehaviorDocument>(lease, graphs[1], chase,
                                            navigation_behavior_import_registry(),
                                            prepare_navigation_behavior_publication)};
    const auto clip = publish<TransformAnimationDocument>(lease, target_clip, moving_target,
                                                          transform_animation_import_registry(),
                                                          prepare_transform_animation_publication);
    for (unsigned index = 0; index < 2; ++index) {
        const bool xy = index != 0;
        EngineContext author;
        Scene scene(author.world());
        if (std::filesystem::exists(root / scenes[index]))
            scene.load(root / scenes[index]);
        else {
            const auto floor = create(scene, xy ? "Walkable XY region" : "Patrol floor");
            if (xy) {
                floor.set<NavigationRegion2D>({true, false, 20, 14});
                const auto art = create(scene, "XY floor visual");
                art.set<Primitive>({3});
                art.set<Tint>({.05f, .09f, .14f});
                art.set<LocalScale>({20, 1, 14});
                art.set<LocalTranslation>({0, 0, 2});
                art.set<LocalRotation>({std::sqrt(.5f), 0, 0, std::sqrt(.5f)});
            } else {
                floor.set<Primitive>({3});
                floor.set<LocalScale>({14, 1, 12});
                floor.set<NavigationSurface>({true});
                floor.set<Tint>({.05f, .09f, .14f});
            }
            const auto actor = create(scene, xy ? "Chaser - inspect behavior and target"
                                                : "Patrol agent - inspect behavior");
            actor.set<LocalTranslation>({xy ? -2.f : -4.f, xy ? 0.f : .1f, 0});
            actor.set<NavigationAgent>({{}, true, false, xy ? 2.f : 3.5f, .06f, 0, 0, 0});
            body(scene, actor, {.08f, .8f, 1.f}, xy);
            NavigationBehavior behavior{{graph_ids[index]}, {}, {}, true};
            if (xy) {
                const auto target = create(scene, "Animated target - edit its clip");
                target.set<Primitive>({1});
                target.set<LocalScale>({.7f, .7f, .7f});
                target.set<Tint>({1, .35f, .08f});
                target.set<LocalTranslation>({2, 0, 0});
                TransformAnimator animator;
                animator.clip = {clip};
                target.set<TransformAnimator>(animator);
                behavior.target = scene.reference(target.get<PersistentEntityId>().value.str());
            } else {
                const auto anchor = create(scene, "Second patrol anchor");
                anchor.set<LocalTranslation>({0, 0, 3});
                const auto second =
                    create(scene, "Anchored patrol - shared graph, independent progress");
                second.set<LocalTranslation>({0, .1f, 3});
                second.set<NavigationAgent>({{}, true, false, 3.5f, .06f, 0, 0, 0});
                second.set<NavigationBehavior>(
                    {{graph_ids[0]},
                     {},
                     scene.reference(anchor.get<PersistentEntityId>().value.str()),
                     true});
                body(scene, second, {.65f, .25f, 1}, false);
                for (const float x : {-4.f, 4.f}) {
                    const auto marker = create(scene, "Patrol destination marker");
                    marker.set<Primitive>({2});
                    marker.set<Tint>({1, .7f, .1f});
                    marker.set<LocalTranslation>({x, .08f, 0});
                    marker.set<LocalScale>({.4f, .1f, .4f});
                }
            }
            actor.set<NavigationBehavior>(behavior);
            const auto camera = create(scene, "Overview Camera");
            Camera values;
            values.projection = unsigned(CameraProjection::Orthographic);
            values.orthographic_height = xy ? 17 : 16;
            camera.set<Camera>(values);
            camera.set<LocalTranslation>({0, xy ? 0.f : 13.f, xy ? -20.f : -16.f});
            if (!xy)
                camera.set<LocalRotation>({.38268343f, 0, 0, .92387953f});
            const auto light = create(scene, "Sunlight");
            light.set<Light>({});
            if (!xy)
                light.set<LocalRotation>({.38268343f, 0, 0, .92387953f});
        }
        // Reconcile helper roots in a prepared fixture without reminting IDs.
        const auto authored = scene.document();
        for (const auto& row : authored.at("entities")) {
            const auto e = scene.entity(row.at("id").get<std::string>());
            if (e.has<Camera>() || e.has<Light>() || e.has<NavigationAgent>() ||
                e.has<NavigationRegion2D>() || row.at("name") == "Second patrol anchor")
                e.set<Primitive>({no_primitive});
            if (xy && e.has<Camera>()) {
                auto camera = e.get<Camera>();
                camera.aspect = 16.0 / 9.0; // Preserve the whole chase course in portrait windows.
                e.set<Camera>(camera);
            }
        }
        NavigationSettings settings;
        settings.plane = xy ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
        const auto nav = prepare_navigation(root, scene.effective_document(), settings, worker)
                             .publish(scene.effective_document());
        author.world().world().each([&](flecs::entity e, const NavigationAgent& value) {
            auto next = value;
            next.navmesh = {nav.id};
            e.set<NavigationAgent>(next);
        });
        scene.save(root / scenes[index]);
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        std::cout << scenes[index] << " ready\n";
    }
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path, bool xy) {
    const auto saved = read(root / path);
    EngineContext engine(
        WorldRole::Runtime, false,
        {runtime_resources_module(root), animation_module(root), navigation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(saved);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto nav = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (true) {
        engine.services().resources()->synchronize();
        const bool ready = nav->prepare_assets() && animation->prepare_initial_pose();
        if (ready)
            break;
        check(std::chrono::steady_clock::now() < deadline, "Gallery behavior readiness timed out");
        std::this_thread::sleep_for(1ms);
    }
    flecs::entity actor;
    for (const auto& row : saved.at("entities"))
        if (row.at("name") ==
            (xy ? "Chaser - inspect behavior and target" : "Patrol agent - inspect behavior"))
            actor = scene.entity(row.at("id").get<std::string>());
    check(bool(actor), "Gallery behavior actor is missing");
    std::set<std::uint32_t> states;
    double minx = 100, maxx = -100;
    bool waited = false, moved = false;
    for (unsigned tick = 1; tick <= 960; ++tick) {
        simulation.tick(1.f / 60);
        engine.world().evaluate_world_transforms();
        const auto cursor = actor.get<NavigationBehaviorCursor>();
        states.insert(cursor.state);
        const auto p = actor.get<WorldTransform>().affine.point({0, 0, 0});
        minx = std::min(minx, p[0]);
        maxx = std::max(maxx, p[0]);
        const auto debug = nav->debug_behavior(actor);
        check(debug.at("phase") == "Running", "Gallery behavior failed: " + debug.dump());
        waited |= !actor.get<NavigationAgent>().has_destination;
        moved |= actor.get<NavigationAgent>().has_destination;
        if (xy)
            check(p[2] == 0, "XY Gallery behavior changed visual depth");
        if (tick == 1 || tick == 240 || tick == 960) {
            const auto rendered = extract_render_scene(simulation.presentation(.5));
            check(rendered.cameras.size() == 1 && rendered.lights.size() == 1 &&
                      rendered.meshes.size() >= 3,
                  "Gallery behavior lost ordinary Game render submissions");
        }
    }
    check(waited && moved &&
              (xy ? states == std::set<std::uint32_t>{1, 2} && maxx - minx > 2
                  : states == std::set<std::uint32_t>{1, 2, 3, 4} && minx < -3.8 && maxx > 3.8),
          "Gallery behavior did not realize its movement/wait/state cycle");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery project and scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str(),
                   root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        for (unsigned i = 0; i < 2; ++i) {
            exercise(root, scenes[i], i != 0);
            const auto source = read(root / scenes[i]);
            const std::array roots{source.at("asset_id").get<AssetId>()};
            const auto output = scratch / (i ? "XYGame" : "SpatialGame");
            (void)package_runtime_content(root, output, roots, {"windows", "d3d12"});
            const auto moved = scratch / (i ? "RelocatedXY" : "RelocatedSpatial"),
                       hidden = scratch / "SourcesOffline";
            std::filesystem::rename(output, moved);
            std::filesystem::rename(root, hidden);
            const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
            check(catalog.records().size() == (i ? 4u : 3u),
                  "Behavior Gallery package closure is incomplete or contains extra sources");
            check(!std::filesystem::exists(moved / graphs[i]) &&
                      !std::filesystem::exists(moved / target_clip),
                  "Behavior Gallery package kept editable graph/clip sources");
            exercise(moved, catalog.records().at(roots[0]).source, i != 0);
            std::filesystem::rename(hidden, root);
        }
        std::filesystem::remove_all(scratch);
        std::cout << "Gallery131/132 actual patrol/wait/chase, animation, rendering submissions "
                     "and source-offline relocated export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
