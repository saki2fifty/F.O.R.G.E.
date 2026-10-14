#include "editor/play.hpp"
#include <SDL3/SDL.h>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need runtime and scratch root");
        check(SDL_Init(0), "SDL initialization failed");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        auto settings = ProjectSettings::defaults("Simulation speed transport");
        settings["game"] = default_game_settings("org.forge.rate-" + AssetId::generate().str(),
                                                 "Simulation speed");
        atomic_write(root / "forge.project.json", settings.dump(2));
        const Json authored{{"version", 1}, {"entities", Json::array()}};
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (const bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(100, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "users");
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 10000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                check(done(),
                      "Simulation speed transport timeout: " + play.status() + " " + play.log());
            };
            check(!play.set_time_scale(.5), "Stopped editor accepted rate");
            play.start(argv[1], authored);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                check(play.submit_editor_observation(false, "KeyboardMouse"),
                      "Initial uncaptured observation rejected");
                check(play.submit_sdk_candidate_ack(true, play.session(),
                                                    play.sdk_candidate_ticket()),
                      "Candidate ack failed");
            }
            wait([&] { return play.control_ready(); });
            check(play.timing().at("time_scale") == 1, "Fresh Play lost normal speed");
            play.pause();
            wait([&] { return play.paused() && play.control_ready(); });
            const auto saved = play.snapshot();
            const auto tick = play.timing().at("tick");
            for (double rate : {0., .25, .5, 1., 2., 4.}) {
                check(play.set_time_scale(rate) && play.time_scale_pending(), "Rate not queued");
                check(!play.set_time_scale(rate), "Pending request admitted duplicate");
                wait([&] {
                    return !play.time_scale_pending() && play.timing().at("time_scale") == rate;
                });
                check(play.snapshot() == saved && play.timing().at("tick") == tick,
                      "Paused rate changed scene or tick");
            }
            check(!play.set_time_scale(-1) && !play.set_time_scale(4.1),
                  "Editor accepted invalid rate");
            check(play.set_time_scale(0), "Hold rejected");
            wait([&] { return !play.time_scale_pending() && play.timing().at("time_scale") == 0; });
            play.resume();
            wait([&] { return !play.paused() && play.control_ready(); });
            const auto until = SDL_GetTicks() + 100;
            while (SDL_GetTicks() < until) {
                play.pump();
                SDL_Delay(1);
            }
            check(play.timing().at("tick") == tick, "Editor held motion advanced");
            wait([&] { return play.control_ready(); });
            check(play.set_time_scale(1), "Editor could not restore hold");
            wait([&] {
                return play.timing().at("tick").get<std::uint64_t>() > tick.get<std::uint64_t>() &&
                       play.control_ready();
            });
            play.pause();
            wait([&] { return play.paused() && play.control_ready(); });
            check(play.set_time_scale(0), "Paused hold rejected");
            wait([&] { return !play.time_scale_pending() && play.control_ready(); });
            const auto before_step = play.timing().at("tick").get<std::uint64_t>();
            play.step();
            wait([&] {
                return play.timing().at("tick") == before_step + 1 && play.control_ready();
            });
            check(play.paused() && play.timing().at("time_scale") == 0,
                  "Step changed rate or pause policy");
            play.stop();
            check(!play.time_scale_pending(), "Stop retained rate command");
            play.start(argv[1], authored);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                check(play.submit_editor_observation(false, "KeyboardMouse"),
                      "Restart uncaptured observation rejected");
                check(play.submit_sdk_candidate_ack(true, play.session(),
                                                    play.sdk_candidate_ticket()),
                      "Restart ack failed");
            }
            wait([&] { return play.control_ready(); });
            check(play.timing().at("time_scale") == 1,
                  "Transient rate leaked across Play processes");
            play.stop();
        }
        SDL_Quit();
        std::cout
            << "Editor normal/exact SDK rate transport, pending, hold, Step, restart passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
