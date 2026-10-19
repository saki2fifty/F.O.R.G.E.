#include "editor/audio_preview.hpp"
#include "editor_capture_input.hpp"
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
void capture_waveform(const std::filesystem::path& root, const AssetRecord& record, float scale) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.FontGlobalScale = scale;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    test::observe_ui = true;
    {
        AudioPreviewEditor preview(AudioOutput::Offline);
        test::EditorCaptureInput input;
        auto displayed = record;
        displayed.metadata["fixture.stale"] = true;
        const auto published =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        AudioClipPreview rejected;
        bool stale_rejected = false;
        try {
            rejected.load_waveform(root, displayed);
        } catch (const std::runtime_error&) {
            stale_rejected = true;
        }
        require(stale_rejected, "Waveform accepted stale browser metadata");
        auto removed = record;
        removed.id = AssetId::generate();
        require(!audio_preview_record(published, &removed, removed.id) &&
                    audio_preview_record({}, &record, record.id) == &record,
                "Published removal or read-only preview fallback differs");
        unsigned frames = 0;
        bool shown = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!shown && std::chrono::steady_clock::now() < deadline) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            input.after_platform_update(io, 235, frames);
            test::ui_targets.clear();
            ImGui::NewFrame();
            // The first ready observation precedes a first-use window move.
            // The capture must settle the actual geometry before one UI click.
            ImGui::SetNextWindowPos(frames < 4 ? ImVec2{0, 0} : ImVec2{30, 55}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1400, 900}, ImGuiCond_Always);
            ImGui::Begin("Audio clip");
            // Keep browser discovery stale throughout the actual click/load.
            // The import owner's published revision must drive the preview.
            const auto* selected = audio_preview_record(published, &displayed, record.id);
            preview.draw(root, selected, false);
            ImGui::End();
            ImGui::Render();
            ++frames;
            if (frames >= 4) {
                const auto& target = test::ui_targets.at("audio-preview:waveform-load");
                input.click_once(io, 235, frames,
                                 {(target.minimum.x + target.maximum.x) * .5f,
                                  (target.minimum.y + target.maximum.y) * .5f},
                                 selected && selected->metadata == record.metadata &&
                                     target.enabled && target.minimum.y >= target.clip_minimum.y &&
                                     target.maximum.y <= target.clip_maximum.y);
            }
            shown = test::ui_targets.contains("audio-preview:waveform");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(shown && input.click_frame >= 7 && !io.MouseDown[0] && !preview.active() &&
                    displayed.metadata != record.metadata,
                "Published waveform did not survive stale browser discovery and a moved control");
    }
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need original Gallery");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto catalog = AssetCatalog::open_project(root);
        const auto record =
            catalog.records().at(AssetId::parse("327101c1-e496-4293-b830-4c25b978607a"));
        for (float scale : {1.f, 1.5f, 2.f})
            capture_waveform(root, record, scale);
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
