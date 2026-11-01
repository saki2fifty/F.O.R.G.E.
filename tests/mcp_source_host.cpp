#include "animation_blend_space_editor.hpp"
#include "animation_mask_editor.hpp"
#include "animation_states_editor.hpp"
#include "animation_timeline.hpp"
#include "audio_imports.hpp"
#include "automation.hpp"
#include "collision_editor.hpp"
#include "content_imports.hpp"
#include "editable_mesh_editor.hpp"
#include "import_document_automation.hpp"
#include "material_editor.hpp"
#include "material_graph_editor.hpp"
#include "navigation_behavior_editor.hpp"
#include "particle_effect_editor.hpp"
#include "prefab_automation.hpp"
#include "source_file_lifecycle_tests.hpp"
#include "source_publication_tests.hpp"
#include "source_workflow_automation.hpp"
#include "sprite_atlas_editor.hpp"
#include "terrain_editor.hpp"
#include "texture_imports.hpp"
#include "tile_map_editor.hpp"
#include "tile_set_editor.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto worker = std::filesystem::u8path(argv[2]);
        const bool read_only = std::string(argv[3]) == "read";
        const bool publication = argc == 5 && std::string(argv[3]) == "publication";
        const bool file_changes = argc == 5 && std::string(argv[3]) == "file_changes";
        if (publication || file_changes) {
            if (!std::filesystem::create_directory(root / "Game"))
                throw std::runtime_error("Publication fixture requires an empty test directory");
            std::filesystem::copy(argv[4], root / "Game", std::filesystem::copy_options::recursive);
        } else
            forge::SceneDocument::create_project(root / "Game", "MCP Sources");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1500, 1000};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        forge::ui::EditorUiContext context;
        forge::ui::ContextScope scope(context);
        forge::PrefabEditor prefab;
        forge::AudioImportEditor audio(worker);
        forge::TextureImportEditor textures(worker);
        forge::SpriteAtlasEditor atlas(worker);
        forge::MaterialEditor material;
        forge::MaterialGraphEditor graph(
            worker, [] { return forge::asset_detail::ShaderCompilerProfile{}; });
        forge::CollisionEditor collision;
        forge::EditableMeshEditor mesh;
        forge::ParticleEffectEditor particles;
        forge::TerrainEditor terrain;
        forge::TileSetEditor tiles(worker);
        forge::TileMapEditor map(worker);
        forge::AnimationStatesEditor states;
        forge::NavigationBehaviorEditor behavior;
        forge::AnimationBlendSpaceEditor blend;
        forge::AnimationMaskEditor mask;
        forge::AnimationTimelineEditor timeline;
        forge::ui::DocumentWorkspace documents;
        auto bind = [&](const char* id, auto& owner) {
            forge::ui::WorkspaceDocument entry;
            entry.id = entry.title = id;
            entry.is_open = [&owner] { return owner.is_open(); };
            entry.dirty = [&owner] { return owner.dirty(); };
            entry.source = forge::AssetDocumentAutomation::bind(owner, files.document);
            documents.add(std::move(entry));
        };
        bind("material", material);
        bind("material.graph", graph);
        bind("collision", collision);
        bind("editable_mesh", mesh);
        bind("particle_effect", particles);
        bind("terrain", terrain);
        bind("tile_set", tiles);
        bind("sprite_atlas", atlas);
        bind("tile_map", map);
        bind("animation_states", states);
        bind("navigation_behavior", behavior);
        bind("animation_blend_space", blend);
        bind("animation_mask", mask);
        bind("animation_timeline", timeline);
        forge::ui::WorkspaceDocument audio_entry;
        audio_entry.id = audio_entry.title = "audio_import";
        audio_entry.dirty = [&] { return audio.dirty(); };
        audio_entry.importer = forge::ImportDocumentAutomation::bind(audio, files.document);
        documents.add(std::move(audio_entry));
        forge::ui::WorkspaceDocument texture_entry;
        texture_entry.id = texture_entry.title = "texture_import";
        texture_entry.dirty = [&] { return textures.dirty(); };
        texture_entry.importer = forge::ImportDocumentAutomation::bind(textures, files.document);
        documents.add(std::move(texture_entry));
        if (publication) {
            test_source_publication(documents, files.document);
            test_import_publication(documents, files.document, audio);
            std::cout << "Source publication lifecycle passed for five source editors and the "
                         "shared import editor\n";
            return 0;
        }
        if (file_changes) {
            test_source_file_lifecycle(documents, files.document, scene);
            std::cout << "Reviewed source Duplicate/Move/Delete editor lifecycle passed\n";
            return 0;
        }
        // Exercise the same external-source publication owner as Content.
        forge::ContentImports imports;
        imports.routes = [] {
            return std::vector<forge::AssetReimportRoute>{
                {"forge.animation.transform", forge::desktop_texture_target(),
                 forge::transform_animation_import_registry(),
                 [](auto& candidate, const auto& plan, const auto&) {
                     forge::prepare_transform_animation_publication(candidate, plan);
                 }},
                {"forge.mesh.editable", forge::desktop_texture_target(),
                 forge::editable_mesh_import_registry(),
                 [](auto& candidate, const auto& plan, const auto&) {
                     forge::prepare_editable_mesh_publication(candidate, plan);
                 }}};
        };
        imports.blocked = [&](forge::AssetId id) {
            return documents.source_publication_blocked(id);
        };
        imports.published = [&](forge::AssetId id, auto) { documents.source_published(id); };
        files.automation_unavailable = [&](bool switching) {
            return switching && documents.source_drafts_dirty() ? "Settle source drafts" : "";
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            auto methods = forge::source_document_automation_methods(documents, permissions);
            auto settings = forge::import_document_automation_methods(documents, permissions);
            methods.insert(methods.end(), std::make_move_iterator(settings.begin()),
                           std::make_move_iterator(settings.end()));
            auto prefabs =
                forge::PrefabAutomation::methods(prefab, scene, files.document, permissions);
            methods.insert(methods.end(), std::make_move_iterator(prefabs.begin()),
                           std::make_move_iterator(prefabs.end()));
            return methods;
        };
        forge::ui::AutomationPermissions permissions;
        permissions.assets = !read_only;
        workspace.start(scene, files, !read_only, !read_only, permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        // The complete source suite exercises every real owner before the
        // client closes it; keep this watchdog within the outer CTest budget.
        const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            workspace.pump(files.document, "");
            audio.poll(files.document, files.status);
            textures.poll(files.document, files.status);
            atlas.poll(files.document, files.status);
            if (auto catalog = textures.take_catalog())
                imports.catalog_changed(std::move(catalog));
            if (auto catalog = atlas.take_catalog())
                imports.catalog_changed(std::move(catalog));
            material.poll(files.document, files.status);
            collision.poll(files.document, files.status);
            mesh.poll(files.document, files.status);
            particles.poll(files.document, files.status);
            terrain.poll(files.document, files.status);
            tiles.poll(files.document, files.status);
            map.poll(files.document, files.status);
            states.poll(files.document, files.status);
            behavior.poll(files.document, files.status);
            blend.poll(files.document, files.status);
            mask.poll(files.document, files.status);
            timeline.poll(files.document, scene, false, 0, files.status);
            if (auto catalog = mesh.take_catalog())
                imports.catalog_changed(std::move(catalog));
            if (auto catalog = timeline.take_catalog())
                imports.catalog_changed(std::move(catalog));
            imports.poll(files.document, files.status);
            // No renderer/shader worker profile: graph source/history is exercised,
            // graph compilation remains a separate platform acceptance boundary.
            ImGui::NewFrame();
            audio.draw(files.document, false);
            textures.draw(files.document, false);
            atlas.draw(files.document, false);
            material.draw(files.document, false);
            collision.draw(files.document, false);
            mesh.draw(files.document, false);
            particles.draw(files.document, scene, false);
            terrain.draw(files.document, scene, false);
            tiles.draw(files.document, false);
            map.draw(files.document, scene, false);
            states.draw(files.document, scene, false);
            behavior.draw(files.document, scene, false);
            blend.draw(files.document, scene, false);
            mask.draw(files.document, scene, false);
            timeline.draw(files.document, scene, false);
            ImGui::Render();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
