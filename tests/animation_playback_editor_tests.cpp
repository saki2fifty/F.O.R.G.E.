#include "editor/animation_playback.hpp"
#include "editor/editor_state.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        for (float scale : {.65f, 1.f, 1.5f, 2.f})
            for (float window_width : {1400.f, 420.f}) {
                ImGui::CreateContext();
                auto& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = {1400, 1200};
                io.DeltaTime = 1.f / 60;
                io.ConfigInputTrickleEventQueue = false;
                unsigned char* pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                ui::style(scale);
                test::observe_ui = true;
                AnimationPlaybackEditor editor;
                const EntityRef ref{AssetId::generate(), EntityId::generate()};
                Json status{{"source", ref},
                            {"available", true},
                            {"playing", false},
                            {"time", .25},
                            {"duration", 1},
                            {"finished", false},
                            {"state_name", "Original clip"}};
                unsigned submits = 0;
                Json sent;
                bool ready = true, pending = false;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({window_width, 1200}, ImGuiCond_Always);
                    ImGui::Begin("Inspector");
                    editor.draw("test", ref, status, ready, pending, "", [&](const Json& value) {
                        sent = value;
                        ++submits;
                        if (value.at("operation") == "pause")
                            status["playing"] = false;
                        if (value.at("operation") == "play")
                            status["playing"] = true;
                        if (value.at("operation") == "seek")
                            status["time"] = value.at("position_seconds");
                        return true;
                    });
                    ImGui::End();
                    ImGui::Render();
                };
                auto point = [&](const char* key, float ratio = .5f) {
                    frame();
                    const auto target = test::ui_targets.at(key);
                    if (!target.enabled || target.minimum.y < target.clip_minimum.y ||
                        target.maximum.y > target.clip_maximum.y ||
                        target.minimum.x < target.clip_minimum.x ||
                        target.maximum.x > target.clip_maximum.x)
                        throw std::runtime_error(
                            std::string("Live animation control clipped/disabled: ") + key +
                            " scale=" + std::to_string(scale) +
                            " width=" + std::to_string(window_width) + " sequence=" +
                            std::to_string(status.value("sequence_controller", false)));
                    io.AddMousePosEvent(target.minimum.x +
                                            (target.maximum.x - target.minimum.x) * ratio,
                                        (target.minimum.y + target.maximum.y) * .5f);
                    frame();
                };
                auto click = [&](const char* key) {
                    point(key);
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                };
                for (int i = 0; i < 4; ++i)
                    frame();
                click("live-animation:pause");
                require(submits == 1 && sent.at("operation") == "play", "Resume clip failed");
                click("live-animation:pause");
                require(submits == 2 && sent.at("operation") == "pause", "Pause clip failed");
                click("live-animation:restart");
                require(submits == 3 && sent.at("operation") == "restart", "Restart clip failed");
                point("live-animation:playhead", .75f);
                io.AddMouseButtonEvent(0, true);
                frame();
                require(submits == 3, "Seek submitted before release");
                status["time"] = .1;
                frame(); // A fresh runtime observation cannot overwrite an active drag.
                io.AddMouseButtonEvent(0, false);
                frame();
                require(submits == 4 && sent.at("operation") == "seek" &&
                            sent.at("position_seconds").get<double>() > .65 &&
                            sent.at("position_seconds").get<double>() < .85,
                        "Release seek lost its dragged value");
                pending = true;
                frame();
                require(!test::ui_targets.at("live-animation:playhead").enabled,
                        "Pending seek enabled");
                pending = false;
                ready = false;
                frame();
                require(!test::ui_targets.at("live-animation:restart").enabled,
                        "Loading restart enabled");
                ready = true;
                status["available"] = false;
                frame();
                require(!test::ui_targets.at("live-animation:pause").enabled,
                        "Missing clip enabled");
                const Json effective{
                    {"asset_id", ref.scene},
                    {"entities",
                     Json::array({{{"id", ref.entity},
                                   {"components", {{"forge.animator", Json::object()}}}}})}};
                require(
                    AnimationPlaybackEditor::selected(effective, ref.entity.str()) == ref &&
                        !AnimationPlaybackEditor::selected(effective, EntityId::generate().str()),
                    "Animation selection fabricated identity");
                status["available"] = true;
                status["sequence_controller"] = true;
                status["duration"] = 6;
                status["playing"] = false;
                frame();
                frame(); // Auto-sized child settles after the unavailable-to-ready transition.
                click("live-animation:pause");
                require(sent.at("operation") == "play", "Resume sequence failed");
                click("live-animation:restart");
                require(sent.at("operation") == "restart", "Restart sequence failed");
                auto grouped = effective;
                grouped["entities"][0]["components"] = {
                    {"forge.animation_sequence", Json::object()}};
                require(AnimationPlaybackEditor::selected(grouped, ref.entity.str()) == ref,
                        "Sequence controller did not expose live controls");
                grouped["entities"][0]["components"] = {
                    {"forge.transform_animator", {{"sequence", ref}}}};
                require(!AnimationPlaybackEditor::selected(grouped, ref.entity.str()),
                        "Bound track exposed independently editable live controls");
                ImGui::DestroyContext();
            }
        std::cout << "Live animation actual buttons/release seek and status isolation pass eight "
                     "layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
