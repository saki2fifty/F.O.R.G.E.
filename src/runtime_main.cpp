#include "animation_playback_access.hpp"
#include "audio_mix.hpp"
#include "audio_source_access.hpp"
#include "authored_inspection.hpp"
#include "cpu_profile_control.hpp"
#include "particle_runtime.hpp"
#include "runtime_io.hpp"
#include "runtime_property_tune.hpp"
#include "runtime_tile_map.hpp"
#include "sdk_play_runtime.hpp"
#include "tile_map_play_transport.hpp"
#include "tile_map_play_world.hpp"
#include <cstdlib>
#include <ctime>
#include <forge/build.hpp>
#include <forge/game_settings.hpp>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/navigation.hpp>
#include <forge/project.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
namespace {
void prepare_physics(forge::PhysicsRuntime& physics) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!physics.prepare_assets()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Runtime collision preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void prepare_tile_maps(forge::RuntimeWorld& runtime, const forge::Json* checkpoint = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto& context = runtime.engine.world();
    for (;;) {
        if (context.services().available(forge::Capability::Resources))
            context.services().resources()->synchronize();
        forge::detail::synchronize_runtime_tile_maps(context);
        bool ready = forge::detail::prepare_runtime_tile_maps(context);
        if (checkpoint)
            for (const auto& row : checkpoint->at("entries")) {
                const auto status = forge::detail::inspect_runtime_tile_map(
                    context, row.at("entity").get<forge::EntityRef>());
                if (!status.ready || !status.current) {
                    if (!status.pending)
                        throw std::runtime_error("Runtime Tile Map recovery preparation: " +
                                                 status.diagnostic);
                    ready = false;
                }
            }
        if (ready)
            return;
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Runtime Tile Map preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--sdk-info") {
        std::cout << forge::Json{{"profile", FORGE_NATIVE_SDK_PROFILE},
                                 {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT}}
                         .dump()
                  << '\n';
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE runtime | Build: " << forge::build_id << '\n';
        return 0;
    }
    std::clog << "FORGE runtime | Build: " << forge::build_id << '\n';
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    try {
        // Existing diagnostic opt-in, separate bounded bootstrap file: an early
        // startup stall can precede both the protocol and SDK host's own trace.
        // Only startup and the first replacement are recorded; default startup does no IO.
        const auto startup_begin = std::chrono::steady_clock::now();
        std::ofstream startup_log;
        if (const auto* directory = std::getenv("FORGE_SDK_DIAGNOSTIC_DIR");
            directory && *directory) {
            try {
#ifdef _WIN32
                const auto process = GetCurrentProcessId();
#else
                const auto process = getpid();
#endif
                startup_log.open(std::filesystem::u8path(directory) /
                                 ("runtime-startup-" + std::to_string(process) + "-" +
                                  std::to_string(startup_begin.time_since_epoch().count()) +
                                  ".log"));
            } catch (...) { /* Optional diagnostics cannot prevent startup. */
            }
        }
        const auto startup_phase = [&](const char* phase) {
            if (!startup_log.is_open() || !startup_log)
                return;
            const auto now = std::time(nullptr);
            std::tm utc{};
#ifdef _WIN32
            if (gmtime_s(&utc, &now) != 0)
                return;
#else
            if (!gmtime_r(&now, &utc))
                return;
#endif
            startup_log << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ") << " elapsed_ms="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startup_begin)
                               .count()
                        << " phase=" << phase << std::endl;
        };
        startup_phase("main");
        if (argc == 2 && std::string(argv[1]) == "--inspect-sdk-worker")
            return forge::detail::authored_inspection_worker();
        if (argc == 3 && std::string(argv[1]) == "--inspect-sdk") {
            std::cout << forge::detail::export_project_authoring(
                             std::filesystem::absolute(std::filesystem::u8path(argv[2])))
                             .dump()
                      << '\n';
            return 0;
        }
        forge::RuntimeConfig config;
        std::vector<forge::EngineModule> sdk_modules;
        std::optional<forge::ProjectSettings> sdk_project;
        forge::EngineServices bootstrap_services;
        std::optional<forge::AudioConfig> audio_config;
        std::filesystem::path project_root;
        std::filesystem::path user_data_base;
        bool explicit_hz = false, sdk_profile = false, ui_enabled = false;
        bool sdk_play = false;
        std::string audio_mode;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("Missing runtime argument value");
            const std::string value = argv[++i];
            if (option == "--sdk-project" || option == "--project") {
                project_root = std::filesystem::u8path(value);
                sdk_profile = option == "--sdk-project";
            } else if (option == "--ui") {
                if (value != "on" && value != "off")
                    throw std::runtime_error("UI mode must be on or off");
                ui_enabled = value == "on";
            } else if (option == "--audio") {
                if (value != "device" && value != "offline")
                    throw std::runtime_error("Audio mode must be device or offline");
                audio_mode = value;
            } else if (option == "--simulation-hz") {
                std::size_t consumed = 0;
                config.simulation_hz = std::stod(value, &consumed);
                explicit_hz = true;
                if (consumed != value.size())
                    throw std::runtime_error("Invalid simulation frequency");
            } else if (option == "--sdk-play") {
                if (value != "on" && value != "off")
                    throw std::runtime_error("--sdk-play mode must be on or off");
                sdk_play = value == "on";
            } else if (option == "--user-data") {
                user_data_base = std::filesystem::u8path(value);
            } else
                throw std::runtime_error("Unknown runtime option: " + option);
        }
        startup_phase("arguments-parsed");
        if (!project_root.empty() && sdk_profile) {
            sdk_project.emplace(project_root);
            startup_phase("project-settings-ready");
            if (!explicit_hz)
                config.simulation_hz = sdk_project->simulation_hz();
            sdk_modules = forge::project_native_modules(project_root, sdk_project->document(),
                                                        bootstrap_services.access());
            startup_phase("native-modules-ready");
        }
        if (sdk_play) {
            // Editor Play host path for exact-SDK projects. Requires
            // --sdk-project and an absolute --user-data directory supplied
            // by the editor. SDK candidate admission owns this world.
            if (!sdk_profile)
                throw std::runtime_error("--sdk-play requires --sdk-project <root>");
            if (project_root.empty() || !sdk_project)
                throw std::runtime_error("--sdk-play requires --sdk-project <root>");
            if (user_data_base.empty() || !user_data_base.is_absolute())
                throw std::runtime_error("--sdk-play requires --user-data <absolute directory>");
            if (!explicit_hz && sdk_project)
                config.simulation_hz = sdk_project->simulation_hz();
            if (!audio_mode.empty()) {
                audio_config =
                    forge::AudioConfig{project_root,
                                       audio_mode == "offline" ? forge::AudioOutput::Offline
                                                               : forge::AudioOutput::Device,
                                       false};
            }
            forge::Json defaults =
                sdk_project ? sdk_project->document().value("game", forge::Json::object())
                            : forge::Json::object();
            forge::SdkPlayRuntime::Config host_config;
            host_config.project = project_root;
            host_config.user_base = user_data_base;
            host_config.runtime = config;
            host_config.physics = sdk_project ? sdk_project->physics() : forge::PhysicsConfig{};
            host_config.modules = sdk_modules;
            host_config.defaults = std::move(defaults);
            host_config.audio = audio_config;
            host_config.ui = ui_enabled;
            host_config.input = sdk_project ? sdk_project->input() : forge::InputMap{};
            startup_phase("sdk-host-begin");
            forge::SdkPlayRuntime host(std::move(host_config));
            startup_phase("sdk-host-ready");
            startup_log.close();
            host.process();
            return 0;
        }
        if (!audio_mode.empty()) {
            if (project_root.empty())
                throw std::runtime_error("Audio requires --project or --sdk-project");
            audio_config = forge::AudioConfig{project_root,
                                              audio_mode == "offline" ? forge::AudioOutput::Offline
                                                                      : forge::AudioOutput::Device,
                                              false};
        }
        if (ui_enabled && project_root.empty())
            throw std::runtime_error("UI requires a project root");
        if (audio_config) {
            const auto project = forge::ProjectSettings(project_root);
            if (project.document().contains("game")) {
                const auto& audio = project.document().at("game").at("audio");
                audio_config->master_volume = audio.at("master_volume").get<float>();
                audio_config->channel_volumes = forge::audio_channel_volumes(audio);
            }
        }
        forge::PhysicsConfig physics_config =
            sdk_project ? sdk_project->physics() : forge::PhysicsConfig{};
        forge::RuntimeClock clock(config);
        using Runtime = forge::RuntimeWorld;
        startup_phase("world-begin");
        auto runtime = std::make_unique<Runtime>(sdk_modules, physics_config, audio_config,
                                                 project_root, ui_enabled);
        startup_phase("world-ready");
        forge::InputMap input_map = sdk_project ? sdk_project->input() : forge::InputMap{};
        forge::RuntimeIo io;
        startup_phase("pipes-ready");
        std::random_device random;
        const std::string session = std::to_string(random()) + "-" + std::to_string(random()) +
                                    "-" + std::to_string(random()) + "-" + std::to_string(random());
        startup_phase("session-ready");
        bool startup_request_seen = false;
        bool startup_response_seen = false, replacement_seen = false;
        std::uint64_t last_id = 0, ui_generation = 1;
        forge::TileMapPlaySource tile_projection;
        forge::ui_protocol::CommandGate ui_commands;
        ui_commands.reset(session, ui_generation);
        bool initialized = false, quit = false, tick_failed = false;
        bool profiling_enabled = false;
        std::string activation = "none";
        std::uint64_t activation_generation = 0, activation_tick = 0;
        auto quit_deadline = forge::RuntimeClock::Time::max();
        auto integrity = [](const forge::Json& value) {
            std::uint64_t h = 14695981039346656037ull;
            for (unsigned char c : value.dump()) {
                h ^= c;
                h *= 1099511628211ull;
            }
            return std::to_string(h);
        };
        auto capture = [&] {
            if (!forge::animation_runtime(runtime->engine.world())->checkpoint_ready())
                return forge::Json(); // Incomplete or invalid bindings are not a checkpoint.
            auto tile_maps = forge::detail::checkpoint_runtime_tile_maps(runtime->engine.world());
            if (tile_maps.is_null())
                return forge::Json(); // A binding/edit has not reached its admitted boundary.
            forge::Json checkpoint = {{"version", 2},
                                      {"session", session},
                                      {"tick", clock.tick()},
                                      {"simulation_hz", clock.status().at("simulation_hz")},
                                      {"scene", runtime->scene.snapshot()},
                                      {"physics", runtime->physics()->checkpoint()}};
            checkpoint["tile_maps"] = std::move(tile_maps);
            checkpoint["animation"] =
                forge::animation_runtime(runtime->engine.world())->checkpoint();
            if (checkpoint["animation"].is_null())
                return forge::Json(); // Model bindings are still pending; no partial recovery.
            checkpoint["navigation"] = std::static_pointer_cast<forge::NavigationRuntime>(
                                           runtime->engine.world().services().navigation())
                                           ->checkpoint();
            checkpoint["integrity"] = integrity(checkpoint);
            if (checkpoint.dump().size() > 8 * 1024 * 1024)
                throw std::runtime_error("Recovery checkpoint exceeds 8 MiB");
            return checkpoint;
        };
        auto tick = [&](float dt) {
            try {
                runtime->simulation.tick(dt);
            } catch (...) {
                tick_failed = true;
                throw;
            }
            if (activation == "loaded_pending_first_tick") {
                activation = "active";
                activation_tick = clock.tick() + 1;
            }
            // Drain an already captured response between catch-up ticks. A slow
            // simulation batch must not restrict transport to one small pipe
            // quota per eight ticks. Commands still execute only after the batch;
            // no world mutation or new snapshot is interleaved with a fixed tick.
            io.flush_for(std::chrono::milliseconds(10));
        };
        while (!io.closed()) {
            clock.advance(forge::RuntimeClock::Clock::now(), tick);
            if (runtime->engine.services().available(forge::Capability::Audio))
                std::static_pointer_cast<forge::AudioRuntime>(runtime->engine.services().audio())
                    ->poll_loading();
            io.flush_for(std::chrono::milliseconds(10));
            if (quit && (!io.pending() || forge::RuntimeClock::Clock::now() >= quit_deadline))
                break;
            std::string line;
            if (!quit && !io.pending() && io.receive(line)) {
                if (!startup_request_seen) {
                    startup_phase("first-request-received");
                    startup_request_seen = true;
                }
                forge::Json response{{"protocol", 2}, {"session", session}, {"id", 0}};
                bool trace_replacement = false;
                const auto replacement_phase = [&](const char* phase) {
                    if (trace_replacement)
                        startup_phase(phase);
                };
                try {
                    const auto request = forge::Json::parse(line);
                    response["id"] = request.value("id", forge::Json(0));
                    if (request.value("protocol", 0) != 2)
                        throw std::runtime_error(
                            "Unsupported protocol; runtime requires protocol 2 (fixed ticks)");
                    if (!request.contains("id") || !request.at("id").is_number_unsigned())
                        throw std::runtime_error("Positive unsigned request ID required");
                    const auto id = request.at("id").get<std::uint64_t>();
                    const auto command = request.at("command").get<std::string>();
                    if (id <= last_id)
                        throw std::runtime_error("Stale or repeated request ID");
                    if (command == "hello") {
                        if (initialized)
                            throw std::runtime_error("Session already initialized");
                        auto next_config = config;
                        if (!sdk_project && !explicit_hz)
                            next_config.simulation_hz =
                                request.value("simulation_hz", config.simulation_hz);
                        next_config.validate();
                        forge::InputMap input(
                            request.value("input_map", sdk_project ? sdk_project->input().source()
                                                                   : forge::InputMap{}.source()));
                        input_map = std::move(input);
                        runtime->simulation.input().configure(input_map);
                        if (!sdk_project && request.contains("gravity"))
                            physics_config.gravity = request.at("gravity").get<forge::Double3>();
                        physics_config.validate();
                        runtime->physics()->configure(physics_config);
                        clock = forge::RuntimeClock(next_config);
                        initialized = true;
                        response["runtime_contract"] = {
                            {"profile", FORGE_NATIVE_SDK_PROFILE},
                            {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                            {"source_commit", forge::source_commit},
                            {"sdk_project", sdk_profile},
                            {"tile_projection", forge::tile_map_play_protocol}};
                    } else if (!initialized || request.value("session", "") != session)
                        throw std::runtime_error("Stale or missing runtime session");
                    last_id = id; // Consume valid-session attempts, including rejected controls.
                    if (tick_failed && command != "replace" && command != "quit")
                        throw std::runtime_error("Runtime fixed tick failed; restore a valid "
                                                 "checkpoint or start clean Play");
                    (void)forge::cpu_profile_watch(request);
                    if (request.contains("seconds"))
                        throw std::runtime_error(
                            "Caller delta is unsupported; Step advances one fixed tick");
                    if (command == "step" && !clock.paused())
                        throw std::runtime_error("Single Step requires Pause");
                    if (request.contains("input_events")) {
                        if (command != "snapshot" && command != "pause" && command != "resume" &&
                            command != "step")
                            throw std::runtime_error(
                                "Input events require a snapshot or clock control request");
                        runtime->simulation.input().submit(
                            request.at("input_events").get<std::vector<forge::InputEvent>>());
                    }
                    if (command == "profiling") {
                        profiling_enabled = forge::control_cpu_profile(
                            request, runtime->engine.services(), ui_generation);
                    } else if (command == "replace") {
                        if (!clock.paused())
                            throw std::runtime_error("Pause before replacing runtime content");
                        trace_replacement = !replacement_seen && startup_log.is_open();
                        replacement_seen = true;
                        replacement_phase("replace-world-begin");
                        auto candidate = std::make_unique<Runtime>(
                            sdk_modules, physics_config, audio_config, project_root, ui_enabled);
                        replacement_phase("replace-world-ready");
                        candidate->simulation.input().configure(input_map);
                        std::uint64_t recovered_tick = 0;
                        replacement_phase("replace-scene-begin");
                        if (request.contains("recovery") && !request.at("recovery").is_null()) {
                            auto recovery = request.at("recovery");
                            if (recovery.dump().size() > 8 * 1024 * 1024)
                                throw std::runtime_error("Recovery checkpoint exceeds 8 MiB");
                            auto checksum = recovery.at("integrity").get<std::string>();
                            recovery.erase("integrity");
                            if (integrity(recovery) != checksum || recovery.at("version") != 2 ||
                                recovery.at("simulation_hz") !=
                                    clock.status().at("simulation_hz") ||
                                recovery.at("scene") != request.at("scene") ||
                                recovery.at("session") != request.at("recovery_session") ||
                                recovery.at("tick") != request.at("recovery_tick") ||
                                recovery.at("tick") != recovery.at("physics").at("tick"))
                                throw std::runtime_error("Recovery checkpoint integrity, identity, "
                                                         "session or boundary mismatch");
                            candidate->scene.restore_snapshot(recovery.at("scene"));
                            candidate->engine.world().modules().scene_ready();
                            replacement_phase("replace-scene-ready");
                            replacement_phase("replace-maps-begin");
                            prepare_tile_maps(*candidate, &recovery.at("tile_maps"));
                            forge::detail::restore_runtime_tile_maps(candidate->engine.world(),
                                                                     recovery.at("tile_maps"));
                            replacement_phase("replace-maps-ready");
                            replacement_phase("replace-physics-begin");
                            prepare_physics(*candidate->physics());
                            candidate->physics()->restore(recovery.at("physics"));
                            replacement_phase("replace-physics-ready");
                            auto animation = forge::animation_runtime(candidate->engine.world());
                            if (recovery.contains("animation"))
                                animation->restore(recovery.at("animation"));
                            else
                                animation->restore(
                                    {{"version", 1}, {"entries", forge::Json::array()}});
                            std::static_pointer_cast<forge::NavigationRuntime>(
                                candidate->engine.world().services().navigation())
                                ->restore(recovery.value(
                                    "navigation", forge::Json{{"version", 1},
                                                              {"assets", forge::Json::array()},
                                                              {"agents", forge::Json::array()}}));
                            recovered_tick = recovery.at("tick").get<std::uint64_t>();
                        } else {
                            candidate->scene.restore_snapshot(request.at("scene"));
                            candidate->engine.world().modules().scene_ready();
                            replacement_phase("replace-scene-ready");
                            replacement_phase("replace-maps-begin");
                            prepare_tile_maps(*candidate);
                            replacement_phase("replace-maps-ready");
                            replacement_phase("replace-physics-begin");
                            prepare_physics(*candidate->physics());
                            candidate->physics()->synchronize(0);
                            replacement_phase("replace-physics-ready");
                        }
                        candidate->simulation.restore_input_tick(recovered_tick);
                        candidate->simulation.reset_presentation();
                        replacement_phase("replace-audio-begin");
                        candidate->simulation.sync_audio();
                        if (candidate->engine.services().available(forge::Capability::Audio)) {
                            auto audio = std::static_pointer_cast<forge::AudioRuntime>(
                                candidate->engine.services().audio());
                            if (!audio->wait_for_sources(std::chrono::seconds(30)))
                                throw std::runtime_error(
                                    "Scene preparation: audio failed or timed out");
                        }
                        replacement_phase("replace-audio-ready");
                        // Publish only a complete validated reconstruction.
                        runtime.swap(candidate);
                        runtime->engine.services().profiling(profiling_enabled);
                        clock.restore_tick(recovered_tick, forge::RuntimeClock::Clock::now());
                        ui_commands.reset(session, ++ui_generation);
                        replacement_phase("replace-published");
                    } else if (command == "refresh_model_assets") {
                        auto animation = forge::animation_runtime(runtime->engine.world());
                        if (!animation || project_root.empty())
                            throw std::runtime_error(
                                "Model asset refresh requires a project runtime");
                        animation->refresh_assets();
                        runtime->physics()->refresh_assets();
                        if (runtime->engine.services().available(forge::Capability::Resources))
                            runtime->engine.services().resources()->refresh();
                    } else if (command == "audio_mix") {
                        const auto mix = forge::AudioMixValues::parse(request.at("mix"));
                        if (!audio_config ||
                            !runtime->engine.services().available(forge::Capability::Audio))
                            throw std::runtime_error("Live audio mix requires enabled audio");
                        std::static_pointer_cast<forge::AudioRuntime>(
                            runtime->engine.services().audio())
                            ->mix_volumes(mix.master, mix.channels);
                        audio_config->master_volume = mix.master;
                        audio_config->channel_volumes = mix.channels;
                    } else if (command == "audio_source") {
                        if (!runtime->engine.services().available(forge::Capability::Audio))
                            throw std::runtime_error("Source playback requires enabled audio");
                        const auto operation = forge::AudioSourceAccess::operation(request);
                        auto audio = std::static_pointer_cast<forge::AudioRuntime>(
                            runtime->engine.services().audio());
                        forge::AudioSourceAccess::control(
                            *audio, request.at("source").get<forge::EntityRef>(), operation);
                    } else if (command == "animation_playback") {
                        if (!runtime->engine.services().available(forge::Capability::Animation))
                            throw std::runtime_error(
                                "Animation playback requires enabled animation");
                        forge::AnimationPlaybackAccess::control(
                            *std::static_pointer_cast<forge::AnimationRuntime>(
                                runtime->engine.services().animation()),
                            request.at("source").get<forge::EntityRef>(), request);
                        runtime->simulation.sync_animation_pose();
                    } else if (command == "property_tune") {
                        const auto target = forge::RuntimePropertyTune::parse(
                            request, ui_generation, runtime->scene.asset_id());
                        forge::Json value;
                        if (forge::RuntimePropertyTune::material_type(target.type) ||
                            forge::RuntimePropertyTune::sprite_type(target.type)) {
                            value = runtime->scene.tune_runtime_property(
                                target.target.entity.str(), target.type, target.property,
                                request.at("value"));
                        } else if (forge::RuntimePropertyTune::particle_type(target.type)) {
                            value = forge::particle_runtime(runtime->engine.world())
                                        ->tune_appearance(target.target, target.property,
                                                          request.at("value"));
                        } else if (forge::RuntimePropertyTune::navigation_type(target.type)) {
                            if (!runtime->engine.services().available(
                                    forge::Capability::Navigation))
                                throw std::runtime_error(
                                    "Live avoidance tuning requires enabled navigation");
                            value = std::static_pointer_cast<forge::NavigationRuntime>(
                                        runtime->engine.services().navigation())
                                        ->tune_avoidance(target.target, target.property,
                                                         request.at("value"));
                        } else {
                            if (!forge::AnimationPlaybackAccess::rig_type(target.type) ||
                                !runtime->engine.services().available(forge::Capability::Animation))
                                throw std::runtime_error("Live property type is not authorable");
                            value = forge::AnimationPlaybackAccess::tune_rig(
                                *std::static_pointer_cast<forge::AnimationRuntime>(
                                    runtime->engine.services().animation()),
                                target.target, target.type, target.property, request.at("value"));
                            runtime->simulation.sync_animation_pose();
                        }
                        response["property_tune"] = {{"generation", ui_generation},
                                                     {"entity", target.target.entity},
                                                     {"type", target.type},
                                                     {"property", target.property},
                                                     {"value", value}};
                    } else if (command == "time_scale") {
                        if (!ui_generation || !request.contains("generation") ||
                            !request.at("generation").is_number_unsigned() ||
                            request.at("generation").get<std::uint64_t>() != ui_generation)
                            throw std::runtime_error(
                                "Simulation rate targets a stale world generation");
                        if (!request.contains("scale") || !request.at("scale").is_number())
                            throw std::runtime_error("Simulation time scale requires a number");
                        clock.set_time_scale(request.at("scale").get<double>());
                    } else if (command == "play" || command == "resume") {
                        if (clock.paused()) {
                            runtime->simulation.reset_presentation();
                            runtime->simulation.audio_paused(false);
                            clock.resume(forge::RuntimeClock::Clock::now());
                        }
                    } else if (command == "pause") {
                        runtime->simulation.audio_paused(true);
                        clock.pause(forge::RuntimeClock::Clock::now());
                    } else if (command == "step")
                        clock.step(tick);
                    else if (command == "ui") {
                        if (!ui_enabled)
                            throw std::runtime_error("Runtime UI is omitted from this composition");
                        auto ui = std::static_pointer_cast<forge::UiRuntime>(
                            runtime->engine.services().ui());
                        response["ui_ack"] =
                            ui_commands.dispatch(request.at("ui_command"), [&](const auto& cmd) {
                                ui->command(
                                    runtime->scene, cmd,
                                    [&](const std::string& control) {
                                        if (control == "Pause") {
                                            runtime->simulation.audio_paused(true);
                                            clock.pause(forge::RuntimeClock::Clock::now());
                                        } else if (control == "Resume") {
                                            if (clock.paused()) {
                                                runtime->simulation.reset_presentation();
                                                runtime->simulation.audio_paused(false);
                                                clock.resume(forge::RuntimeClock::Clock::now());
                                            }
                                        } else if (control == "Step")
                                            clock.step(tick);
                                    },
                                    [&](double scale) { clock.set_time_scale(scale); });
                            });
                    } else if (command == "save")
                        runtime->scene.save(request.at("path").get<std::string>());
                    else if (command == "quit") {
                        runtime->simulation.audio_paused(true);
                        clock.pause(forge::RuntimeClock::Clock::now());
                        quit = true;
                        quit_deadline = forge::RuntimeClock::Clock::now() + std::chrono::seconds(1);
                    } else if (forge::tile_map_play_command(command)) {
                        response["tile_projection"] =
                            forge::tile_map_play_dispatch(tile_projection, request);
                    } else if (command != "hello" && command != "snapshot" && command != "ping" &&
                               command != "schema")
                        throw std::runtime_error("Unknown command");
                    replacement_phase("replace-retired");
                    response["ok"] = true;
                    if (!forge::tile_map_play_command(command)) {
                        response["scene"] = runtime->scene.snapshot();
                        if (!tick_failed)
                            response["recovery"] = capture();
                        replacement_phase("replace-state-ready");
                        response["physics"] = runtime->physics()->status();
                        if (request.contains("physics_debug")) {
                            try {
                                const auto ref =
                                    request.at("physics_debug").get<forge::EntityRef>();
                                const auto c = runtime->physics()->character(ref);
                                response["physics"]["character_debug"] = {
                                    {"entity", ref.entity},
                                    {"ground", unsigned(c.ground)},
                                    {"velocity", c.velocity},
                                    {"ground_normal", c.ground_normal},
                                    {"ground_position", c.ground_position},
                                    {"ground_velocity", c.ground_velocity},
                                    {"crouched", c.crouched},
                                    {"shape_blocked", c.shape_change_blocked}};
                                if (c.supporting_entity)
                                    response["physics"]["character_debug"]["support"] =
                                        *c.supporting_entity;
                            } catch (const std::exception& e) {
                                response["physics"]["debug_diagnostic"] = e.what();
                            }
                        }
                        response["audio"] =
                            runtime->engine.services().available(forge::Capability::Audio)
                                ? std::static_pointer_cast<forge::AudioRuntime>(
                                      runtime->engine.services().audio())
                                      ->status()
                                : forge::Json{{"output", "disabled"}};
                        if (request.contains("audio_source_watch")) {
                            const auto& watched = request.at("audio_source_watch");
                            try {
                                if (!runtime->engine.services().available(forge::Capability::Audio))
                                    throw std::runtime_error("Audio output is disabled");
                                response["audio_source"] = forge::AudioSourceAccess::status(
                                    *std::static_pointer_cast<forge::AudioRuntime>(
                                        runtime->engine.services().audio()),
                                    watched.get<forge::EntityRef>());
                            } catch (const std::exception& error) {
                                response["audio_source"] = {{"source", watched},
                                                            {"available", false},
                                                            {"error", error.what()}};
                            }
                        }
                        if (request.contains("animation_watch")) {
                            const auto& watched = request.at("animation_watch");
                            try {
                                if (!runtime->engine.services().available(
                                        forge::Capability::Animation))
                                    throw std::runtime_error("Animation is disabled");
                                response["animation_playback"] =
                                    forge::AnimationPlaybackAccess::status(
                                        *std::static_pointer_cast<forge::AnimationRuntime>(
                                            runtime->engine.services().animation()),
                                        watched.get<forge::EntityRef>());
                            } catch (const std::exception& error) {
                                response["animation_playback"] = {{"source", watched},
                                                                  {"available", false},
                                                                  {"error", error.what()}};
                            }
                        }
                        response["effective_scene"] =
                            runtime->simulation.presentation(clock.alpha());
                        response["tile_maps"] = tile_projection.capture(
                            ui_generation, runtime->scene.asset_id(),
                            forge::tile_map_play_views(runtime->engine.world()));
                        response["schema"] = runtime->scene.schema();
                        response["input"] = runtime->simulation.input_status();
                        if (ui_enabled)
                            response["ui"] = std::static_pointer_cast<forge::UiRuntime>(
                                                 runtime->engine.services().ui())
                                                 ->snapshot(runtime->scene, session, ui_generation,
                                                            clock.tick(), clock.paused());
                        response["diagnostics"] = runtime->engine.services().diagnostics();
                        if (forge::cpu_profile_watch(request) || command == "profiling")
                            response["cpu_profile"] = forge::observed_cpu_profile(
                                runtime->engine.services(), ui_generation);
                    }
                } catch (const std::exception& error) {
                    response["ok"] = false;
                    response["error"] = error.what();
                    forge::Diagnostic diagnostic{
                        forge::Severity::Error, "runtime", error.what(), {}};
                    if (const auto* physics =
                            dynamic_cast<const forge::PhysicsConfigurationError*>(&error))
                        diagnostic = physics->diagnostic;
                    diagnostic.context.tick = clock.tick();
                    diagnostic.context.session = session;
                    if (!diagnostic.context.asset)
                        diagnostic.context.asset = runtime->scene.asset_id();
                    runtime->engine.services().emit(diagnostic);
                    response["diagnostic"] = forge::diagnostic_json(diagnostic);
                }
                response["timing"] = clock.status();
                response["world_generation"] = ui_generation;
                response["activation"] = {{"state", activation},
                                          {"generation", activation_generation},
                                          {"tick", activation_tick}};
                auto bytes = response.dump() + "\n";
                if (bytes.size() > forge::RuntimeIo::limit) {
                    response = {{"protocol", 2},
                                {"session", session},
                                {"id", response["id"]},
                                {"ok", false},
                                {"error", "Runtime response exceeds 16 MiB"}};
                    bytes = response.dump() + "\n";
                }
                io.send(std::move(bytes));
                if (initialized && !startup_response_seen) {
                    startup_phase("hello-response-queued");
                    startup_response_seen = true;
                }
                if (trace_replacement) {
                    replacement_phase("replace-response-queued");
                    startup_log.close();
                }
            }
            // Polling quantum only: steady_clock elapsed, never this sleep, drives ticks.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch (const std::exception& error) {
        std::cerr << "Runtime stopped: " << error.what() << '\n';
        return 1;
    }
}
