#pragma once
#include "command_workspace.hpp"
#include "creation_menu.hpp"
#include "document_workspace.hpp"
#include "status_bar.hpp"
#include "workspace.hpp"
void require(bool, const char*);
inline void test_document_workspace() {
    using namespace forge::ui;
    DocumentWorkspace docs;
    bool open = true, dirty = true;
    int saves = 0, undos = 0, inspections = 0, draws = 0;
    WorkspaceDocument d;
    d.id = "test.document";
    d.title = "Test document";
    d.is_open = [&] { return open; };
    d.dirty = [&] { return dirty; };
    d.save = [&] {
        ++saves;
        dirty = false;
    };
    d.can_undo = [] { return true; };
    d.undo = [&] { ++undos; };
    d.inspect = [&](const std::string& key) {
        require(key == "local-node", "Local inspection key changed");
        ++inspections;
    };
    d.draw = [&] { ++draws; };
    docs.add(d);
    require(docs.save(d.id) && saves == 1 && !dirty, "Document Save was not dispatched");
    require(docs.undo(d.id, false) && undos == 1 && !docs.undo(d.id, true),
            "Document history capability ignored");
    EditorSelection selection;
    selection.select_entity("previous");
    selection.select_document_item(d.id, "local-node");
    require(selection.entity().empty() && selection.kind() == SelectionKind::DocumentItem &&
                docs.inspect(selection.document(), selection.member()),
            "Local selection retained scene target");
    require(inspections == 1, "Inspector adapter skipped");
    docs.draw();
    require(draws == 1, "Document draw owner skipped");
    open = false;
    require(!docs.save(d.id) && !docs.undo(d.id, false) && !docs.inspect(d.id, "local-node"),
            "Closed document accepts operations");
    bool rejected = false;
    try {
        docs.add(d);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "Duplicate document route admitted");
    EditorUiContext context;
    context.selection.select_document_item(d.id, "local-node");
    context.task.focus_document(d.id, d.title);
    docs.remove(d.id, &context);
    require(!docs.find(d.id) && context.selection.kind() == SelectionKind::None &&
                context.task.owner == DocumentTask::Scene,
            "Retired document retained selection/history target");
    int first_closes = 0, second_closes = 0;
    bool first_dirty = true, second_dirty = true, cancelled = false;
    WorkspaceDocument first;
    first.id = "source.first";
    first.dirty = [&] { return first_dirty; };
    first.request_close = [&] { ++first_closes; };
    first.take_close_cancelled = [&] { return std::exchange(cancelled, false); };
    docs.add(first);
    WorkspaceDocument second;
    second.id = "source.second";
    second.dirty = [&] { return second_dirty; };
    second.request_close = [&] { ++second_closes; };
    docs.add(second);
    WorkspaceDocument scene;
    scene.id = "scene";
    scene.dirty = [] { return true; };
    docs.add(scene);
    require(!docs.close_pending_sources() && first_closes == 1 && second_closes == 0,
            "Independent source guards ran concurrently");
    first_dirty = false;
    require(!docs.close_pending_sources() && second_closes == 1, "Next source guard was omitted");
    cancelled = true;
    require(docs.consume_close_cancellation() && !docs.consume_close_cancellation(),
            "Source close cancellation was lost or repeated");
    second_dirty = false;
    require(docs.close_pending_sources(), "Source guard consumed scene file ownership");
    AssetEditors editors;
    int opens = 0;
    editors.add({"scene", "Open scene", [&](const forge::AssetRecord&) { ++opens; }});
    forge::AssetRecord asset;
    asset.type = "scene";
    require(editors.open(asset) && opens == 1, "Registered asset open failed");
    asset.type = "not-implemented";
    require(!editors.open(asset), "Unimplemented asset editor advertised");
}
inline void test_refinement_chrome() {
    using namespace forge::ui;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::Telemetry stats;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f})
        for (float width : {960.f, 1440.f, 2560.f}) {
            style(scale);
            io.DisplaySize = {width, 900};
            ImGui::NewFrame();
            if (begin_toolbar()) {
                ImGui::MenuItem("File");
                end_toolbar();
            }
            auto* menu = ImGui::FindWindowByName("##FORGE-toolbar");
            require(menu->Size.y <= ImGui::GetFrameHeight() + 5 * scale, "Menu chrome inflated");
            status_bar(stats, false, 3, "EDIT", 1, true, false);
            require(ImGui::FindWindowByName("##FORGE-status")->Size.y <=
                        ImGui::GetTextLineHeight() + 9 * scale,
                    "Status bar wrapped into multiple rows");
            ImGui::SetNextWindowSize({260 * scale, 500});
            ImGui::Begin("Icons");
            for (int icon = 0; icon <= int(Icon::More); ++icon) {
                icon_button(("##icon" + std::to_string(icon)).c_str(), Icon(icon), "Icon help",
                            icon == 1);
                toolbar_next();
            }
            ImGui::End();
            ImGui::Render();
        }
    style(1);
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = {1440, 900};
    Workspace workspace;
    workspace.console = false;
    bool initialize = true;
    auto frame = [&] {
        ImGui::NewFrame();
        status_bar(
            stats, false, 1, "EDIT", 0, true, false, [&] { workspace.toggle_bottom(); },
            workspace.bottom_folded);
        const auto dock = ImGui::DockSpaceOverViewport();
        if (initialize) {
            initialize_workspace(dock);
            initialize = false;
        }
        ImGui::Begin("Scene###Scene");
        ImGui::TextUnformatted("Primary document");
        ImGui::End();
        if (!workspace.bottom_folded) {
            ImGui::Begin("Content");
            ImGui::TextUnformatted("Assets");
            ImGui::End();
        }
        ImGui::Render();
    };
    for (int i = 0; i < 4; ++i)
        frame();
    const auto scene_height = ImGui::FindWindowByName("Scene###Scene")->Size.y;
    const auto content_height = ImGui::FindWindowByName("Content")->Size.y;
    const auto content_dock = ImGui::FindWindowByName("Content")->DockId;
    workspace.toggle_bottom();
    for (int i = 0; i < 4; ++i)
        frame();
    require(ImGui::FindWindowByName("Scene###Scene")->Size.y > scene_height + 100,
            "Folded bottom workspace did not return space to document");
    Workspace restored;
    restored.load({{"panels", workspace.settings()}});
    require(restored.bottom_folded && restored.content && !restored.console,
            "Folding lost personal panel visibility/persistence");
    workspace.toggle_bottom();
    for (int i = 0; i < 4; ++i)
        frame();
    require(ImGui::FindWindowByName("Content")->DockId == content_dock &&
                std::abs(ImGui::FindWindowByName("Content")->Size.y - content_height) < 2,
            "Restoring bottom workspace lost remembered dock/height");
    ImGui::DestroyContext();
}

