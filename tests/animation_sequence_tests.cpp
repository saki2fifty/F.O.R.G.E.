#include "../samples/projects/FeatureGallery/CodeExamples/SequenceControl.hpp"
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
#include <limits>
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
void camera_shot_identity(const std::filesystem::path& root) {
    const auto project = root / "shot-identity";
    std::filesystem::create_directories(project);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe, Json values = Json::object()) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("Director", "animation.sequence");
    const auto camera = create("Camera", "render.camera", {{"enabled", false}});
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    const EntityRef lens{scene.asset_id(), EntityId::parse(camera)};
    const auto shot =
        create("Shot", "animation.camera_shot", {{"sequence", group}, {"camera", lens}});
    scene.reparent_entity(camera, controller, ReparentMode::KeepLocal);
    scene.reparent_entity(shot, controller, ReparentMode::KeepLocal);
    auto original = scene.snapshot();
    for (auto& item : original["entities"])
        if (item["id"] == shot)
            item["components"]["future.shot"] = {{"sequence", group}, {"camera", lens}};
    scene.restore_snapshot(original);
    const auto duplicate = scene.duplicate_subtree(controller);
    const auto copy_camera = named(scene, "Camera", duplicate);
    const auto copy_shot = named(scene, "Shot", duplicate);
    const auto copied = scene.entity(copy_shot).get<CameraShot>();
    check(copied.sequence == EntityRef{scene.asset_id(), EntityId::parse(duplicate)} &&
              copied.camera == EntityRef{scene.asset_id(), EntityId::parse(copy_camera)},
          "Subtree copy did not remap both Camera Shot references");
    check(row(scene.document(), copy_shot).at("components").at("future.shot") ==
              Json{{"sequence", group}, {"camera", lens}},
          "Shot copy interpreted opaque reference-like data");
    const auto detached = scene.duplicate_subtree(shot);
    check(scene.entity(detached).get<CameraShot>().sequence == group &&
              scene.entity(detached).get<CameraShot>().camera == lens,
          "Shot-only copy rebound its external references");
    const auto definition = create_prefab_source(scene, controller);
    check(definition.source.at("version") == 3, "Camera shot Prefab did not declare member links");
    PrefabLibrary library(project);
    const auto prefab = library.create(scene, definition, "CameraRig.prefab.json");
    const auto a = instantiate_prefab(scene, prefab);
    const auto b = instantiate_prefab(scene, prefab);
    const auto verify = [&](const Scene& target, const std::string& actor) {
        const auto child = target.entity(named(target, "Shot", actor)).get<CameraShot>();
        check(child.sequence == EntityRef{target.asset_id(), EntityId::parse(actor)} &&
                  child.camera ==
                      EntityRef{target.asset_id(), EntityId::parse(named(target, "Camera", actor))},
              "Portable Camera Shot references crossed Prefab instances");
    };
    verify(scene, a);
    verify(scene, b);
    scene.save(project / "Shots.scene.json");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    library.load_scene(reopened, read_scene_file(project / "Shots.scene.json"));
    verify(reopened, a);
    verify(reopened, b);
    check(row(reopened.document(), shot).at("components").at("future.shot") ==
              Json{{"sequence", group}, {"camera", lens}},
          "Save/reopen changed opaque shot metadata");
}
void camera_shots(const std::filesystem::path& root) {
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* name, const char* recipe = "empty") {
        return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("Director", "animation.sequence");
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    auto sequence = scene.entity(controller).get<AnimationSequence>();
    sequence.duration = 3;
    scene.entity(controller).set(sequence);
    const auto a = create("Camera", "render.camera");
    const auto b = create("Camera", "render.camera");
    const auto fallback = create("Normal camera", "render.camera");
    auto camera = scene.entity(a).get<Camera>();
    camera.enabled = false;
    scene.entity(a).set(camera);
    scene.entity(b).set(camera);
    const auto add = [&](const std::string& target, double start, double end) {
        return authoring_command(
                   scene, "entity.create",
                   {{"name", "Shot"},
                    {"recipe", "animation.camera_shot"},
                    {"component_values",
                     {{"sequence", group},
                      {"camera", EntityRef{scene.asset_id(), EntityId::parse(target)}},
                      {"start_seconds", start},
                      {"end_seconds", end}}}})
            .at("selected")
            .get<std::string>();
    };
    const auto first = add(a, 0, 1);
    add(b, 1, 2);
    add(a, 2.5, 2.75);
    add(b, 2.75, 3);
    const auto before_invalid = scene.document();
    const auto revision = scene.revision();
    for (const auto& values : {Json{{"start_seconds", 2}, {"end_seconds", 1}},
                               Json{{"unknown_field", 4}}, Json::array({1})})
        rejects([&] {
            authoring_command(scene, "entity.create",
                              {{"recipe", "animation.camera_shot"}, {"component_values", values}});
        });
    rejects([&] {
        authoring_command(scene, "entity.create",
                          {{"recipe", "empty"}, {"component_values", Json::object()}});
    });
    check(scene.document() == before_invalid && scene.revision() == revision,
          "Rejected initial component values published a partial entity/history");
    const auto source = scene.snapshot();
    Runtime live(root, source);
    const auto shown = [&](Runtime& runtime, double alpha = 1) {
        const auto copied = runtime.simulation.presentation(alpha);
        const auto render = extract_render_scene(copied);
        check(render.cameras.size() == 1, "Camera shot did not select exactly one camera");
        check(!runtime.scene.entity(a).get<Camera>().enabled &&
                  !runtime.scene.entity(b).get<Camera>().enabled &&
                  runtime.scene.entity(fallback).get<Camera>().enabled,
              "Camera selection wrote native/authored enabled values");
        return render.cameras.front().entity.str();
    };
    check(shown(live) == a, "Initial camera shot is incorrect");
    live.simulation.tick(.75f);
    live.simulation.tick(.5f);
    check(shown(live, .49) == a && shown(live, .5) == b && shown(live) == b,
          "Adjacent camera cut did not use the shared presentation time");
    AnimationPlaybackAccess::control(*live.animation, group, {{"operation", "pause"}});
    live.simulation.tick(.5f);
    check(shown(live) == b, "Paused camera sequence advanced");
    const auto seek = [&](Runtime& runtime, double time) {
        AnimationPlaybackAccess::control(*runtime.animation, group,
                                         {{"operation", "seek"}, {"position_seconds", time}});
    };
    seek(live, 2.25);
    check(shown(live) == fallback, "Camera-shot gap did not restore authored composition");
    seek(live, 2.5);
    check(shown(live) == a, "Repeated camera reference was rebound by name");
    seek(live, 3);
    check(shown(live) == b, "One-shot completion did not hold its final camera");
    seek(live, 1.5);
    const auto checkpoint = live.animation->checkpoint();
    check(checkpoint.at("version") == 4 && checkpoint.at("camera_shots").size() == 4,
          "Camera-shot recovery did not declare its exact configuration");
    Runtime restored(root, source);
    restored.animation->restore(checkpoint);
    check(shown(restored) == b, "Recovered sequence camera differs");
    auto corrupt = checkpoint;
    corrupt["camera_shots"][0]["camera"]["entity"] = EntityId::generate();
    rejects([&] { restored.animation->restore(corrupt); });
    near(restored.animation->playback(group)->time, 1.5);
    auto old = checkpoint;
    old["version"] = 3;
    old.erase("camera_shots");
    rejects([&] { restored.animation->restore(old); });
    check(shown(restored) == b, "Rejected recovery changed camera selection");
    seek(live, .75);
    check(forge_set_sequence_speed(live.scene.entity(controller), .5f),
          "Compiled sequence helper rejected valid speed");
    check(!forge_set_sequence_speed(live.scene.entity(controller),
                                    std::numeric_limits<float>::quiet_NaN()) &&
              !forge_set_sequence_speed(live.scene.entity(a), .5f),
          "Compiled sequence helper accepted invalid input");
    AnimationPlaybackAccess::control(*live.animation, group, {{"operation", "play"}});
    live.simulation.tick(.5f);
    check(shown(live) == b, "C++ sequence speed did not reach camera selection");
    near(live.animation->playback(group)->time, 1);
    check(forge_set_sequence_speed(live.scene.entity(controller), 0),
          "Sequence zero speed rejected");
    live.simulation.tick(.5f);
    near(live.animation->playback(group)->time, 1);
    sequence.loop = true;
    live.scene.entity(controller).set(sequence);
    seek(live, 2.875);
    AnimationPlaybackAccess::control(*live.animation, group, {{"operation", "play"}});
    live.simulation.tick(.25f);
    check(shown(live, 0) == a && shown(live) == a,
          "Wrapped camera sequence did not snap with its clip presentation");
    sequence.enabled = false;
    live.scene.entity(controller).set(sequence);
    check(shown(live) == fallback, "Disabling sequence did not restore authored cameras");
    check(scene.snapshot() == source, "Runtime camera shots changed their authoring source");
    const auto modified = [&](const char* field, const Json& value) {
        auto result = source;
        for (auto& item : result["entities"])
            if (item["id"] == first)
                item["components"]["forge.camera_shot"][field] = value;
        return result;
    };
    rejects([&] { Runtime invalid(root, modified("end_seconds", 1.25)); });
    rejects([&] { Runtime invalid(root, modified("camera", group)); });
    rejects([&] {
        Runtime invalid(root, modified("sequence", EntityRef{AssetId::generate(), group.entity}));
    });
    rejects([&] { Runtime invalid(root, modified("end_seconds", 4)); });
    rejects([&] { Runtime invalid(root, modified("start_seconds", -1)); });
    {
        Runtime changed(root, source);
        const auto actor = changed.scene.entity(first);
        auto shot = actor.get<CameraShot>();
        const auto saved = shot;
        shot.camera = {changed.scene.asset_id(), EntityId::parse(b)};
        actor.set(shot);
        check(shown(changed) == b, "Native C++ camera retarget did not reach presentation");
        shot.end_seconds = std::numeric_limits<double>::quiet_NaN();
        actor.set(shot);
        check(shown(changed) == fallback,
              "Invalid native shot retained a guessed camera selection");
        shot.enabled = false;
        actor.set(shot);
        check(changed.animation->prepare_initial_pose() && shown(changed) == fallback,
              "Disabled invalid shot retained its admission error");
        actor.set(saved);
        check(changed.animation->prepare_initial_pose() && shown(changed) == a,
              "Corrected native shot did not recover its camera selection");
    }
    {
        auto competing = source;
        auto director = row(source, controller);
        const auto id = EntityId::generate();
        director["id"] = id;
        competing["entities"].push_back(director);
        for (auto& item : competing["entities"])
            if (item["id"] == first)
                item["components"]["forge.camera_shot"]["sequence"] =
                    EntityRef{scene.asset_id(), id};
        rejects([&] { Runtime invalid(root, competing); });
        competing["entities"].back()["components"]["forge.animation_sequence"]["enabled"] = false;
        Runtime inactive(root, competing);
        check(shown(inactive) == fallback,
              "Disabled competing director changed ordinary camera composition");
    }
    {
        Runtime scoped(root, source);
        const auto copied = duplicate_scene_asset(source);
        Scene second(scoped.engine.world());
        second.restore_snapshot(copied);
        check(scoped.animation->prepare_initial_pose(), "Two independent Scene directors failed");
        check(shown(scoped) == a, "A different Scene's shots selected this Scene's camera");
        for (const auto& item : copied.at("entities"))
            if (item.at("components").contains("forge.camera_shot") &&
                item.at("components").at("forge.camera_shot").at("start_seconds") == 0) {
                const auto config =
                    second.entity(item.at("id").get<std::string>()).get<CameraShot>();
                const auto target = second.entity(config.camera.entity.str());
                check(config.sequence.scene == second.asset_id() &&
                          config.camera.scene == second.asset_id() &&
                          scoped.animation->presentation_properties(target.id(), 1)
                              .at("forge.camera")
                              .at("enabled"),
                      "Whole-Scene copy lost shot/camera references or loaded-membership scope");
            }
    }
    {
        auto budget = source;
        auto& rows = budget["entities"];
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [](const auto& item) {
                                      return item.at("components").contains("forge.camera_shot");
                                  }),
                   rows.end());
        for (auto& item : rows)
            if (item["id"] == controller)
                item["components"]["forge.animation_sequence"]["duration"] = 300;
        const auto base = row(source, first);
        for (unsigned i = 0; i < 256; ++i) {
            auto item = base;
            item["id"] = EntityId::generate();
            item["components"]["forge.camera_shot"]["start_seconds"] = i;
            item["components"]["forge.camera_shot"]["end_seconds"] = i + 1;
            rows.push_back(std::move(item));
        }
        Runtime maximum(root, budget);
        check(maximum.animation->checkpoint().at("camera_shots").size() == 256,
              "Legal 256-shot arrangement was truncated");
        auto extra = rows.back();
        extra["id"] = EntityId::generate();
        extra["components"]["forge.camera_shot"]["start_seconds"] = 256;
        extra["components"]["forge.camera_shot"]["end_seconds"] = 257;
        rows.push_back(std::move(extra));
        rejects([&] { Runtime invalid(root, budget); });
    }
    std::cout << "Camera shots: shared clock/interpolation, cuts/gaps/end/loop, immutable cameras, "
                 "typed duplicate-name references and exact recovery passed\n";
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
        camera_shot_identity(root);
        camera_shots(root);
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
