#include "editor/audio_preview.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
double power(AudioPreviewEditor& editor) {
    std::array<float, 8192> warm{};
    editor.read_offline(warm);
    std::array<float, 9600> pcm{};
    editor.read_offline(pcm);
    double result = 0;
    for (float value : pcm)
        result += double(value) * value;
    return result / pcm.size();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need original Gallery");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto catalog = AssetCatalog::open_project(root);
        const auto record =
            catalog.records().at(AssetId::parse("327101c1-e496-4293-b830-4c25b978607a"));
        for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1400, 900};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            test::observe_ui = true;
            {
                AudioPreviewEditor preview(AudioOutput::Offline);
                bool locked = false;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({1400, 900}, ImGuiCond_Always);
                    ImGui::Begin("Audio clip");
                    preview.draw(root, &record, locked);
                    ImGui::End();
                    ImGui::Render();
                };
                auto click = [&](const char* control) {
                    frame();
                    const auto key = std::string("audio-preview:") + control;
                    require(test::ui_targets.contains(key), "Audio preview control missing");
                    auto target = test::ui_targets.at(key);
                    require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                                target.maximum.y <= target.clip_maximum.y,
                            "Preview control disabled or clipped");
                    io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                                        (target.minimum.y + target.maximum.y) * .5f);
                    frame();
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    if (std::string(control) == "play" || std::string(control) == "waveform-load") {
                        require(preview.pending(), "First-use preview hid its pending state");
                        if (std::string(control) == "play")
                            require(test::ui_targets.at("audio-preview:stop").enabled,
                                    "Pending audition could not be stopped");
                    }
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (preview.pending() && std::chrono::steady_clock::now() < deadline) {
                        frame();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    require(!preview.pending(), "Preview UI preparation timed out");
                };
                frame();
                frame();
                require(!preview.active(), "Selecting clip started unexpected autoplay");
                click("waveform-load");
                require(!preview.active() && test::ui_targets.contains("audio-preview:waveform"),
                        "Showing waveform opened playback or omitted the waveform");
                click("loop");
                click("play");
                require(preview.active(), "Actual Play preview button did not start sound");
                const auto baseline = power(preview);
                require(baseline > 1e-5, "Preview button opened no actual PCM");
                click("pause");
                require(preview.paused(), "Pause preview button did not pause");
                const auto held = preview.position();
                require(power(preview) < 1e-12 && preview.position() == held,
                        "Paused preview emitted samples or advanced displayed time");
                click("waveform");
                const auto duration =
                    record.metadata.at("forge.audio").at("duration").get<double>();
                require(preview.paused() && std::abs(preview.position() - duration * .5) < .001,
                        "Waveform click did not seek while preserving pause");
                // Commit a typed timeline value through the actual double slider.
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                click("position");
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8("0.25");
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
                require(preview.paused() && std::abs(preview.position() - .25) < .001,
                        "Typed timeline position did not commit seeking");
                click("pause");
                require(!preview.paused() && power(preview) > 1e-5,
                        "Resume preview button did not restore actual sound");
                std::vector<float> two_seconds(192000);
                preview.read_offline(two_seconds);
                frame();
                require(preview.active(), "Actual Loop control did not repeat sound");
                // Ctrl-click the actual SliderFloat and enter a known preview gain.
                auto type_volume = [&](const char* text) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    click("volume");
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
                type_volume("0.25");
                require(std::abs(power(preview) / baseline - .25) < .004,
                        "Actual Preview volume slider did not change PCM amplitude");
                type_volume("2");
                require(std::abs(power(preview) / baseline - 4) < .004,
                        "Typed preview volume exceeded its admitted maximum");
                type_volume("nan");
                require(preview.active() && std::abs(power(preview) / baseline - 4) < .004,
                        "Nonfinite input crashed preview or changed admitted playback");
                click("stop");
                require(!preview.active(), "Actual Stop preview button retained sound");
                click("play");
                auto revision = record;
                revision.metadata["forge.audio"]["duration"] = 2.0;
                preview.poll(root, &revision, true);
                require(!preview.active(), "New clip revision retained old preview");
                click("play");
                preview.poll(root.parent_path(), &record, true);
                require(!preview.active(), "Project change retained preview");
                click("play");
                preview.poll(root, nullptr, true);
                require(!preview.active(), "Removed selection retained preview");
                click("play");
                locked = true;
                frame();
                require(!preview.active(), "Play/import lock retained preview");
                for (const auto* control : {"play", "stop", "loop", "volume"})
                    require(!test::ui_targets.at(std::string("audio-preview:") + control).enabled,
                            "Locked preview control remained enabled");
            }
            ImGui::DestroyContext();
        }
        std::cout << "Actual preview controls and lifecycle passed at65/100/150/200%\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
