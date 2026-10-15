#include "animation_playback_access.hpp"
#include "transform_animation_authoring.hpp"
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b) { check(std::abs(a - b) < 1e-5, "Sequence numeric oracle differs"); }
template <class F> void rejects(F function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid sequence operation accepted");
}
AssetId clip(const std::shared_ptr<ProjectLease>& lease, AssetImportService& imports,
             const std::filesystem::path& path, bool light, bool held = false) {
    auto doc = TransformAnimationDocument::create(lease, path);
    doc->edit(doc->revision(), "Sequence fixture", [&](Json& json) {
        TransformAnimationSource source{json};
        source.retime_duration(animation_timebase);
        if (light) {
            source.add_property(AnimationProperty::LightIntensity, 1);
            source.document["channels"] = Json::array();
            auto values = source.clip().keys.front().properties;
            values[unsigned(AnimationProperty::LightIntensity)] = 3;
            source.set_key(animation_timebase, {}, false, {}, {}, values);
        } else
            source.set_key(animation_timebase, {{4, 0, 0}, {}, {1, 1, 1}});
        if (held) {
            source.set_key(animation_timebase / 2, {{2, 0, 0}, {}, {1, 1, 1}});
            source.document["keys"][0]["interpolation"] = "held";
        }
        json = source.document;
    });
    doc->save();
    const auto id = doc->source().asset();
    imports.submit(
        imports.prepare(path, {}, id),
        [](auto& c, const auto& p, const auto&) { prepare_transform_animation_publication(c, p); },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "Sequence fixture publication timed out");
    auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes[0].published, "Sequence clip did not publish");
    return id;
}
struct Runtime {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    Runtime(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline, "Sequence admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
    }
};
const Json& row(const Json& source, const std::string& id) {
    for (const auto& item : source.at("entities"))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("Missing sequence entity row");
}
std::string named(const Scene& scene, const std::string& name, const std::string& parent = {}) {
    const auto document = scene.document();
    for (const auto& item : document.at("entities"))
        if (item.at("name") == name && item.value("parent", "") == parent)
            return item.at("id");
    throw std::runtime_error("Missing sequence entity name");
}
void persistence(const std::filesystem::path& root, AssetId clip_id) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto controller =
        authoring_command(scene, "entity.create",
                          {{"name", "Controller"}, {"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Door"}})
                           .at("selected")
                           .get<std::string>();
    scene.entity(actor).set(TransformAnimator{{clip_id}});
    scene.reparent_entity(actor, controller, ReparentMode::KeepLocal);
    const EntityRef original{scene.asset_id(), EntityId::parse(controller)};
    auto bind = [&](const std::string& id, EntityRef value) {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.transform_animator"},
                           {"field", "sequence"},
                           {"value", value}});
    };
    bind(actor, original);
    auto source = scene.snapshot();
    for (auto& item : source["entities"])
        if (item["id"] == actor)
            item["components"]["future.sequence"] = {{"untouched", original}};
    scene.restore_snapshot(source);
    scene.rename_entity(controller, "Renamed controller");
    scene.reparent_entity(actor, {}, ReparentMode::KeepLocal);
    check(scene.entity(actor).get<TransformAnimator>().sequence == original,
          "Rename/reparent changed sequence identity");
    scene.reparent_entity(actor, controller, ReparentMode::KeepLocal);
    const auto duplicate = scene.duplicate_subtree(controller);
    const auto copied_actor = named(scene, "Door", duplicate);
    check(scene.entity(copied_actor).get<TransformAnimator>().sequence ==
              EntityRef{scene.asset_id(), EntityId::parse(duplicate)},
          "Subtree duplicate did not remap controller");
    check(row(scene.document(), copied_actor).at("components").at("future.sequence") ==
              Json{{"untouched", original}},
          "Subtree duplicate rewrote opaque references");
    const auto detached_copy = scene.duplicate_subtree(actor);
    check(scene.entity(detached_copy).get<TransformAnimator>().sequence == original,
          "Target-only duplicate retargeted an external controller");
    scene.save(root / "Sequence.scene.json");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    reopened.reset(read_scene_file(root / "Sequence.scene.json"));
    check(reopened.entity(actor).get<TransformAnimator>().sequence == original,
          "Save/reopen lost sequence reference");
    const auto whole = duplicate_scene_asset(scene.document());
    EngineContext copied_engine;
    Scene copied(copied_engine.world());
    copied.reset(whole);
    const auto copied_controller = named(copied, "Renamed controller");
    const auto whole_actor = named(copied, "Door", copied_controller);
    check(copied.entity(whole_actor).get<TransformAnimator>().sequence ==
              EntityRef{copied.asset_id(), EntityId::parse(copied_controller)},
          "Whole-scene copy did not remap scene/controller identity");
    check(row(whole, whole_actor).at("components").at("future.sequence") ==
              Json{{"untouched", original}},
          "Whole-scene copy interpreted opaque reference");
    check(create_prefab_source(scene, controller).source.at("version") == 3,
          "Portable controller/track subtree capture lost its versioned member link");
    // Portable actor definitions stay unbound; the Scene owns instance bindings.
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.transform_animator"},
                       {"field", "sequence"},
                       {"value", nullptr}});
    const auto definition = create_prefab_source(scene, actor);
    PrefabLibrary library(root);
    const auto prefab = library.create(scene, definition, "SequenceActor.prefab.json");
    const auto instance = instantiate_prefab(scene, prefab);
    scene.rename_entity(instance, "Prefab door");
    scene.reparent_entity(instance, controller, ReparentMode::KeepLocal);
    bind(instance, original);
    const auto copied_root = scene.duplicate_subtree(controller);
    const auto copied_instance = named(scene, "Prefab door", copied_root);
    check(scene.entity(copied_instance).get<TransformAnimator>().sequence ==
              EntityRef{scene.asset_id(), EntityId::parse(copied_root)},
          "Prefab instance override did not remap with its controller");
    scene.save(root / "PrefabSequence.scene.json");
    library.load_scene(reopened, read_scene_file(root / "PrefabSequence.scene.json"));
    check(reopened.entity(copied_instance).get<TransformAnimator>().sequence ==
              EntityRef{reopened.asset_id(), EntityId::parse(copied_root)},
          "Prefab instance override lost its sequence on reopen");
}
void clock_policy(const std::filesystem::path& root, Json source, const std::string& controller,
                  const std::string& target) {
    for (auto& item : source["entities"])
        if (item["id"] == controller) {
            item["components"]["forge.animation_sequence"]["loop"] = true;
            item["components"]["forge.animation_sequence"]["playback_speed"] = 2;
        }
    Runtime runtime(root, source);
    const EntityRef ref{runtime.scene.asset_id(), EntityId::parse(controller)};
    RuntimeClock clock({4, .25, 8});
    auto now = RuntimeClock::Time{};
    const auto tick = [&](float dt) {
        near(dt, .25);
        runtime.simulation.tick(dt);
    };
    clock.set_time_scale(.5);
    clock.resume(now);
    now += std::chrono::milliseconds(250);
    clock.advance(now, tick);
    near(runtime.animation->playback(ref)->time, 0);
    now += std::chrono::milliseconds(250);
    clock.advance(now, tick);
    near(runtime.animation->playback(ref)->time, .5);
    clock.pause(now);
    clock.set_time_scale(0);
    now += std::chrono::milliseconds(250);
    clock.advance(now, tick);
    near(runtime.animation->playback(ref)->time, .5);
    clock.step(tick);
    near(runtime.animation->playback(ref)->time, 1);
    near(runtime.scene.entity(target).get<LocalTranslation>().x, 2);
    clock.resume(now);
    now += std::chrono::milliseconds(250);
    clock.advance(now, tick);
    near(runtime.animation->playback(ref)->time, 1);
    clock.set_time_scale(2);
    now += std::chrono::milliseconds(250);
    clock.advance(now, tick);
    near(runtime.animation->playback(ref)->time, 0);
    near(runtime.scene.entity(target).get<LocalTranslation>().x, 0);
    const auto presented = runtime.simulation.presentation(.5);
    near(row(presented, target).at("components").at("forge.local_translation").at("x"), 0);
    check(clock.tick() == 4, "Sequence changed fixed-clock tick count");
    const auto before = runtime.animation->checkpoint();
    auto missing = source;
    for (auto& item : missing["entities"])
        if (item["id"] == target)
            item["components"]["forge.transform_animator"]["sequence"]["entity"] =
                EntityId::generate();
    rejects([&] { Runtime invalid(root, missing); });
    check(runtime.animation->checkpoint() == before, "Other-world rejection changed live clocks");
}
void held_boundary(const std::filesystem::path& root, Json source, const std::string& target,
                   AssetId held) {
    for (auto& item : source["entities"])
        if (item["id"] == target)
            item["components"]["forge.transform_animator"]["clip"] = held;
    Runtime runtime(root, source);
    runtime.simulation.tick(.75f);
    near(runtime.scene.entity(target).get<LocalTranslation>().x, 0);
    runtime.simulation.tick(.25f);
    near(runtime.scene.entity(target).get<LocalTranslation>().x, 2);
    const auto presented = runtime.simulation.presentation(.5);
    near(row(presented, target).at("components").at("forge.local_translation").at("x"), 2);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need sequence scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService imports(lease, transform_animation_import_registry(),
                                   {"windows", "d3d12", "desktop"});
        const auto motion = clip(lease, imports, "Assets/Motion.animation.json", false);
        const auto pulse = clip(lease, imports, "Assets/Pulse.animation.json", true);
        const auto held = clip(lease, imports, "Assets/Held.animation.json", false, true);
        persistence(root, motion);
        EngineContext author;
        Scene scene(author.world());
        auto create = [&](const char* name) {
            return authoring_command(scene, "entity.create", {{"name", name}})
                .at("selected")
                .get<std::string>();
        };
        const auto controller =
            authoring_command(scene, "entity.create",
                              {{"name", "Sequence"}, {"recipe", "animation.sequence"}})
                .at("selected")
                .get<std::string>();
        const auto moving = create("Door");
        const auto lighting = create("Light");
        const auto legacy = create("Independent legacy clip");
        AnimationSequence config;
        config.duration = 2;
        scene.entity(controller).set(config);
        const EntityRef ref{scene.asset_id(), EntityId::parse(controller)};
        TransformAnimator move{{motion}};
        move.sequence = ref;
        move.sequence_start_seconds = .5;
        scene.entity(moving).set(move);
        TransformAnimator light{{pulse}};
        light.sequence = ref;
        light.sequence_start_seconds = 1.1;
        scene.entity(lighting).set(light).set(Light{});
        scene.entity(legacy).set(TransformAnimator{{motion}});
        auto source = scene.snapshot();
        for (auto& row : source["entities"])
            if (row["id"] == legacy) {
                row["components"]["forge.transform_animator"].erase("sequence");
                row["components"]["forge.transform_animator"].erase("sequence_start_seconds");
            }
        clock_policy(root, source, controller, moving);
        held_boundary(root, source, moving, held);
        Runtime runtime(root, source);
        check(AnimationPlaybackAccess::status(*runtime.animation, ref)
                  .value("sequence_controller", false),
              "Live controller status lost its sequence kind");
        const auto position = [&] {
            return runtime.scene.entity(moving).get<LocalTranslation>().x;
        };
        const auto intensity = [&] {
            return runtime.scene.entity(lighting).get<Light>().intensity;
        };
        near(position(), 0);
        near(intensity(), 1);
        check(!runtime.scene.entity(legacy).get<TransformAnimator>().sequence.entity,
              "Legacy clip acquired a sequence");
        const EntityRef track_ref{scene.asset_id(), EntityId::parse(moving)};
        check(runtime.animation->playback(track_ref).has_value() &&
                  !runtime.animation->control(track_ref, AnimationControl::Pause, 100) &&
                  !runtime.animation->seek(track_ref, .25, 100),
              "Bound track clock was unavailable or independently controllable");
        for (unsigned tick = 0; tick < 5; ++tick)
            runtime.simulation.tick(.25);
        near(position(), 3);
        near(intensity(), 1.3);
        const auto render = extract_render_scene(runtime.simulation.presentation(.5));
        check(render.lights.size() == 1, "Sequence light not extracted");
        near(render.lights[0].light.intensity, 1.05); // Offset crossing must sample shared time.
        near(runtime.animation->playback(ref)->time, 1.25);
        check(!runtime.animation->control(ref, AnimationControl::Pause, 0),
              "Past-tick sequence request accepted");
        AnimationPlaybackAccess::control(*runtime.animation, ref, {{"operation", "pause"}});
        runtime.simulation.tick(.25);
        near(position(), 3);
        near(intensity(), 1.3);
        const auto checkpoint = runtime.animation->checkpoint();
        check(checkpoint.at("version") == 3 && checkpoint.at("sequence_entries").size() == 1,
              "Missing sequence recovery clocks");
        Runtime recovered(root, source);
        recovered.animation->restore(checkpoint);
        near(recovered.animation->playback(ref)->time, 1.25);
        near(recovered.scene.entity(moving).get<LocalTranslation>().x, 3);
        auto damaged = checkpoint;
        damaged["sequence_entries"][0]["duration"] = 3;
        rejects([&] { recovered.animation->restore(damaged); });
        near(recovered.animation->playback(ref)->time, 1.25);
        auto old = checkpoint;
        old["version"] = 2;
        old.erase("sequence_entries");
        rejects([&] { recovered.animation->restore(old); });
        AnimationPlaybackAccess::control(*runtime.animation, ref,
                                         {{"operation", "seek"}, {"position_seconds", .25}});
        near(position(), 0);
        near(intensity(), 1);
        AnimationPlaybackAccess::control(*runtime.animation, ref, {{"operation", "play"}});
        for (unsigned tick = 0; tick < 7; ++tick)
            runtime.simulation.tick(.25);
        near(position(), 4);
        near(intensity(), 2.8);
        check(runtime.animation->playback(ref)->finished, "Sequence did not finish");
        AnimationPlaybackAccess::control(*runtime.animation, ref, {{"operation", "restart"}});
        near(position(), 0);
        near(intensity(), 1);
        const auto before = position();
        runtime.scene.entity(lighting).remove<Light>();
        runtime.simulation.tick(.75);
        near(position(), before); // Invalid property target must prevent the door write too.
        std::cout << "Sequence shared time, offsets/holds, copied presentation, controls, "
                     "recovery, legacy defaults and atomic candidates passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
