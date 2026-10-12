#include "editor/audio_mix.hpp"
#include "editor/editor_state.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
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
            AudioMixEditor editor;
            Json status{{"output", "offline"}, {"paused", true},
                        {"voices", 1},         {"playing_sources", 1},
                        {"clips", 1},          {"decoded_bytes", 4096},
                        {"master_volume", 1},  {"channel_volumes", {1, 1, 1}}};
            std::string session = "first";
            bool ready = true, pending = false;
            unsigned submits = 0;
            Json sent;
            auto frame = [&] {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1400, 1200}, ImGuiCond_Always);
                ImGui::Begin("Project Settings");
                editor.draw(session, status, ready, pending, "", [&](const Json& values) {
                    sent = AudioMixValues::parse(values).document();
                    status["master_volume"] = sent.at("master_volume");
                    status["channel_volumes"] = sent.at("channel_volumes");
                    ++submits;
                    return true;
                });
                ImGui::End();
                ImGui::Render();
            };
            auto click = [&](const char* key) {
                frame();
                require(test::ui_targets.contains(key), "Live mix control missing");
                const auto target = test::ui_targets.at(key);
                require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                            target.maximum.y <= target.clip_maximum.y,
                        "Live mix control disabled/clipped");
                io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                                    (target.minimum.y + target.maximum.y) * .5f);
                frame();
                io.AddMouseButtonEvent(0, true);
                frame();
                io.AddMouseButtonEvent(0, false);
                frame();
            };
            auto number = [&](const char* key, const char* text) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                click(key);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            frame();
            require(!editor.dirty() && !test::ui_targets.at("live-audio:apply").enabled,
                    "Opening live mix sent a draft");
            number("live-audio:Master", "0.25");
            number("live-audio:Music", "0.5");
            number("live-audio:Effects", "0.75");
            number("live-audio:Dialogue", "0");
            require(editor.dirty() && submits == 0, "Slider draft sent per-frame requests");
            click("live-audio:apply");
            require(submits == 1 && sent.at("master_volume") == .25 &&
                        sent.at("channel_volumes") == Json::array({.5, .75, 0}) && !editor.dirty(),
                    "Actual Apply missed admitted volumes");
            pending = true;
            frame();
            require(!test::ui_targets.at("live-audio:Master").enabled,
                    "Pending mix admitted another edit");
            pending = false;
            frame();
            number("live-audio:Master", "nan");
            require(!editor.dirty(), "Nonfinite live volume changed admitted draft");
            number("live-audio:Effects", "0.125");
            click("live-audio:apply");
            require(submits == 2 && sent.at("master_volume") == .25,
                    "Nonfinite edit damaged previous Master volume");
            number("live-audio:Music", "0.2");
            click("live-audio:reload");
            require(!editor.dirty() && submits == 2, "Reload sent/retained draft");
            number("live-audio:Music", "0.3");
            session = "restarted";
            frame();
            require(!editor.dirty() && submits == 2, "New Play session retained old volume draft");
            ready = false;
            frame();
            require(!test::ui_targets.at("live-audio:Music").enabled,
                    "Busy runtime allowed tuning");
            status = {{"output", "disabled"}};
            frame();
            require(!test::ui_targets.contains("live-audio:Master"),
                    "Disabled audio exposed tuning");
            ImGui::DestroyContext();
        }
        std::cout << "Live audio mix actual scaled input, draft/admission/apply/reload/session "
                     "boundaries passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
