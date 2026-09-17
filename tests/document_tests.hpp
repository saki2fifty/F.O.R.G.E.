#pragma once
#include "files.hpp"
#include <chrono>
void require(bool condition, const char* message);
inline void test_documents() {
    const auto root = std::filesystem::current_path() /
                      ("document-tests-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{root};
    auto expect_failure = [](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Expected file operation rejection");
    };
    const auto project = root / "My Game";
    forge::SceneDocument::create_project(project, "My Game");
    require(std::filesystem::exists(project / "forge.project.json") &&
                std::filesystem::is_directory(project / "Assets") &&
                std::filesystem::is_directory(project / "Native"),
            "Project scaffold incomplete");
    expect_failure([&] { forge::SceneDocument::create_project(project, "Replace"); });
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    auto doc = std::make_unique<forge::SceneDocument>(scene);
    doc->open_project(project);
    require(!doc->dirty() && doc->name() == "My Game", "Project opens dirty or with wrong name");
    const auto original = scene.document();
    auto edited = original;
    edited["entities"].push_back({{"id", "55555555-5555-4555-8555-555555555555"},
                                  {"name", "Test"},
                                  {"components",
                                   {{"forge.local_translation", {{"x", 1}, {"y", 2}, {"z", 3}}},
                                    {"unknown", {{"keep", true}}}}}});
    scene.edit(edited);
    require(doc->dirty(), "Edit not marked dirty");
    scene.undo();
    require(!doc->dirty(), "Undo to save point stayed dirty");
    scene.redo();
    require(doc->dirty(), "Redo did not restore dirty state");
    require(doc->autosave() && !doc->autosave(), "Autosave failed or rewrote unchanged revision");
    require(forge::read_json(doc->path()) == original, "Autosave overwrote scene file");
    const auto recovery = doc->recovery_path();
    expect_failure([&] { forge::ProjectLease competitor(project); });
    doc.reset();
    {
        forge::EngineContext restarted_engine;
        forge::Scene restarted(restarted_engine.world());
        forge::SceneDocument resumed(restarted);
        resumed.open_project(project);
        require(resumed.has_recovery(), "Restart cannot find recovery");
        resumed.recover();
        require(resumed.dirty() && restarted.document() == edited, "Recovery lost unsaved data");
        require(restarted.undo() && restarted.document() == original, "Recovery is not undoable");
        restarted.redo();
        resumed.save();
        require(!resumed.dirty() && !resumed.has_recovery(), "Save did not clear dirty/recovery");
    }
    doc = std::make_unique<forge::SceneDocument>(scene);
    doc->open_project(project);
    const auto old_path = doc->path();
    expect_failure([&] { doc->open_scene(root / "outside.json"); });
    forge::atomic_write(project / "broken.json", "{invalid");
    expect_failure([&] { doc->open_scene(project / "broken.json"); });
    require(doc->path() == old_path && scene.document() == edited, "Failed open replaced document");
    expect_failure([&] { doc->save_as(project / "forge.project.json"); });
    expect_failure([&] { doc->save_as(project / ".forge/metadata.json"); });
    expect_failure([&] { doc->save_as(root / "outside.json"); });
    auto external = edited;
    external["entities"][0]["name"] = "External edit";
    forge::atomic_write(doc->path(), external.dump());
    expect_failure([&] { doc->save(); });
    require(forge::read_json(doc->path()) == external, "Save clobbered external changes");
    std::filesystem::remove(doc->path());
    expect_failure([&] { doc->save(); });
    forge::atomic_write(old_path, external.dump());
    doc->save_as(project / "Scenes/copy.scene.json");
    require(!doc->dirty() && scene.asset_id().str() != edited.at("asset_id").get<std::string>() &&
                scene.document()["entities"][0]["components"] ==
                    edited["entities"][0]["components"],
            "Save As did not preserve content with fresh identity");
    const auto copy_id = scene.document()["entities"][0]["id"].get<std::string>();
    doc->new_scene();
    require(doc->path().empty() && doc->dirty() && !scene.undo(),
            "New scene carried history or a filename");
    const auto untitled = forge::duplicate_scene_asset(edited);
    scene.edit(untitled);
    require(doc->autosave(), "Untitled autosave failed");
    doc.reset();
    {
        forge::EngineContext restarted_engine;
        forge::Scene restarted(restarted_engine.world());
        forge::SceneDocument resumed(restarted);
        resumed.open_project(project);
        require(resumed.has_untitled_recovery(), "Untitled recovery missing after restart");
        resumed.recover_untitled();
        require(resumed.path().empty() && resumed.dirty() && restarted.document() == untitled,
                "Untitled recovery failed");
        resumed.save_as(project / "Scenes/recovered.scene.json");
        require(!resumed.has_untitled_recovery(), "Save As left stale untitled recovery");
    }
    doc = std::make_unique<forge::SceneDocument>(scene);
    doc->open_project(project);
    doc->open_scene(project / "Scenes/copy.scene.json");
    scene.rename_entity(copy_id, "Dirty");
    doc->autosave();
    forge::atomic_write(doc->recovery_path(), "invalid");
    const auto before_bad_recovery = scene.document();
    expect_failure([&] { doc->recover(); });
    require(scene.document() == before_bad_recovery && doc->has_recovery(),
            "Invalid recovery mutated scene or disappeared");
    doc->discard_recovery();
    doc.reset();
    std::vector<std::string> recent;
    forge::EditorFiles files(scene, nullptr, recent);
    files.start(project);
    scene.rename_entity("55555555-5555-4555-8555-555555555555", "Pending");
    files.request({forge::EditorFiles::Command::Quit, {}, {}});
    require(!files.quit && files.busy(), "Dirty quit bypassed save guard");
    files.resolve_pending(forge::EditorFiles::Resolution::Cancel);
    require(!files.quit && !files.busy() && files.document.dirty(), "Cancel lost edits or quit");
    files.request({forge::EditorFiles::Command::OpenScene, project / "Scenes/copy.scene.json", {}});
    files.resolve_pending(forge::EditorFiles::Resolution::Save);
    require(!files.document.dirty() && files.document.path().filename() == "copy.scene.json" &&
                forge::read_json(project / "Scenes/main.scene.json")["entities"][0]["name"] ==
                    "Pending",
            "Save-and-continue did not save before switching");
    scene.rename_entity(copy_id, "Keep on failed open");
    files.document.autosave();
    files.request({forge::EditorFiles::Command::OpenScene, project / "broken.json", {}});
    files.resolve_pending(forge::EditorFiles::Resolution::Discard);
    require(files.document.dirty() && files.document.has_recovery(),
            "Failed switch discarded recovery");
    files.pump(false);
    files.request({forge::EditorFiles::Command::NewScene, {}, {}});
    require(!files.busy() && files.document.dirty(), "Busy native build allowed document switch");
    files.pump(true);
    files.request({forge::EditorFiles::Command::OpenScene, project / "Scenes/copy.scene.json", {}});
    files.resolve_pending(forge::EditorFiles::Resolution::Discard);
    require(!files.document.dirty() && !files.document.has_recovery(),
            "Discard/reload did not restore saved scene");
    forge::atomic_write(project / ".forge/recovery-invalid.json", "not a scene");
    expect_failure([&] { files.document.open_scene(project / ".forge/recovery-invalid.json"); });
    require(recent.size() == 1 && recent[0] == forge::path_text(files.document.project()),
            "Recent projects not recorded");
    // Real editor lifecycle for legacy migration, recovery and Save As publication failure.
    const auto legacy_project = root / "Legacy";
    forge::SceneDocument::create_project(legacy_project, "Legacy");
    const auto legacy_path = legacy_project / "Scenes/main.scene.json";
    const auto legacy_id = std::string("old-object");
    const forge::Json v1 = {
        {"version", 1},
        {"entities",
         forge::Json::array({{{"id", legacy_id},
                              {"name", "Before"},
                              {"components", {{"plugin.missing", {{"entity", legacy_id}}}}}}})}};
    forge::atomic_write(legacy_path, v1.dump());
    forge::atomic_write(
        legacy_project / "forge.project.json",
        forge::Json{{"version", 1}, {"name", "Legacy"}, {"startup_scene", "Scenes/main.scene.json"}}
            .dump());
    forge::Json assigned;
    {
        forge::EngineContext legacy_engine;
        forge::Scene legacy_scene(legacy_engine.world());
        forge::SceneDocument legacy_document(legacy_scene);
        legacy_document.open_project(legacy_project);
        assigned = legacy_scene.document();
        require(!legacy_document.dirty() && forge::read_json(legacy_path) == v1,
                "Opening legacy file dirtied or replaced source");
        auto recovery = v1;
        recovery["entities"][0]["name"] = "Recovered";
        forge::atomic_write(
            legacy_document.recovery_path(),
            forge::Json{{"version", 1},
                        {"scene", forge::path_text(legacy_path.lexically_relative(legacy_project))},
                        {"base", v1},
                        {"document", recovery}}
                .dump());
        legacy_document.recover();
        require(legacy_scene.document()["entities"][0]["id"] == assigned["entities"][0]["id"] &&
                    legacy_scene.document()["entities"][0]["name"] == "Recovered",
                "Legacy recovery changed assigned identity or lost content");
        require(legacy_scene.undo() && !legacy_document.dirty(), "Legacy recovery undo failed");
        legacy_document.discard_recovery();
        const auto destination = legacy_project / "Scenes/failed-copy.scene.json";
        std::filesystem::create_directory(destination.string() + ".pending");
        expect_failure([&] { legacy_document.save_as(destination); });
        require(legacy_document.path() == legacy_path && legacy_scene.document() == assigned &&
                    !std::filesystem::exists(destination),
                "Failed Save As changed live identity/path");
    }
    {
        forge::EngineContext legacy_engine;
        forge::Scene legacy_scene(legacy_engine.world());
        forge::SceneDocument legacy_document(legacy_scene);
        legacy_document.open_project(legacy_project);
        require(legacy_scene.document() == assigned, "Project reopen regenerated legacy UUIDs");
        legacy_document.save();
        require(forge::read_json(legacy_path) == assigned && !legacy_document.dirty(),
                "Save did not persist retained migration");
        require(forge::read_json(legacy_path.string() + ".v1.backup") == v1,
                "Original backup lost");
    }
}
