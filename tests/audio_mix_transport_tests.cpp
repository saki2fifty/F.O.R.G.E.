#include "editor/play.hpp"
#include <SDL3/SDL.h>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
int rejecting_peer() {
    Json scene{{"version", 1}, {"entities", Json::array()}};
    Json mix{{"master_volume", 1}, {"channel_volumes", {1, 1, 1}}};
    bool reject = true;
    for (std::string line; std::getline(std::cin, line);) {
        const auto request = Json::parse(line);
        const auto command = request.at("command").get<std::string>();
        if (command == "replace")
            scene = request.at("scene");
        Json response{{"protocol", 2},
                      {"id", request.at("id")},
                      {"session", "mix-peer"},
                      {"ok", true},
                      {"scene", scene},
                      {"effective_scene", scene},
                      {"recovery", {{"scene", scene}}},
                      {"timing", {{"tick", 0}, {"paused", true}}},
                      {"audio", mix}};
        if (command == "audio_mix" && reject) {
            response = {{"protocol", 2},
                        {"id", request.at("id")},
                        {"session", "mix-peer"},
                        {"ok", false},
                        {"error", "fixture mix rejection"}};
            reject = false;
        } else if (command == "audio_mix")
            response["audio"] = mix = request.at("mix");
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
        require(argc == 3, "Need runtime and scratch roots");
        require(SDL_Init(0), "SDL initialization failed");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto settings = ProjectSettings::defaults("Live audio mix test");
        auto project = settings;
        project["game"] = default_game_settings("org.forge.audio-mix-test", "Live audio mix test");
        atomic_write(root / "forge.project.json", project.dump(2));
        const Json authored{{"version", 1}, {"entities", Json::array()}};
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "user-data");
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 10000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                require(done(), "Audio mix transport timeout: " + play.status() + " " + play.log());
            };
            const Json mix{{"master_volume", .25}, {"channel_volumes", {.5, 0, .75}}};
            require(!play.submit_audio_mix(mix), "Stopped Play accepted live tuning");
            play.start(argv[1], authored, true);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                require(play.submit_sdk_candidate_ack(true, play.session(),
                                                      play.sdk_candidate_ticket()),
                        "SDK candidate admission rejected");
            }
            wait([&] {
                return play.control_ready() && play.audio_status().contains("master_volume");
            });
            require(play.audio_status().at("master_volume") == 1,
                    "Play did not use saved defaults");
            const auto before = play.snapshot();
            const auto tick = play.timing().at("tick");
            require(play.submit_audio_mix(mix) && play.audio_mix_pending(), "Mix was not queued");
            require(!play.submit_audio_mix(mix), "Pending mix admitted a duplicate request");
            wait([&] {
                return !play.audio_mix_pending() && play.audio_status().at("master_volume") == .25;
            });
            require(play.audio_status().at("channel_volumes") == Json::array({.5, 0, .75}) &&
                        play.audio_status().at("paused") == true &&
                        play.timing().at("tick") == tick && play.snapshot() == before,
                    "Actual transport changed paused scene/clock or lost mixer values");
            play.stop();
            require(play.audio_status().empty() && !play.audio_mix_pending() &&
                        play.audio_mix_error().empty(),
                    "Play teardown retained live mix transport state");
            require(ProjectSettings(root).document() == project,
                    "Live tuning changed the saved project defaults");
        }
        {
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root);
            play.set_headless_audio(true);
            play.start(std::filesystem::absolute(argv[0]).string(), authored, true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 10000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                require(done(), "Rejected mix transport failed: " + play.status());
            };
            wait([&] { return play.control_ready(); });
            const Json mix{{"master_volume", .25}, {"channel_volumes", {.5, 0, .75}}};
            require(play.submit_audio_mix(mix), "Peer mix did not queue");
            wait([&] { return !play.audio_mix_error().empty() && play.control_ready(); });
            require(play.active() && play.audio_status().at("master_volume") == 1,
                    "Correlated mix rejection destroyed the healthy session or last status");
            require(play.submit_audio_mix(mix), "Rejected mix prevented retry");
            wait([&] {
                return !play.audio_mix_pending() && play.audio_status().at("master_volume") == .25;
            });
            require(play.audio_mix_error().empty(), "Successful retry retained stale rejection");
            play.stop();
        }
        std::filesystem::remove_all(root);
        SDL_Quit();
        std::cout
            << (shared ? "Legacy/SDK" : "Legacy")
            << " real Play transport live mix, paused clock, teardown and saved defaults passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
