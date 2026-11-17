#include "animation_playback_access.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <limits>
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
    std::string controller, first, second, incoming, output, rig, child;
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
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(0);
        scene.entity(second).set(mesh);
        Light light;
        light.kind = unsigned(LightKind::Point);
        scene.entity(second).set(light);
        child = create("empty", "Camera child mesh");
        scene.entity(child).set(mesh);
        scene.entity(child).set<LocalTranslation>({1, 0, 0});
        scene.reparent_entity(child, second, ReparentMode::KeepLocal);
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
double view_x(const Json& document, const std::string& id) {
    const auto render = extract_render_scene(document);
    const auto camera =
        std::find_if(render.cameras.begin(), render.cameras.end(),
                     [&](const auto& value) { return value.entity == EntityId::parse(id); });
    check(camera != render.cameras.end(), "Projected Camera was omitted");
    return camera->world.m[3];
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
            const double node_x = world.scene.entity(authored.rig).get<LocalTranslation>().x + 10;
            for (const auto& [id, expected] :
                 {std::pair{authored.second, node_x}, std::pair{authored.child, node_x + 1}}) {
                const auto mesh = std::find_if(
                    render.meshes.begin(), render.meshes.end(),
                    [&](const auto& value) { return value.entity == EntityId::parse(id); });
                check(mesh != render.meshes.end(), "Camera-attached mesh was omitted");
                near(mesh->world.m[3], expected);
                near(row(copied, id).at("world_affine").at(3), expected);
            }
            const auto light =
                std::find_if(render.lights.begin(), render.lights.end(), [&](const auto& value) {
                    return value.entity == EntityId::parse(authored.second);
                });
            check(light != render.lights.end(), "Camera-attached light was omitted");
            near(light->light.position[0], node_x);
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
        for (const auto& [curve, quarter, three_quarters] : std::array<std::array<double, 3>, 4>{
                 {{0, .15625, .84375}, {1, .25, .75}, {2, .0625, .5625}, {3, .4375, .9375}}}) {
            auto curved_source = source;
            set(curved_source, authored.incoming, "forge.camera_shot", "blend_curve",
                std::uint32_t(curve));
            World curved(root, curved_source);
            sample(curved, 1, 100, start_fov);
            sample(curved, 1.75, 100 + 10 * three_quarters,
                   std::lerp(start_fov, end_fov, three_quarters));
            sample(curved, 2, 110, end_fov);
            sample(curved, 1.25, 100 + 10 * quarter, std::lerp(start_fov, end_fov, quarter));
            const auto checkpoint = curved.animation->checkpoint();
            const auto version = checkpoint.at("version").get<unsigned>();
            check(version == (curve == 0 ? 8u : 16u),
                  "Camera curve changed a legacy profile or omitted its new recovery profile");
            World restored_curve(root, curved_source);
            restored_curve.animation->restore(checkpoint);
            near(view_x(restored_curve.simulation.presentation(1), authored.second),
                 100 + 10 * quarter);
            if (curve == 0)
                continue;
            check(checkpoint.at("camera_curve_profile").get<unsigned>() == 8,
                  "Camera curve recovery lost its underlying transition profile");
            const auto before_invalid = restored_curve.animation->checkpoint();
            auto wrong_curve = checkpoint;
            for (auto& shot : wrong_curve["camera_shots"])
                if (shot.at("entity").get<EntityRef>().entity == EntityId::parse(authored.incoming))
                    shot["blend_curve"] = 0u;
            rejects([&] { restored_curve.animation->restore(wrong_curve); });
            auto older = checkpoint;
            older["version"] = 8u;
            older.erase("camera_curve_profile");
            for (auto& shot : older["camera_shots"])
                shot.erase("blend_curve");
            rejects([&] { restored_curve.animation->restore(older); });
            auto bad_profile = checkpoint;
            bad_profile["camera_curve_profile"] = 3u;
            rejects([&] { restored_curve.animation->restore(bad_profile); });
            check(restored_curve.animation->checkpoint() == before_invalid,
                  "Rejected camera curve recovery changed clocks or configuration");
        }
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
        near(view_x(live.simulation.presentation(1), authored.second), 101.5625);
        command(live, "play");
        live.simulation.tick(.25f);
        near(view_x(live.simulation.presentation(.5), authored.second), 103.1640625);
        near(view_x(live.simulation.presentation(1), authored.second), 105);
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
        const auto before_curve_tuning = live.animation->checkpoint();
        for (const auto& invalid : {Json(-1), Json(4), Json(1.5), Json(),
                                    Json(std::numeric_limits<std::uint64_t>::max())}) {
            rejects([&] {
                AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref, invalid,
                                                          "blend_curve");
            });
            check(live.animation->checkpoint() == before_curve_tuning,
                  "Rejected live curve changed configuration or clocks");
        }
        const auto tuned = AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref,
                                                                     2u, "blend_curve");
        check(tuned.get<unsigned>() == 2 &&
                  live.scene.entity(authored.incoming).get<CameraShot>().blend_curve == 2,
              "Live curve did not publish the validated native value");
        AnimationPlaybackAccess::tune_camera_shot(*live.animation, incoming_ref, 0u, "blend_curve");
        check(live.animation->checkpoint() == before_curve_tuning,
              "Live curve tuning changed clock or unrelated shot configuration");
        const auto checkpoint = live.animation->checkpoint();
        check(checkpoint.at("version") == 8 &&
                  checkpoint.at("camera_shots").at(1).contains("blend_in_seconds"),
              "Transition recovery omitted its versioned configuration");
        World restored(root, source);
        restored.animation->restore(checkpoint);
        near(view_x(restored.simulation.presentation(1), authored.second), 105);
        auto old = checkpoint;
        old["version"] = 7;
        for (auto& item : old["camera_shots"])
            item.erase("blend_in_seconds");
        rejects([&] { restored.animation->restore(old); });
        near(view_x(restored.simulation.presentation(1), authored.second), 105);
        auto legacy = source;
        for (auto& item : legacy["entities"])
            if (item["components"].contains("forge.camera_shot")) {
                item["components"]["forge.camera_shot"].erase("blend_in_seconds");
                item["components"]["forge.camera_shot"].erase("blend_curve");
            }
        World cut(root, legacy);
        sample(cut, 1.5, 110, end_fov);
        check(cut.animation->checkpoint().at("version") == 4,
              "Zero/default blends changed the legacy recovery profile");
        check(cut.scene.entity(authored.incoming).get<CameraShot>().blend_curve == 0,
              "Missing source curve did not retain smooth timing");
        auto curved_cut_source = legacy;
        set(curved_cut_source, authored.incoming, "forge.camera_shot", "blend_curve", 3u);
        World curved_cut(root, curved_cut_source);
        sample(curved_cut, 1.5, 110, end_fov);
        const auto curved_cut_checkpoint = curved_cut.animation->checkpoint();
        check(curved_cut_checkpoint.at("version").get<unsigned>() == 16 &&
                  curved_cut_checkpoint.at("camera_curve_profile").get<unsigned>() == 4,
              "Zero-duration curve did not preserve the legacy cut recovery shape inside its "
              "envelope");
        World restored_curved_cut(root, curved_cut_source);
        restored_curved_cut.animation->restore(curved_cut_checkpoint);
        near(view_x(restored_curved_cut.simulation.presentation(1), authored.second), 110);
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
        near(view_x(nested.simulation.presentation(1), authored.second), 105);
        const auto nested_checkpoint = nested.animation->checkpoint();
        check(nested_checkpoint.at("sequence_profile") == 7,
              "Nested camera transitions lost their existing recovery profile");
        auto nested_curve_source = nested_source;
        set(nested_curve_source, authored.incoming, "forge.camera_shot", "blend_curve", 1u);
        World nested_curve(root, nested_curve_source);
        AnimationPlaybackAccess::control(*nested_curve.animation, outer_ref,
                                         {{"operation", "seek"}, {"position_seconds", 1.75}});
        const auto nested_curve_checkpoint = nested_curve.animation->checkpoint();
        check(nested_curve_checkpoint.at("version").get<unsigned>() == 16 &&
                  nested_curve_checkpoint.at("camera_curve_profile").get<unsigned>() == 8 &&
                  nested_curve_checkpoint.at("sequence_profile").get<unsigned>() == 7,
              "Curve recovery lost the nested controller/transition profile");
        World nested_curve_restored(root, nested_curve_source);
        nested_curve_restored.animation->restore(nested_curve_checkpoint);
        near(view_x(nested_curve_restored.simulation.presentation(1), authored.second), 102.5);
        World nested_restored(root, nested_source);
        nested_restored.animation->restore(nested_checkpoint);
        near(view_x(nested_restored.simulation.presentation(1), authored.second), 105);
        auto wrong_profile = nested_checkpoint;
        wrong_profile["sequence_profile"] = 4u;
        rejects([&] { nested_restored.animation->restore(wrong_profile); });
        near(view_x(nested_restored.simulation.presentation(1), authored.second), 105);
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
