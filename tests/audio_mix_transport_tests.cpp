#include "editor/play.hpp"
#include <SDL3/SDL.h>
#include <forge/assets.hpp>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <fstream>
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
        {
            std::ofstream out(root / "tone.wav", std::ios::binary);
            auto word = [&](std::uint32_t value, unsigned bytes) {
                for (unsigned i = 0; i < bytes; ++i)
                    out.put(char(value >> (i * 8)));
            };
            out.write("RIFF", 4);
            word(36 + 96000, 4);
            out.write("WAVEfmt ", 8);
            word(16, 4);
            word(1, 2);
            word(1, 2);
            word(48000, 4);
            word(96000, 4);
            word(2, 2);
            word(16, 2);
            out.write("data", 4);
            word(96000, 4);
            for (unsigned i = 0; i < 48000; ++i)
                word(std::uint16_t(std::int16_t(2000 * std::sin(i * .06))), 2);
        }
        const auto clip = AssetCatalog::register_audio_clip(root, "tone.wav");
        const EntityRef source{AssetId::generate(), EntityId::generate()};
        const Json authored{{"version", 3},
                            {"asset_id", source.scene},
                            {"entities", Json::array({{{"id", source.entity},
                                                       {"name", "Speaker"},
                                                       {"components",
                                                        {{"forge.audio_source",
                                                          {{"clip", clip.id},
                                                           {"play_on_start", true},
                                                           {"loop", true},
                                                           {"gain", 1},
                                                           {"pitch", 1},
                                                           {"spatialized", false},
                                                           {"minimum_distance", 1},
                                                           {"maximum_distance", 100}}}}}}})}};
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
                require(done(), "Audio mix transport timeout: " + play.status() + " sdk=" +
                                    std::to_string(sdk) + " ready=" + std::to_string(play.ready()) +
                                    " audio=" + play.audio_status().dump() +
                                    " source=" + play.audio_source_status().dump() + " " +
                                    play.audio_source_error() + " " + play.log());
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
            play.watch_audio_source(source);
            wait([&] {
                return play.audio_source_status().is_object() &&
                       play.audio_source_status().value("available", false);
            });
            require(play.audio_source_status().at("playing") == true,
                    "Source watch did not observe actual autoplay voice");
            require(play.submit_audio_source(false) && play.audio_source_pending(),
                    "Manual source Stop was not queued");
            require(!play.submit_audio_mix(mix), "Source control allowed a competing mix request");
            wait([&] {
                return !play.audio_source_pending() &&
                       play.audio_source_status().at("playing") == false;
            });
            require(play.submit_audio_source(true), "Manual source restart was not queued");
            wait([&] {
                return !play.audio_source_pending() &&
                       play.audio_source_status().at("playing") == true;
            });
            require(play.timing().at("tick") == tick && play.snapshot() == before &&
                        play.audio_source_status().at("paused") == true,
                    "Manual source controls advanced paused game or changed authored data");
            require(play.submit_audio_source(AudioSourceOperation::Pause),
                    "Source pause was not queued");
            require(!play.submit_audio_source(AudioSourceOperation::Resume),
                    "Pending source pause admitted a duplicate");
            wait([&] {
                return !play.audio_source_pending() &&
                       play.audio_source_status().value("source_paused", false);
            });
            require(!play.audio_source_status().at("playing").get<bool>(),
                    "Source pause retained playing state");
            require(play.submit_audio_source(AudioSourceOperation::Resume),
                    "Source resume was not queued");
            wait([&] {
                return !play.audio_source_pending() &&
                       play.audio_source_status().value("playing", false);
            });
            require(!play.audio_source_status().at("source_paused").get<bool>() &&
                        play.timing().at("tick") == tick && play.snapshot() == before,
                    "Source pause/resume changed game clock or authored data");
            const EntityRef missing{source.scene, EntityId::generate()};
            play.watch_audio_source(missing);
            wait([&] {
                return play.control_ready() && play.audio_source_status().is_object() &&
                       !play.audio_source_status().value("available", true);
            });
            require(play.submit_audio_source(false), "Stale source rejection was not exercised");
            wait([&] { return play.control_ready() && !play.audio_source_error().empty(); });
            require(play.active() && play.audio_status().at("voices") == 1,
                    "Rejected source control destroyed the healthy session");
            play.watch_audio_source(source);
            wait([&] {
                return play.control_ready() && play.audio_source_status().is_object() &&
                       play.audio_source_status().value("available", false);
            });
            require(play.submit_audio_source(true), "Source rejection prevented valid retry");
            wait([&] {
                return !play.audio_source_pending() &&
                       play.audio_source_status().at("playing") == true;
            });
            play.stop();
            require(play.audio_status().empty() && !play.audio_mix_pending() &&
                        play.audio_mix_error().empty() && play.audio_source_status().is_null() &&
                        play.audio_source_error().empty(),
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
