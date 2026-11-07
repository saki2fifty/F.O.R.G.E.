#include "editor/audio_mix.hpp"
#include "editor/audio_routing.hpp"
#include "editor/editor_state.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void capture_controls(const std::filesystem::path& captures, float scale, bool compact) {
    const int width = int((compact ? 700 : 850) * scale), height = int(480 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::style(scale);
    AudioMixEditor mixer;
    Json audio{
        {"master_volume", 1},
        {"routing", audio_routing_json({{{"rain", "Rain", .5f, "effects", {{"music", .25f}}}}})}};
    Json status{{"output", "offline"},
                {"paused", true},
                {"voices", 1},
                {"playing_sources", 1},
                {"clips", 1},
                {"decoded_bytes", 192000},
                {"master_volume", 1},
                {"channel_volumes", {1, 1, 1}},
                {"bus_volumes", {{"rain", .5}}}};
    for (bool live : {false, true}) {
        for (unsigned frame = 0; frame < 3; ++frame) {
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
            ImGui::Begin("Project Settings");
            if (live)
                mixer.draw("routing-capture", status, true, false, "",
                           [](const Json&) { return true; });
            else {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                AudioRoutingEditor::draw(audio);
            }
            ImGui::End();
            capture.render();
        }
        const auto name = std::string(live ? "audio-routing-live-" : "audio-routing-project-") +
                          std::to_string(int(scale * 100)) + (compact ? "-compact.png" : ".png");
        capture.save(captures / name);
    }
}
} // namespace
int main(int argc, char** argv) {
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
            bool routing_mode = false;
            Json project_audio{{"master_volume", 1}};
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
                if (routing_mode)
                    AudioRoutingEditor::draw(project_audio);
                else
                    editor.draw(session, status, ready, pending, "", [&](const Json& values) {
                        sent = AudioMixValues::parse(values).document();
                        status["master_volume"] = sent.at("master_volume");
                        status["channel_volumes"] = sent.at("channel_volumes");
                        if (sent.contains("bus_volumes"))
                            status["bus_volumes"] = sent.at("bus_volumes");
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
            status["bus_volumes"] = {{"room", .75}, {"rain", .5}};
            session = "custom";
            frame();
            number("live-audio:bus:room", "0.25");
            number("live-audio:bus:rain", "0");
            require(editor.dirty() && submits == 2, "Custom faders submitted before Apply");
            click("live-audio:apply");
            require(submits == 3 && sent.at("bus_volumes") == Json({{"room", .25}, {"rain", 0}}) &&
                        !editor.dirty(),
                    "Custom fader Apply lost the complete key set");
            number("live-audio:bus:room", "0.6");
            click("live-audio:reload");
            require(!editor.dirty() && submits == 3, "Custom fader Reload retained a draft");
            ready = false;
            frame();
            require(!test::ui_targets.at("live-audio:Music").enabled,
                    "Busy runtime allowed tuning");
            status = {{"output", "disabled"}};
            frame();
            require(!test::ui_targets.contains("live-audio:Master"),
                    "Disabled audio exposed tuning");
            routing_mode = true;
            frame();
            click("audio-routing:tree");
            click("audio-routing:add-first");
            require(project_audio.at("routing").at("buses").size() == 1,
                    "First Add custom bus did not create its definition");
            number("audio-routing:key:0", "rain");
            number("audio-routing:label:0", "Rain");
            number("audio-routing:volume:0", "0.5");
            click("audio-routing:add-send:0");
            number("audio-routing:send-gain:0:0", "0.25");
            const auto routing = audio_routing_settings(project_audio);
            require(routing.buses.size() == 1 && routing.buses[0].key == "rain" &&
                        routing.buses[0].label == "Rain" && routing.buses[0].volume == .5f &&
                        routing.buses[0].sends == std::vector<AudioBusSend>{{"master", .25f}},
                    "Actual routing controls lost keys, labels, fader or send draft");
            click("audio-routing:remove-send:0:0");
            require(audio_routing_settings(project_audio).buses[0].sends.empty(),
                    "Remove send did not edit the existing draft");
            click("audio-routing:remove:0");
            require(audio_routing_settings(project_audio).buses.empty(),
                    "Remove bus retained a definition");
            project_audio["routing"] = {
                {"version", 1},
                {"buses", Json::array({{{"key", "quiet"}, {"output", "effects"}}})}};
            const auto minimal = project_audio;
            frame();
            frame();
            require(project_audio == minimal,
                    "Opening a minimal bus definition dirtied optional defaults");
            click("audio-routing:add-send:0");
            require(project_audio["routing"]["buses"][0].contains("sends") &&
                        audio_routing_settings(project_audio).buses[0].sends.size() == 1,
                    "Add send did not explicitly materialize an omitted collection");
            ImGui::DestroyContext();
        }
        if (argc == 2) {
            const auto captures = std::filesystem::absolute(argv[1]);
            std::filesystem::create_directories(captures);
            for (float scale : {.65f, 1.f, 1.5f, 2.f})
                capture_controls(captures, scale, false);
            capture_controls(captures, 1, true);
        } else
            require(argc == 1, "Need optional captures directory");
        std::cout << "Live audio mix actual scaled input, draft/admission/apply/reload/session "
                     "boundaries passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
