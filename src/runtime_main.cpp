#include "authored_inspection.hpp"
#include "runtime_io.hpp"
#include "sdk_play_runtime.hpp"
#include <forge/build.hpp>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/runtime_world.hpp>
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
        if (!project_root.empty() && sdk_profile) {
            sdk_project.emplace(project_root);
            if (!explicit_hz)
                config.simulation_hz = sdk_project->simulation_hz();
            sdk_modules = forge::project_native_modules(project_root, sdk_project->document(),
                                                        bootstrap_services.access());
        }
        if (sdk_play) {
            // Opt-in Editor Play host path. Dispatches before any legacy
            // RuntimeWorld construction so the ABI1 process loop is never
            // entered. Requires both --sdk-project (already parsed) and an
            // absolute --user-data directory supplied by the editor. The
            // --sdk-project flag already gates legacy consumers; the
            // editor must opt into the SDK profile to reach this branch.
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
            forge::SdkPlayRuntime host(std::move(host_config));
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
        forge::PhysicsConfig physics_config =
            sdk_project ? sdk_project->physics() : forge::PhysicsConfig{};
        forge::RuntimeClock clock(config);
        forge::Module module; // Code outlives all systems, scene content and the world.
        using Runtime = forge::RuntimeWorld;
        auto runtime = std::make_unique<Runtime>(module, sdk_modules, physics_config, audio_config,
                                                 project_root, ui_enabled);
        forge::InputMap input_map = sdk_project ? sdk_project->input() : forge::InputMap{};
        forge::RuntimeIo io;
        std::random_device random;
        const std::string session = std::to_string(random()) + "-" + std::to_string(random()) +
                                    "-" + std::to_string(random()) + "-" + std::to_string(random());
        std::uint64_t last_id = 0, ui_generation = 1;
        forge::ui_protocol::CommandGate ui_commands;
        ui_commands.reset(session, ui_generation);
        bool initialized = false, quit = false, tick_failed = false;
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
            forge::Json checkpoint = {{"version", 1},
                                      {"session", session},
                                      {"tick", clock.tick()},
                                      {"simulation_hz", clock.status().at("simulation_hz")},
                                      {"scene", runtime->scene.snapshot()},
                                      {"physics", runtime->physics()->checkpoint()}};
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
            io.flush();
        };
        while (!io.closed()) {
            clock.advance(forge::RuntimeClock::Clock::now(), tick);
            io.flush();
            if (quit && (!io.pending() || forge::RuntimeClock::Clock::now() >= quit_deadline))
                break;
            std::string line;
            if (!quit && !io.pending() && io.receive(line)) {
                forge::Json response{{"protocol", 2}, {"session", session}, {"id", 0}};
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
                            {"sdk_project", sdk_profile}};
                    } else if (!initialized || request.value("session", "") != session)
                        throw std::runtime_error("Stale or missing runtime session");
                    last_id = id; // Consume valid-session attempts, including rejected controls.
                    if (tick_failed && command != "replace" && command != "quit")
                        throw std::runtime_error("Runtime fixed tick failed; restore a valid "
                                                 "checkpoint or start clean Play");
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
                    if (command == "replace") {
                        if (!clock.paused())
                            throw std::runtime_error("Pause before replacing runtime content");
                        auto candidate =
                            std::make_unique<Runtime>(module, sdk_modules, physics_config,
                                                      audio_config, project_root, ui_enabled);
                        candidate->simulation.input().configure(input_map);
                        std::uint64_t recovered_tick = 0;
                        if (request.contains("recovery") && !request.at("recovery").is_null()) {
                            auto recovery = request.at("recovery");
                            if (recovery.dump().size() > 8 * 1024 * 1024)
                                throw std::runtime_error("Recovery checkpoint exceeds 8 MiB");
                            auto checksum = recovery.at("integrity").get<std::string>();
                            recovery.erase("integrity");
                            if (integrity(recovery) != checksum || recovery.at("version") != 1 ||
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
                            prepare_physics(*candidate->physics());
                            candidate->physics()->restore(recovery.at("physics"));
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
                            prepare_physics(*candidate->physics());
                            candidate->physics()->synchronize(0);
                        }
                        candidate->simulation.restore_input_tick(recovered_tick);
                        candidate->simulation.reset_presentation();
                        candidate->simulation.sync_audio();
                        runtime.swap(
                            candidate); // Publish only a complete validated reconstruction.
                        clock.restore_tick(recovered_tick, forge::RuntimeClock::Clock::now());
                        ui_commands.reset(session, ++ui_generation);
                    } else if (command == "refresh_model_assets") {
                        auto animation = forge::animation_runtime(runtime->engine.world());
                        if (!animation || project_root.empty())
                            throw std::runtime_error(
                                "Model asset refresh requires a project runtime");
                        animation->refresh_assets();
                        runtime->physics()->refresh_assets();
                        if (runtime->engine.services().available(forge::Capability::Resources))
                            runtime->engine.services().resources()->refresh();
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
                    else if (command == "load_module") {
                        if (!clock.paused())
                            throw std::runtime_error(
                                "Pause and checkpoint before module replacement");
                        module.load(request.at("path").get<std::string>());
                        runtime->simulation.input().release_all();
                        activation = "loaded_pending_first_tick";
                        ++activation_generation;
                        ui_commands.reset(session, ++ui_generation);
                        activation_tick = 0;
                        runtime->simulation.reset_presentation();
                    } else if (command == "ui") {
                        if (!ui_enabled)
                            throw std::runtime_error("Runtime UI is omitted from this composition");
                        auto ui = std::static_pointer_cast<forge::UiRuntime>(
                            runtime->engine.services().ui());
                        response["ui_ack"] =
                            ui_commands.dispatch(request.at("ui_command"), [&](const auto& cmd) {
                                ui->command(runtime->scene, cmd, [&](const std::string& control) {
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
                                });
                            });
                    } else if (command == "save")
                        runtime->scene.save(request.at("path").get<std::string>());
                    else if (command == "quit") {
                        runtime->simulation.audio_paused(true);
                        clock.pause(forge::RuntimeClock::Clock::now());
                        quit = true;
                        quit_deadline = forge::RuntimeClock::Clock::now() + std::chrono::seconds(1);
                    } else if (command != "hello" && command != "snapshot" && command != "ping" &&
                               command != "schema")
                        throw std::runtime_error("Unknown command");
                    response["ok"] = true;
                    response["module"] = module.id();
                    response["scene"] = runtime->scene.snapshot();
                    if (!tick_failed)
                        response["recovery"] = capture();
                    response["physics"] = runtime->physics()->status();
                    if (request.contains("physics_debug")) {
                        try {
                            const auto ref = request.at("physics_debug").get<forge::EntityRef>();
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
                    response["effective_scene"] = runtime->simulation.presentation(clock.alpha());
                    response["schema"] = runtime->scene.schema();
                    response["input"] = runtime->simulation.input_status();
                    if (ui_enabled)
                        response["ui"] = std::static_pointer_cast<forge::UiRuntime>(
                                             runtime->engine.services().ui())
                                             ->snapshot(runtime->scene, session, ui_generation,
                                                        clock.tick(), clock.paused());
                    response["diagnostics"] = runtime->engine.services().diagnostics();
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
            }
            // Polling quantum only: steady_clock elapsed, never this sleep, drives ticks.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch (const std::exception& error) {
        std::cerr << "Runtime stopped: " << error.what() << '\n';
        return 1;
    }
}
