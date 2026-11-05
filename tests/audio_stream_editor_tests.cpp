#include "audio_source_access.hpp"
#include "editor/audio_imports.hpp"
#include "editor/audio_mix.hpp"
#include "editor/audio_preview.hpp"
#include "editor/audio_source.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void capture(const std::filesystem::path& gallery, const std::filesystem::path& worker,
             const std::filesystem::path& output, float scale, bool narrow) {
    const auto root = output / AssetId::generate().str();
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    test::ImGuiSoftwareCapture surface(int((narrow ? 450 : 760) * scale), int(1020 * scale));
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto before = scene.snapshot();
    const auto catalog = AssetCatalog::open_project(root);
    const AssetRecord* record = nullptr;
    for (const auto& [id, candidate] : catalog.records()) {
        (void)id;
        if (candidate.source == "Assets/AudioStreaming/PagePulse.wav")
            record = &candidate;
    }
    require(record != nullptr, "Streaming capture clip missing");
    AudioImportEditor editor(worker);
    AudioPreviewEditor preview(AudioOutput::Offline);
    editor.preview_before_settings = true;
    editor.draw_extension = [&](auto& document, bool locked) {
        ui::heading("Clip details", "Metadata of the successfully published audio clip.");
        draw_audio_details(*record);
        preview.draw(document.project(), record, locked);
    };
    editor.open(project, record->source);
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        surface.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("###Audio clip"))
            ImGui::SetWindowSize(window, ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        editor.draw(project, false);
        surface.render();
    };
    for (unsigned i = 0; i < 5; ++i)
        frame();
    test::ImGuiInputDriver input(frame);
    input.click("import:settings");
    for (unsigned i = 0; i < 3; ++i)
        frame();
    const auto& storage = test::ui_targets.at("import-setting:forge.audio.wav:playback_storage");
    require(storage.enabled && storage.minimum.x >= storage.clip_minimum.x &&
                storage.maximum.x <= storage.clip_maximum.x &&
                storage.minimum.y >= storage.clip_minimum.y &&
                storage.maximum.y <= storage.clip_maximum.y,
            "Playback storage control is clipped");
    const auto name = std::string(narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100));
    surface.save(output / (name + ".png"));
    require(!editor.pending() && editor.diagnostic().empty() && !preview.active() &&
                scene.snapshot() == before,
            "Viewing stream settings changed or played the project");
    EngineContext runtime(WorldRole::Runtime, false,
                          {audio_module({root, AudioOutput::Offline, true})});
    Scene live(runtime.world());
    std::ifstream in(root / "Scenes/172-audio-streaming.scene.json");
    const auto game = Json::parse(in);
    live.restore_snapshot(game);
    auto audio = std::static_pointer_cast<AudioRuntime>(runtime.services().audio());
    audio->synchronize(0);
    require(audio->wait_for_sources(std::chrono::seconds(5)), "Live mix capture stream failed");
    audio->paused(false);
    const auto ref = live.reference(game.at("entities").back().at("id").get<std::string>());
    AudioMixEditor mixer;
    AudioSourceEditor source;
    auto live_frame = [&] {
        test::ui_targets.clear();
        surface.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Live audio", nullptr, ImGuiWindowFlags_NoResize);
        source.draw("stream", ref, AudioSourceAccess::status(*audio, ref), true, false, "",
                    [&](AudioSourceOperation operation) {
                        AudioSourceAccess::control(*audio, ref, operation);
                        return true;
                    });
        mixer.draw("stream", audio->status(), true, false, "", [](const Json&) { return false; });
        ImGui::End();
        surface.render();
    };
    for (unsigned i = 0; i < 4; ++i)
        live_frame();
    test::ImGuiInputDriver live_input(live_frame);
    live_input.click("live-audio-source:pause");
    require(AudioSourceAccess::status(*audio, ref).at("source_paused") == true,
            "Actual source pause control did not retain stream transport");
    live_input.click("live-audio-source:pause");
    require(AudioSourceAccess::status(*audio, ref).at("playing") == true,
            "Actual source resume control did not continue streaming");
    live_frame();
    surface.save(output / (name + "-mix.png"));
    AudioSourceAccess::control(*audio, ref, AudioSourceOperation::OneShot);
    require(audio->wait_for_sources(std::chrono::seconds(5)), "Capture one-shot did not prepare");
    AudioSourceAccess::control(*audio, ref, AudioSourceOperation::Restart);
    live_input.click("live-audio-source:pause");
    require(AudioSourceAccess::status(*audio, ref).at("pending") == true &&
                AudioSourceAccess::status(*audio, ref).at("paused_one_shots") == 1,
            "UI could not pause the existing one-shot during primary restart");
    live_input.click("live-audio-source:pause");
    require(AudioSourceAccess::status(*audio, ref).at("playing_one_shots") == 1,
            "UI could not resume the one-shot while the primary prepared");
    live_frame();
    surface.save(output / (name + "-pending.png"));
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Need Gallery, worker and capture scratch");
        const auto output = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL capture initialization failed");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                capture(argv[1], argv[2], output, scale, narrow);
        SDL_Quit();
        std::cout << "Actual stream import controls fit six scaled native ImGui layouts\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
