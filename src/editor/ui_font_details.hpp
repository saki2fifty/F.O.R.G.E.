#pragma once
#include "widgets.hpp"
#include <forge/assets.hpp>
#include <string_view>

namespace forge::ui {
inline void draw_ui_font_details(const AssetRecord& asset) {
    heading("Font faces", "Recorded names from the saved font. Use its zero-based face index "
                          "in @font-face; the family alias in your style is independent.");
    const auto source = asset.metadata.find("forge.ui_source");
    const auto* faces =
        source != asset.metadata.end() && source->is_object() && source->contains("font_faces")
            ? &source->at("font_faces")
            : nullptr;
    if (!faces || !faces->is_array() || faces->empty() || faces->size() > 32) {
        ImGui::TextWrapped("Register / Refresh this font in Runtime UI to list its face details.");
        return;
    }
    const auto text = [](const nlohmann::json& row, const char* key) -> const char* {
        const auto at = row.find(key);
        if (at != row.end() && at->is_string()) {
            const auto& value = at->get_ref<const std::string&>();
            if (!value.empty() && value.size() <= 64 && value.find('\0') == value.npos)
                return value.c_str();
        }
        return "Unavailable";
    };
    if (ImGui::BeginTable("font-faces", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Face", 0, 1);
        ImGui::TableSetupColumn("Family", 0, 3);
        ImGui::TableSetupColumn("Style", 0, 2);
        ImGui::TableSetupColumn("Weight", 0, 1.5f);
        ImGui::TableHeadersRow();
        for (const auto& row : *faces) {
            if (!row.is_object() || !row.contains("index") ||
                !row.at("index").is_number_integer() || row.at("index") < 0 ||
                row.at("index") >= 32)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", row.at("index").get<unsigned>());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", text(row, "family"));
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", text(row, "style"));
            ImGui::TableNextColumn();
            const auto weight = row.find("weight");
            if (weight != row.end() && weight->is_number_integer() && *weight >= 1 &&
                *weight <= 1000)
                ImGui::Text("%d", weight->get<int>());
            else
                ImGui::TextUnformatted("Unavailable");
        }
        ImGui::EndTable();
    }
    help("Optional names can be unavailable. Register / Refresh after a source change; restart "
         "Play after replacing a loaded font. These labels do not change font selection.");
}
} // namespace forge::ui
