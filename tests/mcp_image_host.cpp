#include "automation.hpp"
#include "capture_automation.hpp"
#include "game_recording_task.hpp"
#include "mcp_test_marker.hpp"
#include "published_texture_fixture.hpp"
#include "texture_image_input.hpp"
#include <chrono>
#include <forge/game_settings.hpp>
#include <iostream>
#include <thread>

namespace forge::test {
// Test-only detached pixel delivery, replacing only the absent GPU readback.
// File/preview jobs, frame scheduling, receipts and MCP all use the real owner.
struct ViewImageTaskAccess {
    static void deliver_test_pixels(ui::ViewImageTask& owner, std::string& message, double cursor) {
        if (owner.input_ || !owner.pending_ || owner.copied_ || owner.job_.valid())
            return;
        if (owner.frames_) {
            const auto sample = owner.temporal_sample();
            if (!sample || !sample->operation || sample->seconds != cursor)
                throw std::runtime_error(
                    "Capture temporal key differs from the borrowed source sample");
        }
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
            if (forge::test::marker_present(root.parent_path() / "fail_restore"))
                throw std::runtime_error("Deliberate recording-source restore failure");
            if (forge::test::marker_present(root.parent_path() / "fail_webp"))
                forge::asset_storage::replace(root.parent_path() /
                                                  "Output/FailedWebP/frame-000002.png",
                                              "deliberately damaged completed frame");
            if (forge::test::marker_present(root.parent_path() / "fail_avi"))
                forge::asset_storage::replace(root.parent_path() /
                                                  "Output/FailedAVI/frame-000002.png",
                                              "deliberately damaged completed movie frame");
        }
    }
};
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 6)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const bool output = std::string(argv[2]) != "read";
        const bool asset_write = output && std::string(argv[2]) != "game-output";
        forge::SceneDocument::create_project(root / "Game", "Native image IO fixture");
        std::filesystem::create_directory(root / "Output");
        if (argc >= 4)
            forge::test::published_texture_fixture(std::filesystem::u8path(argv[3]), root / "Game");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        if (argc == 6) {
            auto settings = files.document.settings().document();
            settings["game"] = forge::default_game_settings("org.forge.mcp-recording-" +
                                                                forge::AssetId::generate().str(),
                                                            "MCP recording fixture");
            files.document.save_settings(settings);
        }
        const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
            ImGui::CreateContext(), ImGui::DestroyContext);
        ImGui::GetIO().IniFilename = nullptr;
        RecordingFrames frames(scene, files.document.project());
        forge::PlaySession play;
        forge::ui::ViewImageTask images(argc >= 5 ? std::filesystem::u8path(argv[4])
                                                  : std::filesystem::path{});
        std::string message;
        forge::CaptureAutomationProviders providers;
        providers.show = [](auto) {};
        providers.texture = [&](forge::AssetRef<forge::TextureAsset> texture,
                                std::optional<forge::TextureSemantic> semantic,
                                forge::TextureImageSlice slice) {
            return forge::ui::texture_image_input(
                files.document.project(),
                std::make_shared<const forge::AssetCatalog>(
                    forge::AssetCatalog::open_project(files.document.project())),
                texture, semantic, slice, {},
                argc >= 5 ? std::filesystem::u8path(argv[4]) : std::filesystem::path{});
        };
        providers.open_sequence = [&](const std::string& entity) {
            if (!scene.entity(entity).is_valid())
                throw std::runtime_error("Choose a current entity");
            frames.controller = {scene.asset_id(), forge::EntityId::parse(entity)};
        };
        providers.sequence = [&]() -> forge::ui::SequenceFrameSource& { return frames; };
        providers.unavailable = [&](bool sequence) {
            return sequence && play.active() ? std::string("Stop the current Play session")
                                             : std::string{};
        };
        if (argc == 6)
            providers.record_game = [&](const forge::ui::GameRecordingRequest& request) {
                forge::ui::GameRecordingHost host;
                host.start = [&](const std::filesystem::path& user_data) {
                    play.configure(60, forge::InputMap{}, {0, -9.81, 0}, files.document.project(),
                                   true, true);
                    play.set_user_data_override(user_data);
                    play.start(argv[5], scene.snapshot(), false, forge::Json(), true);
                };
                host.stop = [&, folder = request.destination] {
                    if (forge::test::marker_present(root / "fail_game"))
                        forge::asset_storage::replace(folder / "frame-000002.png",
                                                      "invalid game PNG");
                    play.stop();
                };
                host.pixels = [&](unsigned) {
                    const auto tick = play.timing().at("tick").get<unsigned>();
                    forge::Rgba8Pixels value{7, 5, std::vector<std::uint8_t>(140), true};
                    for (unsigned i = 0; i < 35; ++i) {
                        value.rgba[i * 4] = std::uint8_t(30 + tick * 35);
                        value.rgba[i * 4 + 1] = std::uint8_t(i * 4);
                        value.rgba[i * 4 + 2] = std::uint8_t(220 - tick * 30);
                        value.rgba[i * 4 + 3] = 255;
                    }
                    forge::ui::ViewImageTask::PixelInput input;
                    input.source = {{"fixture", "detached runtime tick colors"}};
                    input.load = [value = std::move(value)](forge::ViewImageFormat) {
                        return value;
                    };
                    return input;
                };
                return std::make_unique<forge::ui::GameRecordingTask>(
                    scene, files.document, play, std::filesystem::u8path(argv[4]), request,
                    std::move(host));
            };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            return forge::CaptureAutomation::methods(images, scene, files, providers, message,
                                                     permissions);
        };
        forge::ui::AutomationPermissions permissions;
        permissions.export_game = output;
        permissions.execute = output && argc == 6;
        if (output)
            permissions.export_root = root / "Output";
        workspace.start(scene, files, output, asset_write, permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        std::string accepted_launch;
        std::uint64_t accepted_ticket = 0;
        while (workspace.active() && std::chrono::steady_clock::now() < end &&
               !forge::test::marker_present(root / "stop")) {
            files.changed = false;
            play.pump();
            if (play.launch_id() != accepted_launch) {
                accepted_launch = play.launch_id();
                accepted_ticket = 0;
            }
            if (play.sdk_candidate_ticket() && play.sdk_candidate_ticket() != accepted_ticket &&
                !play.sdk_candidate_envelope().is_null()) {
                if (!play.submit_editor_observation(false, "KeyboardMouse") ||
                    !play.submit_sdk_candidate_ack(true, play.session(),
                                                   play.sdk_candidate_ticket()))
                    throw std::runtime_error("MCP recording candidate admission failed");
                accepted_ticket = play.sdk_candidate_ticket();
            }
            if (!forge::test::marker_present(root / "hold_game"))
                images.poll_work(nullptr, message);
            if (!images.busy() && !play.active() && !play.user_data_override().empty())
                throw std::runtime_error("Recording leaked its private user-data selection");
            images.poll_files(message);
            if (!forge::test::marker_present(root / "hold_pixels"))
                forge::test::ViewImageTaskAccess::deliver_test_pixels(images, message,
                                                                      frames.cursor);
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