inline void test_creation_entry_menu() {
    using namespace forge::ui;
    ImGui::CreateContext();
    style(1);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    EditorUiContext context;
    EditorActions actions;
    bool at_target = false;
    const char* route = "Scene Add";
    ImVec2 first;
    auto frame = [&] {
        actions.entries.clear();
        for (const auto& entry : palette_entries("", 1, {2, 3, 4}, at_target))
            if (entry.operation == "entity.create")
                actions.entries.push_back(
                    {entry.label,
                     entry.label,
                     "",
                     entry.help,
                     true,
                     [&, entry] {
                         context.selection.select_entity(
                             forge::authoring_command(scene, entry.operation, entry.arguments)
                                 .at("selected"));
                         context.task.owner = DocumentTask::Scene;
                     },
                     {}});
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({400, 500});
        ImGui::Begin(route);
        first = ImGui::GetCursorScreenPos();
        creation_menu(actions, at_target);
        ImGui::End();
        ImGui::Render();
    };
    for (const char* name : {"Scene Add", "Hierarchy Add", "Entity Create", "Hierarchy context"})
        for (bool target : {false, true}) {
            route = name;
            at_target = target;
            context.task.focus_document("other.draft", "Other draft");
            context.selection.select_asset(forge::AssetId::generate());
            const auto before = scene.document();
            frame();
            frame();
            io.AddMousePosEvent(first.x + 40, first.y + ImGui::GetTextLineHeight() * .5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            require(context.task.owner == DocumentTask::Scene &&
                        context.selection.kind() == SelectionKind::Entity,
                    "Create menu retained other document/asset selection");
            const auto after = scene.document();
            require(after.at("entities").size() == before.at("entities").size() + 1,
                    "Create entry did not invoke recipe");
            const auto& row = after.at("entities").back();
            require(row.at("components").at("forge.primitive").at("kind") == forge::no_primitive,
                    "Empty menu recipe changed composition");
            require(row.at("components").at("forge.local_translation").at("y") == (target ? 3 : 0),
                    "Create menu ignored placement");
            require(scene.undo() && scene.document() == before && scene.redo() &&
                        scene.document() == after,
                    "Create menu bypassed atomic history");
            scene.undo();
        }
    ImGui::DestroyContext();
}
