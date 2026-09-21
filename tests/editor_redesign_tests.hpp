#pragma once
#include "actions.hpp"
#include "component_inspector.hpp"
#include "content.hpp"
#include "prefabs.hpp"
#include "project_settings.hpp"
#include "workspace.hpp"
void require(bool condition, const char* message);
inline void test_editor_selection_and_actions() {
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    const std::string entity = forge::authoring_command(scene, "entity.create").at("selected");
    forge::ui::EditorSelection selection;
    selection.select_entity(entity);
    require(selection.kind() == forge::ui::SelectionKind::Entity, "Entity scope missing");
    const auto asset = forge::AssetId::generate();
    selection.select_asset(asset);
    require(selection.entity().empty() && selection.kind() == forge::ui::SelectionKind::Asset,
            "Asset retained contradictory entity selection");
    selection.select_member(asset, "member");
    require(selection.entity().empty() &&
                selection.kind() == forge::ui::SelectionKind::PrefabMember,
            "Prefab selection scope missing");
    selection.select_entity(entity);
    forge::authoring_command(scene, "entity.delete", {{"entity", entity}});
    selection.reconcile(scene.document());
    require(selection.kind() == forge::ui::SelectionKind::None,
            "Deleted entity left stale selection");
    forge::ui::Workspace original;
    original.game = false;
    original.problems = false;
    forge::ui::Workspace restored;
    restored.load({{"panels", original.settings()}});
    require(restored.settings() == original.settings(), "New panel visibility did not round-trip");
    const auto migrated = forge::ui::migrate_layout(
        "[Window][Prefab source]\nPos=25,30\nSize=500,600\n[Window][Project "
        "Settings]\nPos=50,60\n");
    require(migrated.find("[Window][Prefab source###Prefab source]") != std::string::npos &&
                migrated.find("Pos=25,30") != std::string::npos &&
                migrated.find("[Window][Project Settings###Project Settings]") != std::string::npos,
            "Draft window migration lost custom layout");
    forge::ui::EditorActions actions;
    int executed = 0;
    actions.entries.push_back({"test", "Test", "", "test", false, [&] { ++executed; }});
    require(!actions.invoke("test") && executed == 0, "Unavailable action executed");
    actions.entries[0].available = true;
    require(actions.invoke("test") && executed == 1, "Available action failed");
    forge::ui::Problems problems;
    problems.ingest({"same", "Error", "failure", {}, {}, {}, {}});
    problems.ingest({"same", "Error", "failure", {}, {}, {}, {}});
    require(problems.size() == 1, "Repeated diagnostic flooded Problems");
    problems.clear();
    problems.ingest({"same", "Error", "failure", {}, {}, {}, {}});
    require(problems.size() == 0, "Dismissed existing diagnostic immediately reappeared");
}
inline void test_redesign_drawers() {
    const auto root =
        std::filesystem::current_path() / ("redesign-" + forge::AssetId::generate().str());
    forge::SceneDocument::create_project(root, "Editor redesign");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{root};
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    std::vector<std::string> recent;
    forge::EditorFiles files(scene, nullptr, recent);
    files.start(root);
    const std::string entity = forge::authoring_command(scene, "entity.create").at("selected");
    const auto schema = scene.schema();
    for (const auto& c : schema.at("components"))
        if (c.value("optional", false)) {
            // NavigationAgent and PhysicsBody intentionally conflict. Draw each separately below.
            require(c.contains("display_name") && c.contains("category"),
                    "Registered schema lacks component discovery metadata");
        }
    forge::ui::EditorUiContext context;
    context.scene = &scene;
    forge::ui::ContextScope scope(context);
    context.selection.select_entity(entity);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440, 1000};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::ComponentInspector inspector;
    forge::ContentBrowser content;
    content.refresh(files);
    const auto prefab_asset = files.document.prefabs().create(
        scene, forge::create_prefab_source(scene, entity), "Assets/New.prefab.json");
    require(content.resolve_record(files, prefab_asset) != nullptr,
            "New prefab missing from asset Inspector before Content refresh");
    require(content.record(prefab_asset)->type == "prefab", "Prefab asset type lost in Content");
    {
        forge::PrefabEditor draft;
        draft.edit_source(files.document, prefab_asset);
        require(context.selection.kind() == forge::ui::SelectionKind::PrefabMember,
                "Prefab member scope missing");
        draft.request_close();
        require(context.selection.kind() == forge::ui::SelectionKind::Asset &&
                    context.selection.asset() == prefab_asset,
                "Closed source retained stale prefab member selection");
        context.selection.select_entity(entity);
    }
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1440, 200});
        content.draw(files);
        auto* window = ImGui::FindWindowByName("Content");
        ImGuiWindow* results = nullptr;
        for (auto* child : ImGui::GetCurrentContext()->Windows)
            if (child->ParentWindow == window && child->ChildId == window->GetID("content-results"))
                results = child;
        require(results &&
                    results->InnerClipRect.GetHeight() >= 3 * ImGui::GetTextLineHeightWithSpacing(),
                "Content filters hide asset rows in the default bottom workspace");
        ImGui::Render();
    }
    for (const auto& c : schema.at("components"))
        if (c.value("optional", false)) {
            const std::string key = c.at("id");
            forge::authoring_command(scene, "component.add",
                                     {{"entity", entity}, {"component", key}});
            for (int i = 0; i < 2; ++i) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize({600, 900});
                ImGui::Begin("All typed components");
                inspector.draw(scene, files.document, entity);
                ImGui::End();
                content.draw(files);
                ImGui::Render();
                require(ImGui::GetDrawData()->TotalVtxCount > 0, "Redesign widgets did not draw");
            }
            forge::authoring_command(scene, "component.revert",
                                     {{"entity", entity}, {"component", key}});
        }
    forge::authoring_command(scene, "component.add",
                             {{"entity", entity}, {"component", "forge.ui_document"}});
    const auto before_invalid = scene.document();
    inspector.edit_property(scene, entity, "forge.ui_document", "layer", -1);
    require(scene.document() == before_invalid && context.problems.size() == 1,
            "Invalid Inspector value changed authored state or did not produce a Problem");
    require(context.problems.items().front().entity == entity &&
                context.problems.items().front().property == "forge.ui_document.layer",
            "Property Problem lost navigation context");
    ImGui::NewFrame();
    ImGui::Begin("Error fixture");
    ImGui::LogToBuffer();
    inspector.draw(scene, files.document, entity);
    const std::string error_text = GImGui->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    require(error_text.find("Error:") != std::string::npos,
            "Inspector validation error was not drawn beside its field");
    inspector.edit_property(scene, entity, "forge.ui_document", "layer", 4u);
    require(context.problems.size() == 0 && scene.effective_document()
                                                    .at("entities")[0]
                                                    .at("components")
                                                    .at("forge.ui_document")
                                                    .at("layer") == 4u,
            "Correcting the field did not apply or resolve its Problem");
    forge::authoring_history(scene, false);
    require(scene.document() == before_invalid, "Rejected edit polluted scene Undo");
    ImGui::DestroyContext();
}
inline void test_draft_ownership() {
    using namespace forge;
    const auto root = std::filesystem::current_path() / ("drafts-" + AssetId::generate().str());
    SceneDocument::create_project(root, "Independent drafts");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{root};
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const std::string entity = authoring_command(scene, "entity.create").at("selected");
    const auto prefab = document.prefabs().create(scene, create_prefab_source(scene, entity),
                                                  "Assets/Test.prefab.json");
    instantiate_prefab(scene, prefab);
    document.save();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440, 1000};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    ProjectSettingsEditor settings;
    PrefabEditor source;
    settings.open();
    source.edit_source(document, prefab);
    std::string status;
    auto draw = [&] {
        ImGui::NewFrame();
        settings.draw(document, scene, false, status);
        source.draw(scene, document, false);
        ImGui::Render();
    };
    draw();
    draw();
    const auto before = scene.document();
    settings.set_frequency(90);
    source.rename_member("Edited draft");
    require(settings.dirty() && source.dirty() && !document.dirty(),
            "Draft changes dirtied the scene or failed to mark their owner");
    document.save();
    require(settings.dirty() && source.dirty(), "Scene Save consumed another document's draft");
    source.edit_source(document, prefab);
    require(source.dirty(), "Reopening the same prefab lost the draft");
    settings.request_close();
    source.request_close();
    require(!settings.resolve_close(ui::DraftResolution::Cancel, document, status) &&
                settings.is_open() && settings.dirty(),
            "Cancel discarded settings");
    require(!source.resolve_close(ui::DraftResolution::Cancel, scene, document) &&
                source.is_open() && source.dirty(),
            "Cancel discarded prefab");
    require(scene.document() == before, "Unpublished drafts changed scene state");
    settings.set_frequency(0);
    require(!settings.resolve_close(ui::DraftResolution::Save, document, status) &&
                settings.is_open() && settings.dirty() && document.settings().simulation_hz() == 60,
            "Invalid settings closed or published");
    settings.set_frequency(90);
    require(settings.resolve_close(ui::DraftResolution::Save, document, status) &&
                !settings.is_open() && document.settings().simulation_hz() == 90,
            "Settings Save failed to close after publication");
    require(source.resolve_close(ui::DraftResolution::Discard, scene, document) &&
                !source.is_open(),
            "Prefab Discard failed");
    require(document.prefabs().source(prefab).at("members")[0].at("name") != "Edited draft",
            "Discard published the draft");
    source.edit_source(document, prefab);
    source.rename_member("Published draft");
    require(source.resolve_close(ui::DraftResolution::Save, scene, document),
            "Prefab publication failed");
    require(document.prefabs().source(prefab).at("members")[0].at("name") == "Published draft" &&
                !scene.can_undo(),
            "Prefab Save ownership/history boundary incorrect");
    source.edit_source(document, prefab);
    source.set_property("forge.local_scale", "x", -10001.0);
    const auto good = document.prefabs().source(prefab);
    require(!source.resolve_close(ui::DraftResolution::Save, scene, document) && source.is_open() &&
                source.dirty() && document.prefabs().source(prefab) == good,
            "Invalid prefab draft replaced known good source or closed");
    ImGui::DestroyContext();
}

