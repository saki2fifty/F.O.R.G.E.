#include "editor/asset_automation.hpp"
#include "editor/automation.hpp"
#include "editor/capture_automation.hpp"
#include "mcp_test_marker.hpp"
#include "reflection_capture_recording.hpp"
#include "texture_authoring.hpp"
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            return 2;
        const auto root = std::filesystem::absolute(argv[1]);
        const auto worker = std::filesystem::absolute(argv[2]);
        const bool assets = std::string(argv[3]) == "assets";
        forge::SceneDocument::create_project(root / "Game", "Reflection capture protocol");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
            ImGui::CreateContext(), ImGui::DestroyContext);
        ImGui::GetIO().IniFilename = nullptr;
        forge::SourceImport source;
        const auto registry = forge::texture_import_registry(worker);
        const forge::AssetReimportRoute route{
            "forge.texture.container", forge::desktop_texture_target(), registry,
            [](auto& candidate, const auto& plan, const auto&) {
                forge::prepare_texture_publication(candidate, plan);
            }};
        source.routes = [route] { return std::vector{route}; };
        forge::ContentImports imports;
        imports.routes = source.routes;
        auto catalog = std::make_shared<const forge::AssetCatalog>(
            forge::AssetCatalog::open_project(files.document.project()));
        source.published = [&](forge::AssetId, auto value) { catalog = std::move(value); };
        imports.published = source.published;
        forge::ContentFiles content_files;
        forge::SceneAssetDrop placement;
        forge::ui::ViewImageTask images;
        std::string message;
        forge::CaptureAutomationProviders providers;
        providers.reflection = [&](const std::string& probe,
                                   const forge::ui::ReflectionCaptureRequest& settings) {
            if (source.busy())
                throw forge::AuthoringError("capture_busy", "Finish the current source import");
            auto task = std::make_unique<forge::ui::ReflectionCaptureTask>(
                scene, files.document, source, probe, settings,
                [&](forge::RenderScene, const auto& request, forge::EntityId) {
                    return std::make_unique<forge::test::RecordingReflectionCapture>(
                        request.size,
                        [&] { return forge::test::marker_present(root / "hold_pixels"); });
                },
                [&] { return catalog; }, [] { return std::string{}; });
            task->reveal(false);
            if (!images.begin_work(std::move(task), files.document.project(), scene.asset_id(),
                                   message))
                throw forge::AuthoringError("capture_busy", "Finish the current image task");
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            auto methods = forge::CaptureAutomation::methods(images, scene, files, providers,
                                                             message, permissions);
            auto values = forge::asset_automation_methods(scene, files, imports, source, placement,
                                                          content_files, permissions);
            methods.insert(methods.end(), std::make_move_iterator(values.begin()),
                           std::make_move_iterator(values.end()));
            return methods;
        };
        forge::ui::AutomationPermissions permissions;
        permissions.assets = assets;
        workspace.start(scene, files, assets, assets, permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (workspace.active() && std::chrono::steady_clock::now() < end &&
               !forge::test::marker_present(root / "stop")) {
            files.changed = false;
            images.poll_work(nullptr, message);
            source.poll(files.document, imports, message);
            imports.suspend(source.busy());
            imports.poll(files.document, message);
            workspace.pump(files.document, "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
