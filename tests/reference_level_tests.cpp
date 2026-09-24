#include "../samples/reference_game/ids.hpp"
#include <algorithm>
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <forge/native_sdk.hpp>
#include <forge/navigation.hpp>
#include <forge/project.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using namespace forge;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Need native module, generated project and evidence root");
        const auto root = std::filesystem::absolute(argv[2]),
                   evidence = std::filesystem::absolute(argv[3]);
        ProjectSettings project(root);
        auto queue = std::make_shared<GameControlQueue>();
        GameSessionConfig config;
        config.controls = queue;
        config.input = project.input();
        config.physics = project.physics();
        config.content_root = root;
        config.ui = true;
        config.audio = AudioConfig{root, AudioOutput::Offline, true};
        config.modules = {load_native_sdk(argv[1], "project.reference", "1")};
        GameSession game(config);
        const auto start = load_game_scene(root, {AssetId::parse(reference::menu_scene)});
        auto now = RuntimeClock::Time{};
        game.activate(game.prepare(start), now, false);
        GameStorage storage(evidence / "users", "org.forge.reference-level");
        bool captured = false;
        GamePlatformControls platform;
        platform.cursor = [&](bool value) { captured = value; };
        GameHostControls host(game, queue, storage, root, project.document().at("game"),
                              project.input(), Json::object(), platform);
        auto model = [&] {
            return std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui())
                ->snapshot(game.active().scene, "acceptance", 1, 0,
                           game.status().at("state") == "paused");
        };
        std::vector<double> control_us, fixed_us, transitions_ms;
        auto control = [&] {
            const auto began = std::chrono::steady_clock::now();
            game.control_frame();
            host.pump(now);
            control_us.push_back(
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began)
                    .count());
        };
        auto command = [&](const char* value) {
            const auto snapshot = model();
            std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui())
                ->command(game.active().scene,
                          {{"instance", snapshot.at("documents")[0].at("instance")},
                           {"command", "Reference"},
                           {"value", value}},
                          [](const std::string&) {});
            control();
        };
        auto wait_scene = [&](const char* id) {
            const auto began = std::chrono::steady_clock::now();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (game.active().scene.asset_id().str() != id) {
                require(std::chrono::steady_clock::now() < deadline,
                        "Reference scene transition timed out");
                control();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            control();
            transitions_ms.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began)
                    .count());
        };
        auto check_render = [&] {
            const auto scene = extract_render_scene(game.active().simulation.presentation(1));
            require(scene.diagnostics.empty() && scene.omitted_diagnostics == 0,
                    "Reference scene has invalid presentation components");
            const auto cameras = prepare_game_cameras(scene, 1280, 720);
            require(!cameras.cameras.empty() && cameras.diagnostics.empty() &&
                        cameras.omitted_diagnostics == 0,
                    "Reference scene has no usable authored camera");
        };
        check_render();
        control();
        require(!captured, "Main menu captured the mouse");
        command("start");
        wait_scene(reference::level_scene);
        check_render();
        require(captured, "Level failed to capture through scoped service");
        const auto level_render = extract_render_scene(game.active().simulation.presentation(1));
        require(std::any_of(level_render.lights.begin(), level_render.lights.end(),
                            [](const RenderLight& light) {
                                return light.light.kind == std::uint32_t(LightKind::Directional) &&
                                       light.light.intensity > 0 && light.light.direction[1] < -.25;
                            }),
                "Reference sun does not illuminate the upward-facing floor");
        double nav_start = 0;
        flecs::entity_t nav_entity = 0, animated = 0;
        game.active().engine.world().world().each([&](flecs::entity e, const NavigationAgent&) {
            nav_entity = e.id();
            nav_start = e.get<LocalTranslation>().z;
        });
        game.active().engine.world().world().each(
            [&](flecs::entity e, const Animator&) { animated = e.id(); });
        require(nav_entity && animated, "Reference assets omitted navigation or animation");
        for (unsigned i = 0; i < 181; ++i) {
            now += std::chrono::nanoseconds(16666667);
            const auto began = std::chrono::steady_clock::now();
            game.advance(now);
            fixed_us.push_back(
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began)
                    .count());
            host.pump(now);
            control();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto nav_end =
            game.active().engine.world().world().entity(nav_entity).get<LocalTranslation>().z;
        require(std::abs(nav_end - nav_start) > 1, "Reference navigation agent did not move");
        const auto animation = animation_runtime(game.active().engine.world());
        require(animation->model_pose_ready(animated) && animation->checkpoint_ready(),
                "Reference skinned model did not become ready");
        std::vector<float> samples(4096);
        auto audio =
            std::static_pointer_cast<AudioRuntime>(game.active().engine.services().audio());
        audio->read_offline(samples);
        double energy = 0;
        for (auto sample : samples)
            energy += std::abs(sample);
        require(energy > .01, "Reference ambient/spatial source produced no offline audio");
        game.input({{"key.e", 1}});
        now += std::chrono::milliseconds(17);
        game.advance(now);
        control();
        require(model().at("documents")[0].at("model").at("interactions") == 1,
                "Reference level beacon was not selected");
        game.input({{"key.e", 0}, {"key.escape", 1}});
        control();
        require(!captured && game.status().at("state") == "paused",
                "Reference pause did not release cursor");
        command("save");
        control();
        command("main");
        wait_scene(reference::menu_scene);
        command("load");
        wait_scene(reference::level_scene);
        require(model().at("documents")[0].at("model").at("interactions") == 1,
                "Reference save did not restore interaction state");
        auto median = [](std::vector<double> samples) {
            std::sort(samples.begin(), samples.end());
            return samples.at(samples.size() / 2);
        };
        atomic_write(evidence / "reference-level.json",
                     Json{{"navigation_distance", std::abs(nav_end - nav_start)},
                          {"offline_audio_energy", energy},
                          {"animation_ready", true},
                          {"scene_round_trip", true},
                          {"save_restored", true},
                          {"headless_fixed_step_us_median", median(fixed_us)},
                          {"headless_control_and_host_us_median", median(control_us)},
                          {"transition_wait_ms", transitions_ms}}
                         .dump(2));
        std::cout << "Reference level integration passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
