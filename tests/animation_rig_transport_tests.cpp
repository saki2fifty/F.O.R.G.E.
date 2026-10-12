#include "editor/play.hpp"
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
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    Json value;
    file >> value;
    return value;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need runtime and prepared rig project");
        require(SDL_Init(0), "SDL initialization failed");
        const auto root = std::filesystem::absolute(argv[2]);
        const auto authored = read(root / "main.scene.json");
        auto settings = ProjectSettings::defaults("Rig live tuning");
        settings["game"] = default_game_settings("org.forge.rig-tuning", "Rig live tuning");
        atomic_write(root / "forge.project.json", settings.dump(2));
        std::string actor;
        for (const auto& row : authored.at("entities"))
            if (row.at("components").contains("forge.two_bone_ik"))
                actor = row.at("id");
        require(!actor.empty(), "Prepared scene has no reach actor");
        const bool shared = std::string_view(FORGE_NATIVE_SDK_PROFILE) == "shared-native-sdk";
        for (const bool sdk : {false, true}) {
            if (sdk && !shared)
                continue;
            PlaySession play;
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / (sdk ? "sdk-user-data" : "user-data"));
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 20000;
                while (!done() && play.active() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                require(done(), "Rig live transport timeout: " + play.status() + " " + play.log());
            };
            require(!play.submit_property_tune(actor, "forge.two_bone_ik", "target_x", .8),
                    "Stopped Play admitted rig tuning");
            // Normal Play keeps polling asynchronous model admission. The SDK
            // probe uses its real candidate/resource gate but has no desktop
            // cursor/epoch adapter; native fixtures cover that separate boundary.
            play.start(argv[1], authored, sdk);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                require(play.submit_sdk_candidate_ack(true, play.session(),
                                                      play.sdk_candidate_ticket()),
                        "Rig SDK candidate could not be admitted");
            }
            auto row = [&]() -> Json {
                for (const auto& item : play.effective_snapshot().at("entities"))
                    if (item.at("id") == actor)
                        return item;
                throw std::runtime_error("Rig live actor disappeared");
            };
            wait([&] {
                return play.control_ready() && play.effective_snapshot().is_object() &&
                       row().contains("animation_pose");
            });
            if (!play.paused())
                play.pause();
            wait([&] { return play.paused() && play.control_ready(); });
            const auto phase = row().at("animation_pose").at("time");
            const auto tick = play.timing().at("tick");
            require(play.submit_property_tune(actor, "forge.two_bone_ik", "target_x", .8),
                    "Ready Play rejected rig tuning");
            require(!play.submit_property_tune(actor, "forge.two_bone_ik", "target_x", .7),
                    "Live rig queue admitted duplicate command");
            wait([&] { return play.property_tune_ack().is_object() && play.control_ready(); });
            require(play.property_tune_ack().value("ok", true) &&
                        std::abs(row()
                                     .at("components")
                                     .at("forge.two_bone_ik")
                                     .at("target_x")
                                     .get<double>() -
                                 .8) < 1e-6,
                    "Transport did not apply and acknowledge Live rig value");
            const auto preserved = play.snapshot();
            require(row().at("animation_pose").at("time") == phase &&
                        play.timing().at("tick") == tick,
                    "Paused Live rig tuning advanced simulation");
            require(play.submit_property_tune(actor, "forge.two_bone_ik", "axis_z", 0),
                    "Live rig rejection request did not queue");
            wait([&] { return play.property_tune_ack().is_object() && play.control_ready(); });
            require(!play.property_tune_ack().value("ok", true) && play.snapshot() == preserved &&
                        play.active(),
                    "Rejected Live rig change lost the running world");
            play.stop();
            require(read(root / "main.scene.json") == authored &&
                        !play.submit_property_tune(actor, "forge.two_bone_ik", "target_x", .7),
                    "Stop changed authored rig data or retained a tuning queue");
        }
        SDL_Quit();
        std::cout << "Actual normal/shared Play transport, typed tuning, rejection feedback and "
                     "Stop passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
