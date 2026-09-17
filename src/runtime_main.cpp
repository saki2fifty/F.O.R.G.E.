#include "runtime_io.hpp"
#include <forge/build.hpp>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <random>
#include <thread>
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
        forge::RuntimeConfig config;
        std::vector<forge::EngineModule> sdk_modules;
        std::optional<forge::ProjectSettings> sdk_project;
        forge::EngineServices bootstrap_services;
        if (argc == 3 && std::string(argv[1]) == "--sdk-project") {
            sdk_project.emplace(std::filesystem::u8path(argv[2]));
            config.simulation_hz = sdk_project->simulation_hz();
            sdk_modules =
                forge::project_native_modules(std::filesystem::u8path(argv[2]),
                                              sdk_project->document(), bootstrap_services.access());
        } else if (argc == 3 && std::string(argv[1]) == "--simulation-hz") {
            std::size_t consumed = 0;
            config.simulation_hz = std::stod(argv[2], &consumed);
            if (consumed != std::string(argv[2]).size())
                throw std::runtime_error("Invalid simulation frequency");
        } else if (argc != 1)
            throw std::runtime_error(
                "Usage: forge_runtime [--simulation-hz 1..240 | --sdk-project PROJECT]");
        forge::PhysicsConfig physics_config =
            sdk_project ? sdk_project->physics() : forge::PhysicsConfig{};
        forge::RuntimeClock clock(config);
        forge::Module module; // Code outlives all systems, scene content and the world.
        struct Runtime {
            forge::EngineContext engine;
            forge::Scene scene;
            forge::RuntimeSimulation simulation;
            Runtime(forge::Module& m, std::vector<forge::EngineModule> modules,
                    forge::PhysicsConfig physics)
                : engine(forge::WorldRole::Runtime, false,
                         [&] {
                             modules.push_back(forge::physics_module(physics));
                             return std::move(modules);
                         }()),
                  scene(engine.world()), simulation(engine.world(), scene, m) {}
            std::shared_ptr<forge::PhysicsRuntime> physics() {
                return std::static_pointer_cast<forge::PhysicsRuntime>(engine.services().physics());
            }
        };
        auto runtime = std::make_unique<Runtime>(module, sdk_modules, physics_config);
        forge::InputMap input_map = sdk_project ? sdk_project->input() : forge::InputMap{};
        forge::RuntimeIo io;
        std::random_device random;
        const std::string session = std::to_string(random()) + "-" + std::to_string(random()) +
                                    "-" + std::to_string(random()) + "-" + std::to_string(random());
        std::uint64_t last_id = 0;
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
            forge::Json checkpoint = {{"version", 1},
                                      {"session", session},
                                      {"tick", clock.tick()},
                                      {"simulation_hz", clock.status().at("simulation_hz")},
                                      {"scene", runtime->scene.snapshot()},
                                      {"physics", runtime->physics()->checkpoint()}};
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
                        if (argc == 1)
                            next_config.simulation_hz =
                                request.value("simulation_hz", config.simulation_hz);
                        next_config.validate();
                        forge::InputMap input(
                            request.value("input_map", sdk_project ? sdk_project->input().source()
                                                                   : forge::InputMap{}.source()));
                        input_map = std::move(input);
                        runtime->simulation.input().configure(input_map);
                        if (argc == 1 && request.contains("gravity"))
                            physics_config.gravity = request.at("gravity").get<forge::Double3>();
                        physics_config.validate();
                        runtime->physics()->configure(physics_config);
                        clock = forge::RuntimeClock(next_config);
                        initialized = true;
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
                            std::make_unique<Runtime>(module, sdk_modules, physics_config);
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
                            candidate->physics()->restore(recovery.at("physics"));
                            recovered_tick = recovery.at("tick").get<std::uint64_t>();
                        } else {
                            candidate->scene.restore_snapshot(request.at("scene"));
                            candidate->physics()->synchronize(0);
                        }
                        candidate->simulation.restore_input_tick(recovered_tick);
                        candidate->simulation.reset_presentation();
                        runtime.swap(
                            candidate); // Publish only a complete validated reconstruction.
                        clock.restore_tick(recovered_tick, forge::RuntimeClock::Clock::now());
                    } else if (command == "play" || command == "resume") {
                        if (clock.paused()) {
                            runtime->simulation.reset_presentation();
                            clock.resume(forge::RuntimeClock::Clock::now());
                        }
                    } else if (command == "pause")
                        clock.pause(forge::RuntimeClock::Clock::now());
                    else if (command == "step")
                        clock.step(tick);
                    else if (command == "load_module") {
                        if (!clock.paused())
                            throw std::runtime_error(
                                "Pause and checkpoint before module replacement");
                        module.load(request.at("path").get<std::string>());
                        runtime->simulation.input().release_all();
                        activation = "loaded_pending_first_tick";
                        ++activation_generation;
                        activation_tick = 0;
                        runtime->simulation.reset_presentation();
                    } else if (command == "save")
                        runtime->scene.save(request.at("path").get<std::string>());
                    else if (command == "quit") {
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
                    response["effective_scene"] = runtime->simulation.presentation(clock.alpha());
                    response["schema"] = runtime->scene.schema();
                    response["input"] = runtime->simulation.input_status();
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
