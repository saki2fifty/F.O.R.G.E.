#include <stdexcept>
void require(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
#include "batch_authoring_tests.hpp"
#include "framing_tests.hpp"
#include "hierarchy.hpp"
#include "scene_cache_tests.hpp"
#include "scene_tools.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <type_traits>

using namespace forge;
namespace {
void check_selection(const ui::EditorSelection& selection,
                     const std::vector<std::string>& expected) {
    require(selection.entity_count() == expected.size() && selection.entities() == expected,
            "Selection order/count changed");
    for (const auto& id : expected)
        require(selection.contains(id), "Selected membership was lost");
    require(!selection.contains("") && !selection.contains("not selected"),
            "Selection admitted an absent member");
    auto copy = selection.entities();
    copy.clear();
    require(selection.entities() == expected, "Owning selection copy became an alias");
}
void check_selection_reconcile() {
    Json source{
        {"entities", Json::array({{{"id", "a"}}, {{"id", "b"}}, {{"id", "c"}}, {{"id", "d"}}})}};
    const auto original = source;
    const std::vector<std::string> visible{"a", "b", "c", "d"};
    ui::EditorSelection selection;
    selection.click_entity("a", false);
    selection.click_entity("d", false, true, visible);
    for (unsigned i = 0; i < 3; ++i)
        selection.reconcile(source);
    check_selection(selection, visible);
    selection.click_entity("c", false, true, visible);
    check_selection(selection, {"a", "b", "c"});

    selection.click_entity("a", false);
    selection.click_entity("d", false, true, visible);
    auto pruned = source;
    pruned["entities"].erase(1);
    selection.reconcile(pruned);
    check_selection(selection, {"a", "c", "d"});
    selection.click_entity("c", false, true, {"a", "c", "d"});
    check_selection(selection, {"a", "c"});
    selection.entity_slot() = "b";
    selection.reconcile(source);
    check_selection(selection, {"b"});
    selection.reconcile(pruned);
    check_selection(selection, {});

    const auto asset = AssetId::generate();
    selection.select_asset(asset);
    selection.reconcile(Json());
    require(selection.kind() == ui::SelectionKind::Asset && selection.asset() == asset,
            "Empty entity reconciliation changed asset selection");
    selection.entity_slot() = "c";
    selection.reconcile(source);
    check_selection(selection, {"c"});
    require(!selection.asset() && selection.member().empty() && selection.document().empty(),
            "Legacy entity reconciliation retained a previous selection owner");
    selection.entity_slot().clear();
    selection.reconcile(Json());
    check_selection(selection, {});
    selection.select_entities({"missing", "stale"});
    selection.reconcile(source);
    check_selection(selection, {});
    require(source == original, "Selection reconciliation changed the authored document");

    EngineContext engine;
    Scene scene(engine.world());
    const std::string first = authoring_command(scene, "entity.create").at("selected");
    const std::string second = authoring_command(scene, "entity.create").at("selected");
    AuthoringSnapshot snapshot;
    selection.select_entities({first, second});
    selection.reconcile(snapshot.document(scene));
    check_selection(selection, {first, second});
    authoring_command(scene, "entity.delete", {{"entity", second}});
    selection.reconcile(snapshot.document(scene));
    check_selection(selection, {first});
    require(scene.undo(), "Delete had no Undo step");
    selection.click_entity(second, true);
    selection.reconcile(snapshot.document(scene));
    check_selection(selection, {first, second});
    require(scene.redo(), "Delete had no Redo step");
    selection.reconcile(snapshot.document(scene));
    check_selection(selection, {first});
    EngineContext other_engine;
    Scene other(other_engine.world());
    selection.reconcile(snapshot.document(other));
    check_selection(selection, {});
}
void check_selection_overlays(Scene& scene, const std::vector<std::string>& ids) {
    auto arranged = scene.document();
    for (std::size_t i = 0; i < ids.size(); ++i)
        arranged["entities"][i]["components"]["forge.local_translation"] = {
            {"x", double(i) - 2}, {"y", 0}, {"z", double(i) * .5}};
    auto view = scene.preview_document(arranged);
    view["entities"][1]["spatial_resolved"] = false;
    view["entities"][2]["components"].erase("forge.position");
    view["entities"][3]["prefab"] = true;
    view["entities"][4]["components"]["forge.mesh_renderer"]["enabled"] = false;
    const auto original = view;
    auto reversed = view;
    auto& reversed_rows = reversed["entities"].get_ref<Json::array_t&>();
    std::reverse(reversed_rows.begin(), reversed_rows.end());
    ui::SceneTools tools;
    EditorCamera camera;
    auto draw = [&](const Json& source, const std::vector<std::string>& selection) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("Overlay fixture", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
        tools.draw(source, camera, ids.back(), {10, 10}, {760, 550}, true, selection);
        ImGui::End();
        ImGui::Render();
        Json vertices = Json::array(), indices = Json::array(), commands = Json::array();
        for (const auto* list : ImGui::GetDrawData()->CmdLists) {
            for (const auto& vertex : list->VtxBuffer)
                vertices.push_back(
                    {vertex.pos.x, vertex.pos.y, vertex.uv.x, vertex.uv.y, vertex.col});
            for (const auto index : list->IdxBuffer)
                indices.push_back(index);
            for (const auto& command : list->CmdBuffer)
                commands.push_back({command.ElemCount, command.IdxOffset, command.VtxOffset,
                                    command.ClipRect.x, command.ClipRect.y, command.ClipRect.z,
                                    command.ClipRect.w});
        }
        return Json{{"vertices", vertices}, {"indices", indices}, {"commands", commands}};
    };
    draw(view, ids); // Settle the actual window before comparing drawing output.
    draw(view, ids);
    for (const auto& selected :
         std::vector<std::vector<std::string>>{{},
                                               {ids.back()},
                                               {ids.front(), ids.back()},
                                               ids,
                                               {"missing", ids.back(), ids.front()},
                                               {ids.back(), ids.front(), ids.back()}})
        require(draw(view, selected) == draw(reversed, selected),
                "Selection overlays depend on scene row order or stale IDs");
    auto reordered = ids;
    std::reverse(reordered.begin(), reordered.end());
    require(draw(view, ids) != draw(view, reordered),
            "Selection overlay drawing lost selection order");
    require(view == original, "Selection drawing changed its borrowed projection");
}
} // namespace
int main() {
    try {
        static_assert(
            std::is_same_v<decltype(std::declval<const ui::EditorSelection&>().entities()),
                           std::vector<std::string>>);
        static_assert(
            std::is_same_v<decltype(std::declval<const ui::EditorSelection&>().entity_count()),
                           std::size_t>);
        test_batch_authoring(); // Existing batch/Undo/prefab/spatial selection coverage.
        test_framing_selection();
        test_scene_cache(); // Existing native-write, reset, Undo/Redo and owner-invalidation
                            // checks.
        check_selection_reconcile();
        ui::EditorSelection direct;
        check_selection(direct, {});
        direct.select_entities({"b", "a", "b", "", "c"});
        check_selection(direct, {"b", "a", "c"});
        direct.entity_slot() = "legacy";
        check_selection(direct, {"legacy"});
        require(!direct.contains("a"), "Legacy writer retained old multi-selection membership");
        direct.entity_slot().clear();
        check_selection(direct, {});
        direct.select_asset(AssetId::generate());
        check_selection(direct, {});
        direct.select_document_item("document", "node");
        check_selection(direct, {});
        direct.select_member(AssetId::generate(), "member");
        check_selection(direct, {});

        EngineContext engine;
        Scene scene(engine.world());
        for (unsigned i = 0; i < 6; ++i)
            authoring_command(
                scene, "entity.create",
                {{"name", std::string(i % 2 ? "Other " : "Match ") + std::to_string(i)}});
        const auto before = scene.document();
        const auto revision = scene.revision();
        std::vector<std::string> order, filtered;
        for (const auto& entity : before.at("entities")) {
            order.push_back(entity.at("id"));
            if (entity.at("name").get<std::string>().starts_with("Match"))
                filtered.push_back(entity.at("id"));
        }
        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() {
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
        } cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(1);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        test::observe_ui = true;
        std::string filter;
        auto frame = [&] {
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 450});
            ImGui::Begin("Selection fixture");
            ui::hierarchy(before, context.selection.entity_slot(), filter, 1, &scene, false,
                          &context.selection);
            ImGui::End();
            ImGui::Render();
        };
        auto click = [&](const std::string& id, bool toggle = false, bool range = false) {
            const auto box = test::ui_targets.at("entity:" + id);
            const float x = (box.minimum.x + box.maximum.x) * .5f;
            const float y = (box.minimum.y + box.maximum.y) * .5f;
            require(x >= box.clip_minimum.x && x <= box.clip_maximum.x && y >= box.clip_minimum.y &&
                        y <= box.clip_maximum.y,
                    "Hierarchy selection target clipped");
            io.AddMousePosEvent(x, y);
            io.AddKeyEvent(ImGuiMod_Ctrl, toggle);
            io.AddKeyEvent(ImGuiMod_Shift, range);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            frame();
        };
        frame();
        frame();
        click(order[0]);
        check_selection(context.selection, {order[0]});
        click(order[2], true);
        check_selection(context.selection, {order[0], order[2]});
        click(order[4], false, true);
        check_selection(context.selection, {order[2], order[3], order[4]});
        click(order[3], true);
        check_selection(context.selection, {order[2], order[4]});
        require(context.selection.entity() == order[4], "Toggle changed the surviving primary");
        filter = "Match";
        frame();
        click(filtered.front());
        click(filtered.back(), false, true);
        check_selection(context.selection, filtered);
        context.selection.entity_slot() = order[1];
        frame();
        check_selection(context.selection, {order[1]});
        context.selection.entity_slot().clear();
        frame();
        check_selection(context.selection, {});
        check_selection_overlays(scene, order);
        require(scene.document() == before && scene.revision() == revision,
                "Hierarchy selection changed authored state/history");
        std::cout
            << "Selection queries preserve legacy, owning-copy and actual Ctrl/Shift UI behavior\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
