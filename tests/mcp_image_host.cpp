#include "automation.hpp"
#include "capture_automation.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace forge::test {
// Test-only detached pixel delivery, replacing only the absent GPU readback.
// File/preview jobs, frame scheduling, receipts and MCP all use the real owner.
struct ViewImageTaskAccess {
    static void deliver_test_pixels(ui::ViewImageTask& owner, std::string& message) {
        if (!owner.pending_ || owner.copied_ || owner.job_.valid())
            return;
        DiligentPresentation::DisplayPixels pixels;
        const auto extent = owner.sequence_extent();
        pixels.width = extent ? extent->width : 4;
        pixels.height = extent ? extent->height : 3;
        pixels.rgba.resize(std::size_t(pixels.width) * pixels.height * 4);
        for (std::size_t i = 0; i < pixels.rgba.size(); i += 4) {
            pixels.rgba[i] = 32;
            pixels.rgba[i + 1] = 96;
            pixels.rgba[i + 2] = 192;
            pixels.rgba[i + 3] = 255;
        }
        owner.save_pixels(std::move(pixels), message);
    }
};
} // namespace forge::test
struct RecordingFrames final : forge::ui::SequenceFrameSource {
    forge::Scene& scene;
    std::filesystem::path root;
    forge::EntityRef controller;
    std::uint64_t token = 0;
    double cursor = .25, saved = 0;
    bool active = false;
    RecordingFrames(forge::Scene& value, std::filesystem::path project)
        : scene(value), root(std::move(project)) {}
    bool frame_export_ready() const override { return bool(controller.entity) && !active; }
    forge::ui::SequenceFrameSourceInfo frame_export_info() const override {
        if (!frame_export_ready())
            throw std::runtime_error("Recording source not ready");
        return {controller, 3., scene.revision(), token,
                std::make_shared<forge::AssetCatalog>(forge::AssetCatalog::open_project(root))};
    }
    forge::Json frame_export_document() const override { return scene.document(); }
    std::uint64_t begin_frame_export() override {
        saved = cursor;
        active = true;
        return ++token;
    }
    bool frame_export_current(std::uint64_t value) const override {
        return active && value == token;
    }
    void sample_frame_export(std::uint64_t value, double seconds) override {
        if (!frame_export_current(value))
            throw std::runtime_error("Stale recording token");
        cursor = seconds;
    }
    void end_frame_export(std::uint64_t value) override {
        if (value == token) {
            active = false;
            cursor = saved;
            if (std::filesystem::exists(root.parent_path() / "fail_restore"))
                throw std::runtime_error("Deliberate recording-source restore failure");
        }
    }
};
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const bool output = std::string(argv[2]) != "read";
        forge::SceneDocument::create_project(root / "Game", "Native image IO fixture");
        std::filesystem::create_directory(root / "Output");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        forge::ui::ViewImageTask images;
        RecordingFrames frames(scene, files.document.project());
        std::string message;
        forge::CaptureAutomationProviders providers;
        providers.show = [](auto) {};
        providers.open_sequence = [&](const std::string& entity) {
            if (!scene.entity(entity).is_valid())
                throw std::runtime_error("Choose a current entity");
            frames.controller = {scene.asset_id(), forge::EntityId::parse(entity)};
        };
        providers.sequence = [&]() -> forge::ui::SequenceFrameSource& { return frames; };
        providers.unavailable = [](bool) { return std::string{}; };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            return forge::CaptureAutomation::methods(images, scene, files, providers, message,
                                                     permissions);
        };
        forge::ui::AutomationPermissions permissions;
        permissions.export_game = output;
        if (output)
            permissions.export_root = root / "Output";
        workspace.start(scene, files, output, output, permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            images.poll_files(message);
            if (!std::filesystem::exists(root / "hold_pixels"))
                forge::test::ViewImageTaskAccess::deliver_test_pixels(images, message);
            workspace.pump(files.document, "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!workspace.active())
            std::cerr << "MCP owner connection stopped: " << workspace.diagnostic() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
