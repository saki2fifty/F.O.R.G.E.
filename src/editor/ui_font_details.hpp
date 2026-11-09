#pragma once
#include "../ui_font_profile.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cmath>
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
    for (const auto& row : *faces) {
        if (!row.is_object() || !row.contains("index") || !row.at("index").is_number_integer() ||
            row.at("index") < 0 || row.at("index") >= 32)
            continue;
        if (const auto color = row.find("color");
            color != row.end() && color->is_object() &&
            color->value("format", nlohmann::json{}) == "COLRv0") {
            const auto palettes = color->find("palettes"), entries = color->find("palette_entries");
            if (palettes != color->end() && palettes->is_number_unsigned() &&
                entries != color->end() && entries->is_number_unsigned() && *palettes > 0 &&
                *palettes <= 65535 && *entries > 0 && *entries <= 65535)
                ImGui::TextWrapped(
                    "Face %u: COLRv0 color layers; %u palettes, %u entries. "
                    "Shape UI text uses palette 0 and the text color for foreground layers.",
                    row.at("index").get<unsigned>(), palettes->get<unsigned>(),
                    entries->get<unsigned>());
        }
        const auto axes = row.find("axes");
        const auto omitted = row.find("axes_omitted");
        if (omitted != row.end() && omitted->is_boolean() && omitted->get<bool>())
            ImGui::TextWrapped("Face %u: axis details omitted by the metadata limit.",
                               row.at("index").get<unsigned>());
        if (axes == row.end() || !axes->is_array() || axes->empty() ||
            axes->size() > ui_font_detail::variation_axis_limit)
            continue;
        ImGui::PushID(row.at("index").get<int>());
        ImGui::Text("Face %u variation axes", row.at("index").get<unsigned>());
        if (ImGui::BeginTable("font-axes", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingStretchProp)) {
            for (const char* label : {"Tag", "Name", "Min", "Default", "Max"})
                ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            for (const auto& axis : *axes) {
                if (!axis.is_object() || !axis.contains("tag") || !axis.at("tag").is_string())
                    continue;
                const auto& tag = axis.at("tag").get_ref<const std::string&>();
                if (tag.size() != 4 ||
                    !std::ranges::all_of(tag, [](unsigned char c) { return c >= 32 && c <= 126; }))
                    continue;
                bool valid = true;
                for (const char* key : {"minimum", "default", "maximum"}) {
                    const auto value = axis.find(key);
                    valid &= value != axis.end() && value->is_number() &&
                             std::isfinite(value->get<double>()) &&
                             std::abs(value->get<double>()) <= 32768;
                }
                if (!valid || axis.at("minimum") > axis.at("default") ||
                    axis.at("default") > axis.at("maximum"))
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(tag.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", text(axis, "name"));
                const auto hidden = axis.find("hidden");
                if (hidden != axis.end() && hidden->is_boolean() && hidden->get<bool>())
                    ImGui::TextDisabled("Hidden");
                for (const char* key : {"minimum", "default", "maximum"}) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3g", axis.at(key).get<double>());
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    help("Optional names can be unavailable. Register / Refresh after a source change; restart "
         "Play after replacing a loaded font. Shape UI text uses an admitted wght axis with "
         "ordinary font-weight; other axes stay at their defaults. Color layer and axis details "
         "are read-only; recorded metadata does not guarantee native font admission.");
}
} // namespace forge::ui
