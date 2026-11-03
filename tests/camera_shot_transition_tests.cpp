#include "animation_playback_access.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    if (std::abs(actual - expected) > 1e-5)
        throw std::runtime_error("Camera transition oracle: " + std::to_string(actual) + " vs " +
                                 std::to_string(expected));
}
template <class Function> void rejects(Function&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Incompatible Camera transition recovery was accepted");
}
struct Fixture {
    EngineContext engine;
    Scene scene{engine.world()};
    std::string controller, first, second, incoming, output, rig;
    EntityRef group;
    Fixture() {
        const auto create = [&](const char* recipe, const char* name) {
            return authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
                .at("selected")
                .get<std::string>();
        };
        controller = create("animation.sequence", "Director");
        group = {scene.asset_id(), EntityId::parse(controller)};
        auto sequence = scene.entity(controller).get<AnimationSequence>();
        sequence.duration = 3;
        scene.entity(controller).set(sequence);
        rig = create("empty", "Moving rig");
        scene.entity(rig).set<LocalTranslation>({100, 0, 0});
        scene.reparent_entity(rig, controller, ReparentMode::KeepLocal);
        first = create("render.camera", "A");
        second = create("render.camera", "B");
        for (const auto& id : {first, second}) {
            auto camera = scene.entity(id).get<Camera>();
            camera.enabled = false;
            if (id == second)
                camera.vertical_fov = std::numbers::pi / 2;
            scene.entity(id).set(camera);
            scene.entity(id).set<LocalTranslation>({id == first ? 0. : 10., 0, 0});
            scene.reparent_entity(id, rig, ReparentMode::KeepLocal);
        }
        create("render.camera", "Fallback");
        output = create("render.camera", "Independent output");
        auto producer = scene.entity(output).get<Camera>();
        producer.render_texture = {AssetId::generate()};
        scene.entity(output).set(producer);
        const auto shot = [&](const char* name, const std::string& target, double start, double end,
                              double blend) {
            const auto id = create("animation.camera_shot", name);
            scene.entity(id).set<CameraShot>(
                {group, {scene.asset_id(), EntityId::parse(target)}, start, end, true, blend});
            scene.reparent_entity(id, controller, ReparentMode::KeepLocal);
            return id;
        };
        shot("Outgoing", first, 0, 1, 0);
        incoming = shot("Incoming", second, 1, 3, 1);
    }
};
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source, bool admit = true)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        if (!admit)
            return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Camera transition admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
    }
};
const Json& row(const Json& document, const std::string& id) {
    const auto& rows = document.at("entities");
    return *std::find_if(rows.begin(), rows.end(), [&](const auto& v) { return v.at("id") == id; });
}
void set(Json& document, const std::string& id, const char* component, const char* field,
         const Json& value) {
    for (auto& item : document.at("entities"))
        if (item.at("id") == id)
            item["components"][component][field] = value;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Camera transition scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::ofstream(root / "forge.assets.json")
            << Json{{"version", 1}, {"assets", Json::array()}};
        Fixture authored;
        const auto source = authored.scene.document();
        World live(root, source);
        const auto seek = [&](World& world, double time) {
            AnimationPlaybackAccess::control(*world.animation, authored.group,
                                             {{"operation", "seek"}, {"position_seconds", time}});
        };
        const auto sample = [&](World& world, double time, double x, double fov) {
            seek(world, time);
            const auto copied = world.simulation.presentation(1);
            const auto render = extract_render_scene(copied);
            const auto main = prepare_game_cameras(render, 800, 600);
            check(main.cameras.size() == 1 &&
                      main.cameras[0].entity == EntityId::parse(authored.second),
                  "Incoming Camera lost its stable identity");
            near(main.cameras[0].view.position[0], x);
            near(main.cameras[0].settings.vertical_fov, fov);
            check(row(copied, authored.output).at("components").at("forge.camera").at("enabled") ==
                      true,
                  "Cinematic transition disabled its independent output producer");
            near(world.scene.entity(authored.second).get<LocalTranslation>().x, 10);
            near(world.scene.entity(authored.second).get<Camera>().vertical_fov,
                 std::numbers::pi / 2);
        };
        const double start_fov = std::numbers::pi / 3, end_fov = std::numbers::pi / 2;
        for (const auto& [time, weight] : std::array<std::pair<double, double>, 5>{
                 {{1, 0}, {1.25, .15625}, {1.5, .5}, {1.75, .84375}, {2, 1}}})
            sample(live, time, 100 + 10 * weight, std::lerp(start_fov, end_fov, weight));
        sample(live, 1.5, 105, 5 * std::numbers::pi / 12);
        // Projection preserves forward-compatible Camera data and source-local values.
        auto opaque = live.scene.effective_document();
        for (auto& item : opaque["entities"])
            if (item["id"] == authored.second)
                item["components"]["forge.camera"]["future_camera_setting"] = 17;
        live.animation->project_camera_shots(opaque, 1);
        check(row(opaque, authored.second)
                      .at("components")
                      .at("forge.camera")
                      .at("future_camera_setting") == 17,
              "Camera projection discarded an unknown copied Camera field");
        live.scene.entity(authored.rig).set<LocalTranslation>({120, 0, 0});
        live.simulation.reset_presentation();
        sample(live, 1.5, 125, 5 * std::numbers::pi / 12);
        live.scene.entity(authored.rig).set<LocalTranslation>({100, 0, 0});
        live.simulation.reset_presentation();
        const auto command = [&](World& world, const char* operation) {
            AnimationPlaybackAccess::control(*world.animation, authored.group,
                                             {{"operation", operation}});
        };
        seek(live, 1.25);
        command(live, "pause");
        live.simulation.tick(.25f);
        near(row(live.simulation.presentation(1), authored.second).at("world_affine").at(3),
             101.5625);
        command(live, "play");
        live.simulation.tick(.25f);
        near(row(live.simulation.presentation(.5), authored.second).at("world_affine").at(3),
             103.1640625);
        near(row(live.simulation.presentation(1), authored.second).at("world_affine").at(3), 105);
        const EntityRef incoming_ref{authored.scene.asset_id(), EntityId::parse(authored.incoming)};
        const auto before_tuning = live.animation->checkpoint();
        rejects([&] {
            AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref, 9999.);
        });
        check(live.animation->checkpoint() == before_tuning,
              "Rejected live duration changed configuration or clocks");
        near(AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref, .5), .5);
        near(live.scene.entity(authored.incoming).get<CameraShot>().blend_in_seconds, .5);
        AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref, 1.);
        const auto checkpoint = live.animation->checkpoint();
        check(checkpoint.at("version") == 8 &&
                  checkpoint.at("camera_shots").at(1).contains("blend_in_seconds"),
              "Transition recovery omitted its versioned configuration");
        World restored(root, source);
        restored.animation->restore(checkpoint);
        near(row(restored.simulation.presentation(1), authored.second).at("world_affine").at(3),
             105);
        auto old = checkpoint;
        old["version"] = 7;
        for (auto& item : old["camera_shots"])
            item.erase("blend_in_seconds");
        rejects([&] { restored.animation->restore(old); });
        near(row(restored.simulation.presentation(1), authored.second).at("world_affine").at(3),
             105);
        auto legacy = source;
        for (auto& item : legacy["entities"])
            if (item["components"].contains("forge.camera_shot"))
                item["components"]["forge.camera_shot"].erase("blend_in_seconds");
        World cut(root, legacy);
        sample(cut, 1.5, 110, end_fov);
        check(cut.animation->checkpoint().at("version") == 4,
              "Zero/default blends changed the legacy recovery profile");
        auto looping_source = source;
        set(looping_source, authored.controller, "forge.animation_sequence", "loop", true);
        World looping(root, looping_source);
        seek(looping, 2.9);
        looping.simulation.tick(.2f);
        const auto wrapped = prepare_game_cameras(
            extract_render_scene(looping.simulation.presentation(.5)), 800, 600);
        check(wrapped.cameras.size() == 1 &&
                  wrapped.cameras[0].entity == EntityId::parse(authored.first),
              "Loop start blended from the previous cycle instead of cutting");
        near(wrapped.cameras[0].view.position[0], 100);
        auto gap = source;
        set(gap, authored.incoming, "forge.camera_shot", "start_seconds", 1.2);
        World spaced(root, gap);
        sample(spaced, 1.5, 110, end_fov);
        check(authored.scene.document() == source,
              "Transition sampling changed authored Scene data");
        // A nested controller uses the root clock; recovery retains that profile.
        const auto outer = authoring_command(authored.scene, "entity.create",
                                             {{"recipe", "animation.sequence"}, {"name", "Root"}})
                               .at("selected")
                               .get<std::string>();
        const EntityRef outer_ref{authored.scene.asset_id(), EntityId::parse(outer)};
        auto outer_config = authored.scene.entity(outer).get<AnimationSequence>();
        outer_config.duration = 4;
        authored.scene.entity(outer).set(outer_config);
        auto inner_config = authored.scene.entity(authored.controller).get<AnimationSequence>();
        inner_config.sequence = outer_ref;
        inner_config.sequence_start_seconds = .5;
        authored.scene.entity(authored.controller).set(inner_config);
        const auto nested_source = authored.scene.document();
        World nested(root, nested_source);
        AnimationPlaybackAccess::control(*nested.animation, outer_ref,
                                         {{"operation", "seek"}, {"position_seconds", 2.}});
        near(row(nested.simulation.presentation(1), authored.second).at("world_affine").at(3), 105);
        const auto nested_checkpoint = nested.animation->checkpoint();
        check(nested_checkpoint.at("sequence_profile") == 7,
              "Nested camera transitions lost their existing recovery profile");
        World nested_restored(root, nested_source);
        nested_restored.animation->restore(nested_checkpoint);
        near(row(nested_restored.simulation.presentation(1), authored.second)
                 .at("world_affine")
                 .at(3),
             105);
        auto wrong_profile = nested_checkpoint;
        wrong_profile["sequence_profile"] = 4u;
        rejects([&] { nested_restored.animation->restore(wrong_profile); });
        near(row(nested_restored.simulation.presentation(1), authored.second)
                 .at("world_affine")
                 .at(3),
             105);
        auto incompatible_cut = legacy;
        set(incompatible_cut, authored.second, "forge.camera", "layers", 1);
        World cut_profile(root, incompatible_cut);
        const auto cut_before = cut_profile.animation->checkpoint();
        rejects([&] {
            AnimationPlaybackAccess::tune_camera_shot(*cut_profile.animation, incoming_ref, .5);
        });
        check(cut_profile.animation->checkpoint() == cut_before &&
                  cut_profile.scene.entity(authored.incoming).get<CameraShot>().blend_in_seconds ==
                      0,
              "Rejected live pair compatibility changed its duration or clocks");
        auto incompatible = source;
        set(incompatible, authored.second, "forge.camera", "layers", 1);
        World invalid(root, incompatible, false);
        invalid.animation->synchronize();
        const auto diagnostics = invalid.engine.services().diagnostics();
        check(std::any_of(diagnostics.begin(), diagnostics.end(),
                          [](const auto& v) {
                              return v.at("text").template get<std::string>().find(
                                         "matching projection") != std::string::npos;
                          }),
              "Incompatible composition did not report its transition profile");
        std::cout << "Parented Camera transition timing/easing/lens, copied authority, independent "
                     "outputs, legacy cuts, gaps and versioned recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
