#include "editor/play.hpp"
#include <SDL3/SDL.h>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const std::string& text) {
    if (!value)
        throw std::runtime_error(text);
}
int rejecting_peer() {
    Json scene{{"version", 1}, {"entities", Json::array()}};
    bool reject = true, enabled = false;
    for (std::string line; std::getline(std::cin, line);) {
        const auto request = Json::parse(line);
        const auto command = request.at("command").get<std::string>();
        if (command == "replace")
            scene = request.at("scene");
        Json response{{"protocol", 2},
                      {"id", request.at("id")},
                      {"session", "profile-peer"},
                      {"ok", true},
                      {"scene", scene},
                      {"effective_scene", scene},
                      {"recovery", {{"scene", scene}}},
                      {"timing", {{"tick", 0}, {"paused", true}}},
                      {"world_generation", 1}};
        if (command == "profiling" && reject) {
            response = {{"protocol", 2},
                        {"id", request.at("id")},
                        {"session", "profile-peer"},
                        {"ok", false},
                        {"error", "fixture profiling rejection"}};
            reject = false;
        } else {
            if (command == "profiling")
                enabled = request.at("enabled").get<bool>();
            if (request.value("profile_watch", false) || command == "profiling")
                response["cpu_profile"] = {{"available", true},
                                           {"compiled", true},
                                           {"enabled", enabled},
                                           {"world_generation", 1},
                                           {"samples", Json::array()}};
        }
        std::cout << response.dump() << '\n' << std::flush;
        if (command == "quit")
            return 0;
    }
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "--project")
        return rejecting_peer();
    try {
        check(argc == 3 && SDL_Init(0), "Runtime/scratch path and SDL initialization required");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        auto settings = ProjectSettings::defaults("CPU profiler transport test");
        settings["game"] =
            default_game_settings("org.forge.profile-transport", "CPU profiler test");
        atomic_write(root / "forge.project.json", settings.dump(2));
        const Json authored{{"version", 1}, {"entities", Json::array()}};
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "user-data");
            play.set_headless_audio(true);
            play.watch_cpu_profile(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 10000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                check(done(), "Profiler transport timeout: " + play.status() +
                                  " ready=" + std::to_string(play.ready()) +
                                  " controls=" + std::to_string(play.control_ready()) +
                                  " sdk=" + std::to_string(sdk) + " session=" + play.session() +
                                  " timing=" + play.timing().dump() +
                                  " cpu=" + play.cpu_profile().dump() + " " + play.log());
            };
            check(!play.submit_cpu_profile(true, false), "Stopped Play accepted collection");
            play.start(argv[1], authored, true);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                check(play.submit_sdk_candidate_ack(true, play.session(),
                                                    play.sdk_candidate_ticket()),
                      "SDK candidate admission failed");
            }
            wait([&] {
                return play.control_ready() && play.cpu_profile().value("available", false);
            });
            const auto scene = play.snapshot(), tick = play.timing().at("tick");
            check(!play.cpu_profile().at("enabled").get<bool>(),
                  "Default worker collection enabled");
            check(play.submit_cpu_profile(true, true) && play.cpu_profile_pending(),
                  "Collection not queued");
            check(!play.submit_cpu_profile(false, false),
                  "Pending collection admitted competing control");
            wait([&] {
                return !play.cpu_profile_pending() && play.cpu_profile().at("enabled") == true;
            });
            check(play.snapshot() == scene && play.timing().at("tick") == tick && play.paused(),
                  "Adapter collection changed paused scene/clock");
            wait([&] { return play.control_ready() && !play.cpu_profile().at("samples").empty(); });
            check(play.submit_cpu_profile(false, false), "Collection stop was rejected");
            wait([&] {
                return !play.cpu_profile_pending() && play.cpu_profile().at("enabled") == false;
            });
            const auto retained = play.cpu_profile().at("samples");
            for (int i = 0; i < 30; ++i) {
                play.pump();
                SDL_Delay(1);
            }
            check(play.cpu_profile().at("samples") == retained, "Stopped worker kept collecting");
            wait([&] { return play.control_ready(); });
            check(play.submit_cpu_profile(false, true), "Clear was rejected");
            wait([&] {
                return !play.cpu_profile_pending() && play.cpu_profile().at("samples").empty();
            });
            play.stop();
            check(play.cpu_profile().empty(), "Stopped session retained observations");
        }
        PlaySession peer;
        peer.configure(60, InputMap{}, {0, -9.81, 0}, root);
        peer.watch_cpu_profile(true);
        peer.start(argv[0], authored, true);
        auto wait_peer = [&](auto done) {
            const auto deadline = SDL_GetTicks() + 10000;
            while (!done() && peer.active() && SDL_GetTicks() < deadline) {
                peer.pump();
                SDL_Delay(1);
            }
            check(done(), "Rejecting profile peer timeout: " + peer.status());
        };
        wait_peer(
            [&] { return peer.control_ready() && peer.cpu_profile().value("available", false); });
        check(peer.submit_cpu_profile(true, false), "Peer control not queued");
        wait_peer([&] { return !peer.cpu_profile_error().empty() && peer.control_ready(); });
        check(peer.active() && peer.cpu_profile_error() == "fixture profiling rejection",
              "Rejected profiling command retired runtime or lost feedback");
        check(peer.submit_cpu_profile(true, false), "Rejected command prevented retry");
        wait_peer([&] {
            return !peer.cpu_profile_pending() && peer.cpu_profile().at("enabled") == true;
        });
        check(peer.cpu_profile_error().empty(), "Successful retry retained error");
        peer.stop();
        std::filesystem::remove_all(root);
        SDL_Quit();
        std::cout << "CPU profiler real adapter/process and rejection/retry passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
