#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "audio_importer.hpp"
#include "audio_imports.hpp"
#include "editor/audio_preview.hpp"
#include "import_settings.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void initialize(float scale) {
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
}
template <class Frame> void click(Frame frame, const std::string& key) {
    auto& io = ImGui::GetIO();
    frame();
    require(test::ui_targets.contains(key), "Audio import target missing");
    const auto target = test::ui_targets.at(key);
    require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                target.maximum.y <= target.clip_maximum.y,
            "Audio import target disabled/clipped");
    io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                        (target.minimum.y + target.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
}
void schema_controls() {
    for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
        initialize(scale);
        const auto schema = asset_detail::audio_settings();
        ImportSettingsDocument draft{"forge.audio.wav"};
        std::string error;
        auto frame = [&] {
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1400, 1200}, ImGuiCond_Always);
            ImGui::Begin("WAV import settings");
            ui::import_settings_fields(schema, draft, error);
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        for (const auto* option : {"force_mono", "normalize_peak", "trim_silence"}) {
            click(frame, std::string("import-setting:forge.audio.wav:") + option);
            require(error.empty() && draft.overrides.at(option) == true,
                    "Generated audio checkbox did not edit its settings draft");
        }
        auto& io = ImGui::GetIO();
        auto number = [&](const char* text) {
            click(frame, "import-setting:forge.audio.wav:trim_threshold_db");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
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
        number("-40");
        require(error.empty() && schema.effective(draft).at("trim_threshold_db") == -40.,
                "Generated threshold input did not edit its settings draft");
        number("nan");
        require(!error.empty() && schema.effective(draft).at("trim_threshold_db") == -40.,
                "Nonfinite threshold escaped schema admission");
        ImGui::DestroyContext();
    }
}
void workflow(const std::filesystem::path& worker, const std::filesystem::path& gallery,
              const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto source = root / "Assets/ProcessingOriginal.wav";
    const auto bytes = asset_detail::read_bytes(source, asset_detail::max_audio_source_bytes);
    initialize(1);
    {
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument document(scene);
        document.open_project(root, true);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        AudioImportEditor editor(worker);
        AudioPreviewEditor preview(AudioOutput::Offline);
        editor.preview_before_settings = true;
        editor.draw_extension = [&](auto& doc, bool locked) {
            const auto catalog = AssetCatalog::open_project(root);
            const auto found = catalog.records().find(editor.selected_asset());
            preview.draw(doc.project(), found == catalog.records().end() ? nullptr : &found->second,
                         locked);
        };
        std::string message;
        auto frame = [&] {
            editor.poll(document, message);
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1400, 1200}, ImGuiCond_Always);
            editor.draw(document, false);
            ImGui::Render();
        };
        auto settle_preview = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (preview.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(!preview.pending(), "Import preview preparation timed out");
            frame();
        };
        editor.open(document, "Assets/ProcessingOriginal.wav");
        frame();
        frame();
        const auto id = editor.selected_asset();
        click(frame, "audio-preview:waveform-load");
        settle_preview();
        const auto original_band = test::ui_targets.at("audio-preview:waveform");
        const auto original_height = original_band.maximum.y - original_band.minimum.y;
        click(frame, "audio-preview:play");
        settle_preview();
        require(preview.active(), "Original selected revision did not audition");
        click(frame, "import:settings");
        click(frame, "import-setting:forge.audio.wav:normalize_peak");
        frame(); // Preview draws before settings; the next frame observes the new draft lock.
        require(!preview.active() && !test::ui_targets.contains("audio-preview:waveform"),
                "Import draft did not release original audition and waveform");
        click(frame, "import-setting:forge.audio.wav:force_mono");
        click(frame, "import-setting:forge.audio.wav:trim_silence");
        require(editor.dirty(), "Audio processing edit did not mark import draft dirty");
        click(frame, "button:Import / Reimport");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while ((editor.pending() || editor.dirty()) &&
               std::chrono::steady_clock::now() < deadline) {
            frame();
            if (!editor.pending() && !editor.diagnostic().empty())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        if (editor.pending() || editor.dirty() || !editor.diagnostic().empty())
            std::cerr << "Import pending=" << editor.pending() << " dirty=" << editor.dirty()
                      << " diagnostic=" << editor.diagnostic() << " message=" << message << '\n';
        require(!editor.pending() && !editor.dirty() && editor.diagnostic().empty(),
                "Actual audio Import did not publish processing settings");
        const auto sidecar =
            AssetImportSidecar::parse(*asset_storage::read(source.string() + ".forge-import.json"));
        require(sidecar.settings.overrides.at("normalize_peak") == true &&
                    sidecar.settings.overrides.at("force_mono") == true &&
                    sidecar.settings.overrides.at("trim_silence") == true &&
                    editor.selected_asset() == id,
                "Audio Import lost settings or asset identity");
        const auto published = AssetCatalog::open_project(root).records().at(id);
        require(published.metadata.at("forge.audio").at("channels") == 1 &&
                    published.metadata.at("forge.audio").at("frames") == 48000,
                "Reimport did not publish the shorter mono selection");
        click(frame, "audio-preview:waveform-load");
        settle_preview();
        require(!preview.active() && test::ui_targets.contains("audio-preview:waveform"),
                "Published processed clip could not show waveform");
        const auto processed_band = test::ui_targets.at("audio-preview:waveform");
        require(processed_band.maximum.y - processed_band.minimum.y == original_height * .5f,
                "Reimported waveform retained the previous stereo envelope");
        click(frame, "import-setting:forge.audio.wav:force_mono");
        require(editor.dirty(), "Second import edit did not create a draft");
        click(frame, "button:Reload saved settings");
        require(!editor.dirty(), "Reload did not restore saved processing settings");
        click(frame, "button:Reset to defaults");
        require(editor.dirty(), "Reset did not create a defaults draft");
        click(frame, "button:Reload saved settings");
        require(!editor.dirty() &&
                    asset_detail::read_bytes(source, asset_detail::max_audio_source_bytes) == bytes,
                "Audio import controls changed the original WAV");
        preview.stop();
        editor.request_close();
        frame();
        require(!editor.is_open(), "Saved AudioClip tab failed to close");
    }
    ImGui::DestroyContext();
    std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Need worker or --schema-only, Gallery, scratch");
        schema_controls();
        if (std::string_view(argv[1]) != "--schema-only")
            workflow(std::filesystem::absolute(argv[1]), argv[2], argv[3]);
        std::cout << "Audio processing widgets/admission and configured import workflow passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
