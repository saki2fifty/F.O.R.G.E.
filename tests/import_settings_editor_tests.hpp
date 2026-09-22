#pragma once
#include "import_settings.hpp"
void require(bool condition, const char* message);
inline void test_import_settings_editor() {
    using namespace forge;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 800};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const ImportSettingsSchema schema{
        "test.shader",
        1,
        {{"permutation", "Permutation", "Choose the shader combination.",
          ImportSettingType::StringMap, Json::object()}}};
    const std::map<std::string, std::vector<std::string>> axes{{"QUALITY", {"LOW", "HIGH"}}};
    for (const float scale : {1.f, 1.5f, 2.f}) {
        ui::style(scale);
        ImportSettingsDocument draft{"test.shader"};
        std::string error;
        bool activate = false;
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({850, 700});
            ImGui::Begin("Import settings test");
            if (activate) {
                ui::IdScope rule("permutation"), axis("QUALITY");
                ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID("##axis")),
                                   ImGuiPopupFlags_None);
                activate = false;
            }
            ui::import_settings_fields(schema, draft, error, &axes);
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        require(draft.overrides.empty(), "Drawing shader axes invented a default selection");
        activate = true;
        frame();
        frame();
        const auto* popup = ImGui::GetTopMostPopupModal();
        require(!popup, "Permutation field unexpectedly opened a modal");
        const auto& open = ImGui::GetCurrentContext()->OpenPopupStack;
        require(!open.empty() && open.back().Window && !open.back().Window->Hidden,
                "Shader axis dropdown did not open at this UI scale");
        const auto* choices = open.back().Window;
        const ImVec2 high{choices->Pos.x + choices->WindowPadding.x + 25,
                          choices->Pos.y + choices->WindowPadding.y +
                              ImGui::GetTextLineHeightWithSpacing() +
                              ImGui::GetTextLineHeight() * .5f};
        io.AddMousePosEvent(high.x, high.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        require(error.empty() && draft.overrides.at("permutation").at("QUALITY") == "HIGH",
                "Shader axis interaction did not edit the typed settings draft");
        require(ImGui::GetCurrentContext()->OpenPopupStack.empty(),
                "Shader permutation selection left a popup open");
        // Domain-invalid old axes must remain visible/preserved until an explicit edit.
        draft = schema.edit(draft, "permutation", Json{{"REMOVED_AXIS", "OLD"}});
        const auto prior = Json(draft);
        frame();
        require(Json(draft) == prior, "Drawing obsolete shader settings silently rewrote them");
    }
    ui::style(1);
    ImGui::DestroyContext();
}
