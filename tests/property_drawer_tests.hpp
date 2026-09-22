#pragma once
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
void require(bool condition, const char* message);
inline void test_asset_picker() {
    using namespace forge;
    AssetCatalog catalog(std::filesystem::current_path());
    std::vector<AssetRecord> records;
    for (unsigned i = 0; i < 2000; ++i)
        records.push_back({AssetId::generate(),
                           "mesh",
                           "Assets/Item_" + std::to_string(10000 + i) + ".mesh",
                           1,
                           {}});
    const auto wanted = records.back().id;
    const auto texture = AssetId::generate();
    records.push_back({texture, "texture", "Assets/Item_11999.texture", 1, {}});
    catalog.replace_all(std::move(records));
    const auto matching = asset_picker_entries(catalog, "mesh", "ITEM_11999", false);
    require(matching.size() == 1 && matching[0].id == wanted,
            "Asset picker search did not filter type and case-insensitive source name");
    require(asset_picker_entries(catalog, "mesh", "Engine /", false).empty() &&
                !asset_picker_entries(catalog, "mesh", "Engine /", true).empty(),
            "Asset picker ignored engine visibility policy");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Json value;
    ImVec2 combo{};
    ImGuiWindow* popup = nullptr;
    bool committed = false;
    const char* title = "Mesh test";
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 500});
        ImGui::Begin("Asset picker test");
        combo = ImGui::GetCursorScreenPos();
        combo.x += 30;
        combo.y += ImGui::GetFrameHeight() * .5f;
        ImGui::SetNextItemWidth(500);
        committed |= asset_ref_picker(catalog, value, "mesh", title, false);
        ImGui::End();
        ImGui::Render();
        popup = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->Active && !window->Hidden && (window->Flags & ImGuiWindowFlags_Popup) &&
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
    click(combo);
    frame();
    require(popup && ImGui::GetDrawData()->TotalVtxCount < 5000,
            "Asset picker did not clip its 2000-row popup");
    click({popup->Pos.x + popup->WindowPadding.x + 30,
           popup->Pos.y + popup->WindowPadding.y + ImGui::GetFrameHeight() * .5f});
    io.AddInputCharactersUTF8("Item_11999");
    frame();
    frame();
    require(popup, "Searching unexpectedly closed the asset picker");
    click({popup->Pos.x + popup->WindowPadding.x + 30,
           popup->Pos.y + popup->WindowPadding.y + ImGui::GetFrameHeightWithSpacing() +
               ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() * .5f});
    require(committed && value == Json(wanted), "Visible picker search result was not assigned");
    committed = false;
    click(combo);
    frame();
    require(popup, "Assigned asset picker did not reopen");
    require(popup->Scroll.y == 0,
            "Selected asset focus scrolled the search and Clear controls out of view");
    click({popup->Pos.x + popup->WindowPadding.x + 30, popup->Pos.y + popup->WindowPadding.y +
                                                           ImGui::GetFrameHeightWithSpacing() +
                                                           ImGui::GetTextLineHeight() * .5f});
    require(committed && value.is_null(), "Asset picker Clear did not remove the reference");
    value = texture;
    committed = false;
    frame();
    require(!committed && value == Json(texture), "Inspection rewrote an incompatible asset ref");
    // Each new field scope starts with an empty search. The last of 2000 rows
    // is selected, so default focus must scroll only the result child.
    value = wanted;
    unsigned scale_index = 0;
    for (const char* field : {"Selected mesh 100", "Selected mesh 150", "Selected mesh 200"}) {
        title = field;
        ui::style(1.f + .5f * float(scale_index++));
        frame();
        frame();
        click(combo);
        frame();
        frame();
        require(popup && popup->Scroll.y == 0 && ImGui::GetDrawData()->TotalVtxCount < 5000,
                "Large selected asset list hid search/Clear or stopped clipping at UI scale");
        ImGui::ClosePopupToLevel(0, true);
    }
    ui::style(1);
    ImGui::DestroyContext();
}
inline void test_typed_ui_layer() {
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    const std::string entity = forge::authoring_command(scene, "entity.create").at("selected");
    forge::authoring_command(scene, "component.add",
                             {{"entity", entity}, {"component", "forge.ui_document"}});
    forge::Json field;
    const auto schema = scene.schema();
    for (const auto& c : schema.at("components"))
        if (c.at("id") == "forge.ui_document")
            for (const auto& f : c.at("fields"))
                if (f.at("id") == "layer")
                    field = f;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::Json value = 0u;
    ImVec2 target{};
    bool committed = false;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 300});
        ImGui::Begin("Typed field test");
        if (forge::property_field({}, field, value)) {
            forge::authoring_command(scene, "property.set",
                                     {{"entity", entity},
                                      {"component", "forge.ui_document"},
                                      {"field", "layer"},
                                      {"value", value}});
            committed = true;
        }
        target = ImGui::GetItemRectMin();
        target.x += 20;
        target.y += 8;
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    io.AddMousePosEvent(target.x, target.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    io.AddInputCharactersUTF8("7");
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    require(committed && value.is_number_unsigned() && value == 7u,
            "Actual UI layer input did not commit an unsigned integer");
    forge::authoring_history(scene, false);
    require(scene.effective_document()
                    .at("entities")[0]
                    .at("components")
                    .at("forge.ui_document")
                    .at("layer") == 0u,
            "Layer edit did not undo");
    ImGui::DestroyContext();
}
inline void test_reflected_value_inputs() {
    using forge::Json;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Json field, value;
    ImVec2 target{};
    bool committed = false;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 500});
        ImGui::Begin("Reflected value inputs");
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        committed |= forge::property_field({}, field, value);
        target = ImGui::GetItemRectMin();
        target.x += 20;
        target.y += 8;
        ImGui::End();
        ImGui::Render();
    };
    auto press = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frame();
    };
    auto enter = [&](const char* text, bool append = false) {
        committed = false;
        frame();
        frame();
        io.AddMousePosEvent(target.x, target.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        press(append ? ImGuiKey_End : ImGuiKey_A);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        io.AddInputCharactersUTF8(text);
        frame();
        press(ImGuiKey_Enter);
    };
    for (const auto& [type, initial, input, expected] :
         std::vector<std::tuple<std::string, Json, const char*, Json>>{
             {"int8", 0, "-128", -128},
             {"uint8", 0u, "255", 255u},
             {"int16", 0, "-32768", -32768},
             {"uint16", 0u, "65535", 65535u},
             {"uint64", 0u, "18446744073709551615", UINT64_MAX}}) {
        field = {{"id", type}, {"type", type}};
        value = initial;
        enter(input);
        require(committed && value == expected, "Typed reflected input lost integer width/value");
    }
    field = {{"id", "long-text"}, {"type", "string"}};
    value = std::string(5000, 'x');
    enter("y", true);
    require(committed && value == std::string(5000, 'x') + "y",
            "Editing a long reflected string truncated its existing contents");
    field = {{"id", "nested"},
             {"type", "struct"},
             {"fields", Json::array({{{"id", "count"}, {"type", "uint64"}}})}};
    value = {{"count", 0u}, {"unknown", "preserved"}};
    enter("9007199254740993");
    require(committed && value.at("count") == UINT64_C(9007199254740993) &&
                value.at("unknown") == "preserved",
            "Nested property input rounded an integer or discarded unknown fields");
    field = {{"id", "locked"}, {"type", "int32"}, {"read_only", true}};
    value = 7;
    enter("9");
    require(!committed && value == 7, "Read-only reflected field remained editable");
    ImGui::DestroyContext();
}
