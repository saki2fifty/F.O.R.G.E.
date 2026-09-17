#include "runtime_io.hpp"
#include <forge/build.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <random>
#include <thread>
int main(int argc, char** argv) {
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
        if (argc == 3 && std::string(argv[1]) == "--simulation-hz") {
            std::size_t consumed = 0;
            config.simulation_hz = std::stod(argv[2], &consumed);
            if (consumed != std::string(argv[2]).size())
                throw std::runtime_error("Invalid simulation frequency");
        } else if (argc != 1)
            throw std::runtime_error("Usage: forge_runtime [--simulation-hz 1..240]");
        forge::RuntimeClock clock(config);
        forge::Module module; // Code outlives all systems, scene content and the world.
        forge::EngineContext engine(forge::WorldRole::Runtime);
        forge::Scene scene(engine.world());
        forge::RuntimeSimulation simulation(engine.world(), scene, module);
        forge::RuntimeIo io;
        std::random_device random;
        const std::string session = std::to_string(random()) + "-" + std::to_string(random()) +
                                    "-" + std::to_string(random()) + "-" + std::to_string(random());
        std::uint64_t last_id = 0;
        bool initialized = false, quit = false;
        std::string activation = "none";
        std::uint64_t activation_generation = 0, activation_tick = 0;
        auto quit_deadline = forge::RuntimeClock::Time::max();
        auto tick = [&](float dt) {
            simulation.tick(dt);
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
                        initialized = true;
                    } else if (!initialized || request.value("session", "") != session)
                        throw std::runtime_error("Stale or missing runtime session");
                    last_id = id; // Consume valid-session attempts, including rejected controls.
                    if (request.contains("seconds"))
                        throw std::runtime_error(
                            "Caller delta is unsupported; Step advances one fixed tick");
                    if (command == "replace") {
                        if (!clock.paused())
                            throw std::runtime_error("Pause before replacing runtime content");
                        scene.replace(request.at("scene"));
                        simulation.reset_presentation();
                    } else if (command == "play" || command == "resume") {
                        if (clock.paused()) {
                            simulation.reset_presentation();
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
                        activation = "loaded_pending_first_tick";
                        ++activation_generation;
                        activation_tick = 0;
                        simulation.reset_presentation();
                    } else if (command == "save")
                        scene.save(request.at("path").get<std::string>());
                    else if (command == "quit") {
                        clock.pause(forge::RuntimeClock::Clock::now());
                        quit = true;
                        quit_deadline = forge::RuntimeClock::Clock::now() + std::chrono::seconds(1);
                    } else if (command != "hello" && command != "snapshot" && command != "ping" &&
                               command != "schema")
                        throw std::runtime_error("Unknown command");
                    response["ok"] = true;
                    response["module"] = module.id();
                    response["scene"] = scene.document(); // Uninterpolated recovery state only.
                    response["effective_scene"] = simulation.presentation(clock.alpha());
                    response["schema"] = scene.schema();
                } catch (const std::exception& error) {
                    response["ok"] = false;
                    response["error"] = error.what();
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
