#include "asset_automation.hpp"
#include "automation.hpp"
#include "fbx_fixture.hpp"
#include "import_authoring.hpp"
#include "material_editor.hpp"
#include "mcp_test_marker.hpp"
#include "source_workflow_automation.hpp"
#include "texture_authoring.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

// Actual owning controllers without a renderer; markers only control test timing.
int main(int argc, char** argv) {
    try {
        if (argc != 5)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto worker = std::filesystem::u8path(argv[2]);
        const auto converter = std::filesystem::u8path(argv[3]);
        const bool read_only = std::string(argv[4]) == "read";
        forge::SceneDocument::create_project(root / "Game", "MCP Assets");
        std::filesystem::create_directory(root / "Sources");
        for (const auto& [name, text] :
             {std::pair{"native.fbx", forge::test::fbx_animated_triangle(true)},
              std::pair{"native-texture.fbx", forge::test::fbx_textured_triangle("wide.png")}}) {
            std::ofstream output(root / "Sources" / name, std::ios::binary);
            if (!output.write(text.data(), std::streamsize(text.size())) || !output.flush())
                throw std::runtime_error("Cannot write original FBX MCP fixture");
        }
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> imgui(
            ImGui::CreateContext(), ImGui::DestroyContext);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* font_pixels;
        int font_width, font_height;
        io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
        forge::ui::EditorUiContext ui_context;
        forge::ui::ContextScope ui_scope(ui_context);
        forge::MaterialEditor material;
        forge::ui::DocumentWorkspace documents;
        forge::ui::WorkspaceDocument material_document;
        material_document.id = material_document.title = "material";
        material_document.is_open = [&] { return material.is_open(); };
        material_document.dirty = [&] { return material.dirty(); };
        material_document.source = forge::AssetDocumentAutomation::bind(material, files.document);
        documents.add(std::move(material_document));
        forge::ContentImports imports;
        forge::SourceImport source;
        source.atlas_worker = worker;
        forge::ContentFiles content_files;
        forge::SceneAssetDrop placement;
        forge::ui::EditorSelection selection;
        imports.routes = [worker, converter] {
            const auto registry = forge::asset_import_registry(worker, converter);
            std::vector<forge::AssetReimportRoute> routes;
            for (const auto& descriptor : registry->descriptors())
                routes.push_back({descriptor.id, forge::desktop_texture_target(), registry,
                                  [](auto& candidate, const auto& plan, const auto& catalog) {
                                      forge::prepare_asset_publication(candidate, plan, catalog);
                                  }});
            return routes;
        };
        source.routes = imports.routes;
        files.external_busy = [&] {
            return source.busy() || content_files.busy() || placement.busy();
        };
        content_files.unavailable = [&](const forge::AssetRecord& asset,
                                        forge::AssetFileAction action) -> std::string {
            if (files.busy() || forge::test::marker_present(root / "dirty_source"))
                return "Finish the active source work";
            if (action == forge::AssetFileAction::Delete && asset.id == scene.asset_id())
                return "Open another scene before deleting the active source";
            return {};
        };
        content_files.adopted = [&](const auto& review, const auto&, auto) {
            if (review.plan.request.action == forge::AssetFileAction::Move) {
                const auto old =
                    std::find_if(review.plan.changes.begin(), review.plan.changes.end(),
                                 [](const auto& change) { return change.before && !change.after; });
                if (old != review.plan.changes.end())
                    files.document.source_relocated(old->source, review.plan.request.destination);
            }
        };
        imports.blocked = [&](forge::AssetId id) {
            return forge::test::marker_present(root / "dirty_source") ||
                   documents.source_publication_blocked(id);
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            auto methods = forge::asset_automation_methods(scene, files, imports, source, placement,
                                                           content_files, permissions);
            auto sources = forge::source_document_automation_methods(documents, permissions);
            methods.insert(methods.end(), std::make_move_iterator(sources.begin()),
                           std::make_move_iterator(sources.end()));
            return methods;
        };
        forge::ui::AutomationPermissions permissions;
        permissions.assets = !read_only;
        permissions.import_root = root / "Sources";
        std::string message;
        imports.poll(files.document, message);
        workspace.start(scene, files, !read_only, !read_only, permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(175);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            material.poll(files.document, message);
            if (auto catalog = material.take_catalog())
                imports.catalog_changed(std::move(catalog));
            if (!forge::test::marker_present(root / "pause_placement"))
                placement.poll(scene, files, selection, false, message);
            content_files.poll(files.document, scene, imports, message);
            imports.suspend(content_files.busy() || source.busy());
            source.poll(files.document, imports, message);
            imports.poll(files.document, message);
            workspace.pump(files.document, files.busy() ? "Finish the active file operation" : "");
            ImGui::NewFrame();
            material.draw(files.document, source.busy() || content_files.busy());
            ImGui::Render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
