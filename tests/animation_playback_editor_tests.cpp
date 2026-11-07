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
                EntityRef selected_controller;
                bool ready = true, pending = false;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({window_width, 1200}, ImGuiCond_Always);
                    ImGui::Begin("Inspector");
                    editor.draw(
                        "test", ref, status, ready, pending, "",
                        [&](const Json& value) {
                            sent = value;
                            ++submits;
                            if (value.at("operation") == "pause")
                                status["playing"] = false;
                            if (value.at("operation") == "play")
                                status["playing"] = true;
                            if (value.at("operation") == "seek")
                                status["time"] = value.at("position_seconds");
                            return true;
                        },
                        [&](EntityRef controller) { selected_controller = controller; });
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
                require(AnimationPlaybackEditor::selected(grouped, ref.entity.str()) == ref,
                        "Bound object track did not expose read-only live status");
                const EntityRef controller{ref.scene, EntityId::generate()};
                status["sequence_controller"] = false;
                status["sequence"] = controller;
                frame();
                frame();
                const auto before = submits;
                for (const auto* key :
                     {"live-animation:pause", "live-animation:restart", "live-animation:playhead"})
                    require(!test::ui_targets.at(key).enabled,
                            "Sequence-bound model exposed independently editable live controls");
                click("live-animation:controller");
                require(
                    selected_controller == controller && submits == before,
                    "Live model selection did not reveal its owning sequence without a command");
                Json clips = Json::array();
                for (unsigned i = 0; i < 8; ++i)
                    clips.push_back({{"clip", AssetId::generate()},
                                     {"start_seconds", .5 * i},
                                     {"duration_seconds", 1}});
                status["sequence_clips"] = {
                    {"clips", clips}, {"active_primary", 4}, {"active_next", 5}};
                const auto collection_status = status;
                frame();
                frame();
                require(test::ui_targets.contains("live-animation:clip_window"),
                        "Live eight-clip status omitted its active window");
                if (scale == 1 && window_width == 1400) {
                    click("live-animation:placements");
                    for (unsigned i = 0; i < 8; ++i)
                        require(test::ui_targets.contains("live-animation:placement:" +
                                                          std::to_string(i)),
                                "Live collection list omitted a placement");
                    click("live-animation:placements");
                }
                require(status == collection_status && submits == before,
                        "Live collection inspection changed values or sent a playback command");
                status["sequence_clips"]["multiway"] = true;
                status["sequence_clips"]["summary_only"] = true;
                for (unsigned i = 0; i < 8; ++i) {
                    status["sequence_clips"]["clips"][i]["weight"] = .125;
                    status["sequence_clips"]["clips"][i]["time_seconds"] = .75;
                }
                const auto complete_status = status;
                frame();
                frame();
                require(test::ui_targets.contains("live-animation:summary_only"),
                        "Live multiway view omitted its two-slot summary label");
                if (scale == 1 && window_width == 1400) {
                    click("live-animation:placements");
                    for (unsigned i = 0; i < 8; ++i)
                        require(test::ui_targets.contains("live-animation:influence:" +
                                                          std::to_string(i)),
                                "Live multiway list omitted a local time/influence");
                    click("live-animation:placements");
                }
                require(status == complete_status && submits == before,
                        "Multiway live inspection changed values or sent a command");
                status.erase("sequence_clips");
                status["sequence_controller"] = true;
                status["sequence_placements"] = {
                    {"active_placement", 1},
                    {"active", false},
                    {"placements",
                     Json::array({{{"start_seconds", .5}, {"rate", 2.}, {"loop", false}},
                                  {{"start_seconds", 2.}, {"rate", .5}, {"loop", true}}})}};
                const auto repeated_status = status;
                frame();
                frame();
                require(test::ui_targets.contains("live-animation:controller_window"),
                        "Live repeated controller omitted its selected placement");
                if (scale == 1 && window_width == 1400) {
                    click("live-animation:controller_placements");
                    for (unsigned i = 0; i < 2; ++i)
                        require(test::ui_targets.contains("live-animation:controller_placement:" +
                                                          std::to_string(i)),
                                "Live repeated controller omitted an authored placement");
                    click("live-animation:controller_placements");
                }
                require(status == repeated_status && submits == before,
                        "Live controller placement inspection mutated playback/configuration");
                for (const auto* key :
                     {"live-animation:pause", "live-animation:restart", "live-animation:playhead"})
                    require(!test::ui_targets.at(key).enabled,
                            "Nested controller exposed independent live controls");
                click("live-animation:controller");
                require(selected_controller == controller && submits == before,
                        "Live child selection did not reveal its parent without a command");
                ImGui::DestroyContext();
            }
        std::cout << "Live animation actual buttons/release seek and status isolation pass eight "
                     "layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
