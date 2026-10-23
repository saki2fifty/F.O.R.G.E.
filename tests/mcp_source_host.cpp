#include "animation_blend_space_editor.hpp"
#include "animation_mask_editor.hpp"
#include "animation_states_editor.hpp"
#include "animation_timeline.hpp"
#include "audio_imports.hpp"
#include "automation.hpp"
#include "collision_editor.hpp"
#include "editable_mesh_editor.hpp"
#include "import_document_automation.hpp"
#include "material_editor.hpp"
#include "material_graph_editor.hpp"
#include "particle_effect_editor.hpp"
#include "prefab_automation.hpp"
#include "source_workflow_automation.hpp"
#include "terrain_editor.hpp"
#include "tile_map_editor.hpp"
#include "tile_set_editor.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc != 4)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto worker = std::filesystem::u8path(argv[2]);
        const bool read_only = std::string(argv[3]) == "read";
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
        bind("tile_map", map);
        bind("animation_states", states);
        bind("animation_blend_space", blend);
        bind("animation_mask", mask);
        bind("animation_timeline", timeline);
        forge::ui::WorkspaceDocument audio_entry;
        audio_entry.id = audio_entry.title = "audio_import";
        audio_entry.dirty = [&] { return audio.dirty(); };
        audio_entry.importer = forge::ImportDocumentAutomation::bind(audio, files.document);
        documents.add(std::move(audio_entry));
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
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(175);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            workspace.pump(files.document, "");
            audio.poll(files.document, files.status);
            material.poll(files.document, files.status);
            collision.poll(files.document, files.status);
            mesh.poll(files.document, files.status);
            particles.poll(files.document, files.status);
            terrain.poll(files.document, files.status);
            tiles.poll(files.document, files.status);
            map.poll(files.document, files.status);
            states.poll(files.document, files.status);
            blend.poll(files.document, files.status);
            mask.poll(files.document, files.status);
            timeline.poll(files.document, scene, false, 0, files.status);
            // No renderer/shader worker profile: graph source/history is exercised,
            // graph compilation remains a separate platform acceptance boundary.
            ImGui::NewFrame();
            audio.draw(files.document, false);
            material.draw(files.document, false);
            collision.draw(files.document, false);
            mesh.draw(files.document, false);
            particles.draw(files.document, scene, false);
            terrain.draw(files.document, scene, false);
            tiles.draw(files.document, false);
            map.draw(files.document, scene, false);
            states.draw(files.document, scene, false);
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
