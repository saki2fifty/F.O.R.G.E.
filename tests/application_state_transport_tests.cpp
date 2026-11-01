#include "application_state_fixture.hpp"
#include "editor/play.hpp"
#include <SDL3/SDL.h>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>

namespace {
using namespace forge;
using namespace std::chrono_literals;
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need runtime and scratch directory");
        check(SDL_Init(0), SDL_GetError());
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto authored = forge::test::application_state_fixture(root);
        std::filesystem::rename(root / "Assets", root / "Source offline");
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "users");
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 15000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                check(done(), "State transport timeout sdk=" + std::to_string(sdk) + ": " +
                                  play.status() + " " + play.log());
            };
            auto start = [&] {
                play.start(argv[1], authored, true);
                if (sdk) {
                    wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                    check(play.submit_sdk_candidate_ack(true, play.session(),
                                                        play.sdk_candidate_ticket()),
                          "State candidate ack failed");
                }
                wait([&] { return play.control_ready() && !play.application_states().is_null(); });
            };
            check(!play.submit_application_state("app", "playing", 1),
                  "Stopped state control queued");
            start();
            const auto tick = play.timing().at("tick"), saved = play.snapshot();
            check(play.paused() &&
                      play.application_states().at("domains")[0].at("active") == "menu",
                  "Initial state missing");
            check(!play.submit_application_state("Bad", "playing", 1),
                  "Invalid semantic key queued");
            auto command = [&](std::string domain, std::string state, std::uint64_t revision,
                               bool accepted) {
                check(play.submit_application_state(domain, state, revision) &&
                          play.application_state_pending(),
                      "State command did not queue");
                check(!play.submit_application_state(domain, state, revision),
                      "Duplicate state command queued");
                wait([&] { return !play.application_state_pending() && play.control_ready(); });
                check(play.application_state_error().empty() == accepted,
                      "Unexpected state response: " + play.application_state_error());
                check(play.snapshot() == saved && play.timing().at("tick") == tick && play.paused(),
                      "State command changed authored values or paused clock");
            };
            command("app", "playing", 1, true);
            auto observed = play.application_states();
            check(observed.at("domains")[0].at("active") == "playing" &&
                      observed.at("domains")[1].at("active") == "visible",
                  "State transition did not publish computed result");
            command("app", "menu", 1, false);
            check(play.application_states() == observed, "Stale request disturbed published state");
            command("hud", "hidden", observed.at("domains")[1].at("revision"), false);
            check(play.application_states() == observed, "Computed control disturbed state");
            command("app", "missing", 2, false);
            check(play.application_states() == observed, "Unknown state disturbed active World");
            command("app", "menu", 2, true);
            check(play.application_states().at("domains")[0].at("revision") == 3,
                  "Successful retry lost revision");
            play.stop();
            check(!play.application_state_pending() && play.application_states().is_null() &&
                      play.application_state_error().empty(),
                  "Stop retained state controls");
            start();
            check(play.application_states().at("domains")[0].at("revision") == 1 &&
                      play.application_states().at("domains")[0].at("active") == "menu",
                  "Restart retained runtime state");
            play.stop();
        }
        SDL_Quit();
        std::cout << "Real editor legacy/shared state controls, paused clock, source-free "
                     "admission, nonfatal rejection and restart passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
