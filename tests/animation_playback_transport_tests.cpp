#include "editor/play.hpp"
#include <SDL3/SDL.h>
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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need runtime and prepared original model project");
        require(SDL_Init(0), "SDL initialization failed");
        const auto root = std::filesystem::absolute(argv[2]);
        auto settings = ProjectSettings::defaults("Animation playback transport");
        settings["game"] = default_game_settings("org.forge.animation-playback-test",
                                                 "Animation playback transport");
        atomic_write(root / "forge.project.json", settings.dump(2));
        std::ifstream file(root / "main.scene.json");
        const auto authored = Json::parse(file);
        EntityRef ref;
        for (const auto& row : authored.at("entities"))
            if (row.at("components").contains("forge.animator"))
                ref = {authored.at("asset_id").get<AssetId>(), row.at("id").get<EntityId>()};
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "user-data");
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 15000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    // Legacy probe Play starts paused, before asynchronous model adoption.
                    // Use its ordinary Step path until this actual player is admitted.
                    if (!sdk && play.animation_status().is_object() &&
                        !play.animation_status().value("available", false) &&
                        play.recovery().is_null() && play.control_ready())
                        play.step();
                    SDL_Delay(1);
                }
                require(done(), "Animation transport timeout sdk=" + std::to_string(sdk) + ": " +
                                    play.status() + " " + play.animation_status().dump() + " " +
                                    play.animation_error() + " " + play.log());
            };
            require(!play.submit_animation({{"operation", "seek"}, {"position_seconds", .5}}),
                    "Stopped seek queued");
            play.start(argv[1], authored, true);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                require(play.submit_sdk_candidate_ack(true, play.session(),
                                                      play.sdk_candidate_ticket()),
                        "Animation SDK candidate rejected");
            }
            wait([&] { return play.control_ready(); });
            play.watch_animation(ref);
            wait([&] {
                return play.animation_status().is_object() &&
                       play.animation_status().value("available", false);
            });
            const auto before = play.snapshot(), tick = play.timing().at("tick");
            auto command = [&](const Json& value) {
                require(play.submit_animation(value) && play.animation_pending(),
                        "Playback did not queue");
                require(!play.submit_animation(value), "Duplicate playback command queued");
                wait([&] { return !play.animation_pending() && play.control_ready(); });
                require(play.animation_error().empty(),
                        "Playback rejected: " + play.animation_error());
                require(play.timing().at("tick") == tick && play.paused(),
                        "Playback advanced paused game time");
            };
            command({{"operation", "pause"}});
            command({{"operation", "seek"}, {"position_seconds", .75}});
            require(!play.animation_status().at("playing").get<bool>() &&
                        play.animation_status().at("time") == .75 && play.snapshot() == before,
                    "Seek lost pause or moved root across jump");
            require(!play.submit_animation({{"operation", "seek"}, {"position_seconds", 1.1}}),
                    "Invalid seek queued");
            command({{"operation", "play"}});
            require(play.animation_status().at("playing").get<bool>(),
                    "Clip did not resume intent");
            command({{"operation", "seek"}, {"position_seconds", 1.}});
            require(play.animation_status().at("time") == 0, "Loop end did not map to zero");
            command({{"operation", "restart"}});
            require(play.snapshot() == before, "Restart moved root across jump");
            const EntityRef missing{ref.scene, EntityId::generate()};
            play.watch_animation(missing);
            wait([&] {
                return play.animation_status().is_object() &&
                       !play.animation_status().value("available", true);
            });
            require(!play.submit_animation({{"operation", "play"}}), "Stale player queued control");
            play.watch_animation(ref);
            wait([&] {
                return play.animation_status().is_object() &&
                       play.animation_status().value("available", false);
            });
            play.stop();
            require(!play.animation_pending() && play.animation_status().is_null(),
                    "Stop retained playback watch");
        }
        std::ifstream reopened(root / "main.scene.json");
        require(Json::parse(reopened) == authored, "Play edited saved scene");
        SDL_Quit();
        std::cout << "Real editor legacy/shared Play seeking and pause/restart retain game time "
                     "and saved source\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