inline void test_responsive_xyz() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {960, 640};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (float scale : {1.f, 2.f})
        for (float width : {200.f, 600.f}) {
            forge::ui::style(scale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({width, 500});
            ImGui::Begin("Responsive XYZ");
            double values[] = {0, -9.81, 0};
            forge::ui::xyz_input("Gravity", values, "X/Y/Z gravity");
            require(ImGui::GetItemRectMax().x <= ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                    "XYZ field escaped a narrow panel");
            require(values[1] == -9.81, "Drawing responsive fields changed authored values");
            ImGui::End();
            ImGui::Render();
        }
    forge::ui::style(1);
    ImGui::DestroyContext();
}

inline void test_initial_content_tab() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440, 900};
    io.DeltaTime = 1.f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        const auto dock = ImGui::DockSpaceOverViewport();
        if (!frame)
            forge::ui::initialize_workspace(dock);
        for (const char* name :
             {"Hierarchy###World", "Inspector", "Scene###Scene", "Game", "Content",
              "Problems###Problems", "Gameplay Code###Native", "Console"}) {
            ImGui::Begin(name);
            ImGui::TextUnformatted(name);
            ImGui::End();
        }
        if (!frame)
            ImGui::SetWindowFocus("Content");
        ImGui::Render();
    }
    require(ImGui::FindWindowByName("Content")->DockTabIsVisible,
            "First-use dock tabs stole default Content focus");
    ImGui::DestroyContext();
}
