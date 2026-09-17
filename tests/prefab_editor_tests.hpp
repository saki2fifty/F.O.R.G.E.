#pragma once
#include "prefabs.hpp"
#include "scene_cache.hpp"
void require(bool condition, const char* message);
inline void test_prefab_editor_documents() {
    const auto scratch =
        std::filesystem::current_path() / ("prefab-editor-" + forge::AssetId::generate().str());
    std::filesystem::create_directory(scratch);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(p, ignored);
        }
    } cleanup{scratch};
    const auto project = scratch / "Game";
    forge::SceneDocument::create_project(project, "Prefab game");
    forge::WorldContext world;
    forge::Scene scene(world);
    forge::SceneDocument document(scene);
    document.open_project(project);
    auto selected = forge::authoring_command(scene, "entity.create", {{"name", "Reusable"}})
                        .at("selected")
                        .get<std::string>();
    forge::authoring_command(scene, "component.add",
                             {{"entity", selected}, {"component", "forge.audio_source"}});
    auto source = forge::create_prefab_source(scene, selected);
    const auto asset = document.prefabs().create(scene, source, "Assets/Sample.prefab.json");
    selected = forge::instantiate_prefab(scene, asset);
    document.save();
    const auto saved = scene.document();
    forge::authoring_command(scene, "transform.position",
                             {{"entity", selected}, {"value", {{"x", 4}, {"y", 2}, {"z", 0}}}});
    require(document.autosave(), "Prefab override was not autosaved");
    const auto intended = scene.document();
    document.open_scene(document.path());
    require(scene.document() == saved, "Prefab scene reopening lost source/mapping");
    document.recover();
    require(scene.document() == intended && scene.can_undo(),
            "Prefab override recovery/undo missing");
    forge::AuthoringSnapshot snapshot;
    require(snapshot.snapshot(scene).contains("_prefab_sources") &&
                !snapshot.document(scene).contains("_prefab_sources"),
            "Transient definitions leaked into disk document or missing from play snapshot");
    auto next = source.source;
    next["members"][0]["name"] = "Published name";
    document.prefabs().publish(scene, source.source, next);
    require(snapshot.snapshot(scene).at("_prefab_sources")[0]["revision"] == 2,
            "Play cache missed source publication");
    require(!scene.can_undo(), "Scene history claims to undo source publication");
    const auto other = scratch / "Invalid";
    forge::SceneDocument::create_project(other, "Invalid");
    forge::atomic_write(other / "Assets/Broken.prefab.json", "{}");
    const auto before = scene.snapshot();
    const auto revision = scene.revision();
    bool rejected = false;
    try {
        document.open_project(other);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && document.project() == project && scene.snapshot() == before &&
                scene.revision() == revision,
            "Failed project scan changed open scene");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 900};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::PrefabEditor editor;
    editor.edit_source(document, asset);
    for (int frame = 0; frame < 3; ++frame) {
        if (frame == 1)
            forge::atomic_write(project / "forge.assets.json", "malformed");
        if (frame == 2)
            std::filesystem::remove(project / "forge.assets.json");
        ImGui::NewFrame();
        ImGui::Begin("Prefab content test");
        editor.content(scene, document, selected, false);
        ImGui::End();
        ImGui::Begin("Prefab inspector test");
        auto doc = scene.document();
        for (const auto& row : doc.at("entities"))
            if (row.at("id") == selected)
                editor.inspector(scene, document, row);
        std::string audio_message;
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        forge::audio_inspector(scene, document, selected, audio_message);
        require(audio_message.empty(), "Audio Inspector failed to draw inherited source");
        ImGui::End();
        editor.draw(scene, document, false);
        ImGui::Render();
        require(ImGui::GetDrawData()->TotalVtxCount > 0, "Prefab UI smoke draw empty");
    }
    ImGui::DestroyContext();
}
