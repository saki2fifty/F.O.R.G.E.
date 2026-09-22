#pragma once
#include "entity_ref_picker.hpp"
void require(bool condition, const char* message);
inline void test_entity_picker() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    Json document = {{"version", 1}, {"entities", Json::array()}};
    for (unsigned i = 0; i < 2000; ++i) {
        Json entity = {{"id", EntityId::generate()},
                       {"name", "Item_" + std::to_string(10000 + i)},
                       {"components", Json::object()}};
        if (i > 0)
            entity["parent"] = document["entities"][0]["id"];
        document["entities"].push_back(std::move(entity));
    }
    scene.reset(document);
    document = scene.document();
    const auto wanted = document.at("entities").back().at("id").get<EntityId>();
    const auto root = document.at("entities")[0].at("id").get<EntityId>();
    const auto matches = entity_picker_entries(document, "ITEM_10000 / ITEM_11999");
    require(matches.size() == 1 && matches[0].id == wanted,
            "Entity search did not match its case-insensitive hierarchy path");
    require(entity_picker_entries(document, "Item_10000").size() == 2000 &&
                entity_picker_entries(document, wanted.str()).size() == 1,
            "Entity search lost ancestor or persistent ID matches");
    const Json missing = EntityRef{scene.asset_id(), EntityId::generate()};
    const Json foreign = EntityRef{AssetId::generate(), wanted};
    require(entity_ref_label(scene, missing).starts_with("Missing entity") &&
                entity_ref_label(scene, foreign).starts_with("Another scene") &&
                entity_ref_label(scene, Json{{"invalid", true}}) == "Invalid entity reference",
            "Entity reference diagnostics conflated missing, foreign and malformed targets");
    // A deep projection uses bounded display paths, not recursive traversal or
    // unbounded repeated ancestor strings; distant ancestor search still works.
    auto deep = document;
    for (std::size_t i = 1; i < deep["entities"].size(); ++i)
        deep["entities"][i]["parent"] = deep["entities"][i - 1]["id"];
    const auto deep_rows = entity_picker_entries(deep, "Item_10000");
    require(deep_rows.size() == 2000, "Deep hierarchy search lost descendants");
    for (const auto& row : deep_rows)
        require(row.label.size() <= 1024, "Deep hierarchy display paths grew without a bound");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Json value = foreign;
    ImVec2 combo{};
    ImGuiWindow* popup = nullptr;
    bool committed = false;
    const auto revision = scene.revision();
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 500});
        ImGui::Begin("Entity picker test");
        combo = ImGui::GetCursorScreenPos();
        combo.x += 30;
        combo.y += ImGui::GetFrameHeight() * .5f;
        ImGui::SetNextItemWidth(500);
        committed |= entity_ref_picker(scene, value, "Entity test");
        ImGui::End();
        ImGui::Render();
        popup = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->Active && !window->Hidden &&
                std::string_view(window->Name).starts_with("##Combo_"))
                popup = window;
    };
    auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    frame();
    frame();
    require(!committed && value == foreign, "Inspection rewrote a cross-scene reference");
    click(combo);
    frame();
    require(popup && ImGui::GetDrawData()->TotalVtxCount < 6000,
            "Entity picker did not clip its 2000-row hierarchy");
    click({popup->Pos.x + popup->WindowPadding.x + 30,
           popup->Pos.y + popup->WindowPadding.y + ImGui::GetFrameHeight() * .5f});
    io.AddInputCharactersUTF8("Item_11999");
    frame();
    frame();
    require(popup, "Entity hierarchy search closed its picker");
    click({popup->Pos.x + popup->WindowPadding.x + 30,
           popup->Pos.y + popup->WindowPadding.y + ImGui::GetFrameHeightWithSpacing() +
               ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() * .5f});
    require(committed && value == Json(EntityRef{scene.asset_id(), wanted}),
            "Entity picker did not assign the visible result's persistent scene/entity IDs");
    require(entity_ref_label(scene, value) == "Item_11999",
            "Entity picker preview lost the assigned name");
    committed = false;
    click(combo);
    frame();
    require(popup, "Entity picker did not reopen after assignment");
    click({popup->Pos.x + popup->WindowPadding.x + 30, popup->Pos.y + popup->WindowPadding.y +
                                                           ImGui::GetFrameHeightWithSpacing() +
                                                           ImGui::GetTextLineHeight() * .5f});
    require(committed && value.is_null(), "Entity picker Clear retained its reference");
    require(scene.revision() == revision && scene.reference(root.str()).entity == root,
            "A reference draft edit mutated the scene directly");
    ImGui::DestroyContext();
}
