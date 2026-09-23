#pragma once
#include "../reflected_value.hpp"
#include "asset_labels.hpp"
#include "editor_state.hpp"
#include "entity_ref_picker.hpp"
#include "icons.hpp"
#include "search.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/project_paths.hpp>
#include <map>
#include <misc/cpp/imgui_stdlib.h>
namespace forge {
inline std::string property_label(const Json& field) {
    if (field.contains("display_name"))
        return field.at("display_name");
    std::string label = field.at("id");
    bool start = true;
    for (auto& c : label) {
        if (c == '_') {
            c = ' ';
            start = true;
        } else if (start) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            start = false;
        }
    }
    return label;
}
inline std::string asset_display(const AssetRecord& record) {
    if (const auto* builtin = engine_asset(record.id))
        return std::string("Engine / ") + builtin->name;
    auto name = path_utf8(record.source);
    const auto member = content_member_name(record);
    if (!member.empty())
        name += " / " + member;
    return name;
}
struct AssetPickerEntry {
    AssetId id;
    std::string label;
    bool engine = false, removed = false;
};
#ifdef FORGE_UI_FIXTURE
// Actual Inspector control capture hook; absent from the shipped editor.
inline bool fixture_open_mesh_picker = false;
#endif
// Disposable projection of the admitted catalog. No source reads or private
// asset registry; only the popup's visible rows are submitted to ImGui.
inline std::vector<AssetPickerEntry> asset_picker_entries(const AssetCatalog& catalog,
                                                          std::string_view type,
                                                          std::string_view search,
                                                          bool include_engine) {
    std::vector<AssetPickerEntry> rows;
    const auto query = search_key(std::string(search));
    if (include_engine)
        for (const auto& asset : engine_assets()) {
            auto label = std::string("Engine / ") + asset.name;
            if (type == asset.type && search_key(label).find(query) != std::string::npos)
                rows.push_back({asset.id, std::move(label), true, false});
        }
    for (const auto& [id, record] : catalog.records()) {
        if (record.type != type || engine_asset(id))
            continue;
        auto label = asset_display(record);
        const bool removed = record.subasset && record.subasset->removed;
        if (removed)
            label += " (removed member)";
        if (search_key(label).find(query) != std::string::npos)
            rows.push_back({id, std::move(label), false, removed});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.engine != b.engine)
            return a.engine;
        return a.label == b.label ? a.id < b.id : a.label < b.label;
    });
    return rows;
}
inline bool asset_ref_picker(const AssetCatalog& catalog, Json& value, const std::string& type,
                             const char* title, bool include_engine = true) {
    ui::IdScope scope(title);
    try {
        std::string label = "None";
        if (!value.is_null()) {
            auto resolved = catalog.resolve(value.get<AssetId>(), type);
            label = resolved.record ? asset_display(*resolved.record) : "Unresolved asset";
            if (resolved.state != AssetState::Available)
                label += " (missing / incompatible)";
        }
        bool changed = false;
#ifdef FORGE_UI_FIXTURE
        const bool fixture_open = fixture_open_mesh_picker && type == "mesh" &&
                                  std::string_view(ImGui::GetCurrentWindow()->Name) == "Inspector";
        if (fixture_open) {
            // Keep the requested popup visible until its capture is complete.
            // A scale/resize may clip the combo again after its first open frame.
            // Scroll the combo itself into view, not the previous label/item.
            ImGui::SetScrollFromPosY(ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y +
                                         ImGui::GetFrameHeight(),
                                     .5f);
            ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID(title)),
                               ImGuiPopupFlags_None);
        }
#endif
        if (ImGui::BeginCombo(title, label.c_str(), ImGuiComboFlags_HeightLarge)) {
            // Popup storage belongs to this field's ImGui scope, not a second asset database.
            static std::map<ImGuiID, std::array<char, 192>> searches;
            if (searches.size() > 1024)
                searches.clear();
            auto& search = searches[ImGui::GetID("search")];
            ImGui::InputTextWithHint("##search", "Search assets...", search.data(), search.size());
            ui::help(
                "Search registered assets compatible with this field by project-relative path.");
            if (ImGui::Selectable("None / Clear", value.is_null())) {
                value = nullptr;
                changed = true;
            }
            ui::help(
                "Clear this reference. Required resources will report a missing-asset diagnostic.");
            const auto rows = asset_picker_entries(catalog, type, search.data(), include_engine);
            const auto selected = value.is_null() ? AssetId{} : value.get<AssetId>();
            const float height = ImGui::GetTextLineHeight();
            // Keep search/Clear outside the scrolling results. Default focus on
            // a selected row must never scroll the search field out of view.
            const float list_height =
                std::min(8 * ImGui::GetTextLineHeightWithSpacing(),
                         std::max(height, ImGui::GetMainViewport()->WorkSize.y * .5f));
            if (ImGui::BeginChild("##asset-results", {0, list_height}, ImGuiChildFlags_None)) {
                ImGuiListClipper clipper;
                clipper.Begin(int(rows.size()), height + ImGui::GetStyle().ItemSpacing.y);
                for (std::size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].id == selected)
                        clipper.IncludeItemByIndex(int(i));
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto& row = rows[std::size_t(i)];
                        ui::IdScope item(row.id.str().c_str());
                        const auto pos = ImGui::GetCursorScreenPos();
                        const auto width = ImGui::GetContentRegionAvail().x;
                        ImGui::BeginDisabled(row.removed);
                        if (ImGui::Selectable("##asset", row.id == selected, 0, {width, height})) {
                            value = row.id;
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                        FORGE_UI_PROBE("picker-option:" + row.id.str());
                        if (row.id == selected)
                            ImGui::SetItemDefaultFocus();
                        ui::help(
                            (row.label + "\n" +
                             (row.removed ? "This member was removed from its source. Reimport a "
                                            "matching member or choose another asset."
                              : row.engine
                                  ? "Built-in engine asset; its shared source is read-only."
                                  : "Assign this registered asset. Its persistent identity "
                                    "survives supported relocation."))
                                .c_str());
                        auto* draw = ImGui::GetWindowDrawList();
                        draw->PushClipRect(pos, {pos.x + width, pos.y + height}, true);
                        ui::asset_icon(draw, pos, height, type);
                        draw->AddText(
                            {pos.x + height + ImGui::GetStyle().ItemInnerSpacing.x, pos.y},
                            ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
                        draw->PopClipRect();
                        ImGui::EndDisabled();
                    }
                if (rows.empty())
                    ImGui::TextWrapped("No matching assets. Use Content > Create / Register for "
                                       "supported asset workflows.");
            }
            ImGui::EndChild();
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("asset-picker:" + type + ":" + title);
        ui::help("Choose a compatible asset, search its path, or drop it from Content. Reveal "
                 "opens its Content selection.");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(
                    "FORGE_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (payload->DataSize == 37) {
                    const auto id =
                        AssetId::parse(std::string(static_cast<const char*>(payload->Data), 36));
                    const auto it = catalog.records().find(id);
                    const auto* builtin = engine_asset(id);
                    const bool compatible =
                        builtin ? include_engine && type == builtin->type
                                : it != catalog.records().end() && it->second.type == type &&
                                      (!it->second.subasset || !it->second.subasset->removed);
                    if (!compatible)
                        ImGui::SetTooltip("This field requires a current %s asset", type.c_str());
                    if (compatible && payload->IsDelivery()) {
                        value = id;
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (!value.is_null() && ui::editor_context && !engine_asset(value.get<AssetId>())) {
            if (ui::button("Reveal in Content",
                           "Inspect this asset in Content. Scene reference stays unchanged.")) {
                ui::editor_context->selection.select_asset(value.get<AssetId>());
                ui::editor_context->reveal_content = true;
            }
        }
        return changed;
    } catch (const std::exception& e) {
        ui::field_error(e.what());
        return false;
    }
}
inline bool asset_ref_picker(const std::filesystem::path& project, Json& value,
                             const std::string& type, const char* title) {
    try {
        return asset_ref_picker(AssetCatalog::open_project(project), value, type, title);
    } catch (const std::exception& e) {
        ui::field_error(e.what());
        return false;
    }
}
template <class T>
inline bool numeric_property(const char* label, ImGuiDataType type, const char* format, Json& value,
                             bool commit_on_enter) {
    T next = value.get<T>();
    if (!ImGui::InputScalar(label, type, &next, nullptr, nullptr, format,
                            commit_on_enter ? ImGuiInputTextFlags_EnterReturnsTrue : 0))
        return false;
    value = next;
    return true;
}
inline bool property_field(const std::filesystem::path& root, const Json& field, Json& value,
                           bool commit_on_enter = true);
// A new collection element is a detached UI draft, not an authored default.
// It must pass native-derived validation before the collection can be committed.
inline Json property_draft_seed(const Json& field) {
    if (field.contains("default"))
        return field.at("default");
    const auto type = field.at("type").get<std::string>();
    if (type == "struct") {
        auto result = Json::object();
        for (const auto& member : field.at("fields"))
            result[member.at("id").get<std::string>()] = property_draft_seed(member);
        return result;
    }
    if (type == "array" || type == "vector") {
        auto result = Json::array();
        if (type == "array")
            for (unsigned i = 0; i < field.at("count").get<unsigned>(); ++i)
                result.push_back(property_draft_seed(field.at("element")));
        return result;
    }
    if (type == "string")
        return "";
    if (type == "bool")
        return false;
    if (type == "entity_ref" || type == "asset_ref")
        return nullptr;
    if (type == "enum")
        return field.at("choices").front().at("value");
    if (type == "float32" || type == "float64")
        return field.value("minimum", Json(0.0));
    return field.value("minimum",
                       type.starts_with("uint") || type == "bitmask" ? Json(0u) : Json(0));
}
inline bool property_collection(const std::filesystem::path& root, const Json& field, Json& value,
                                bool commit_on_enter) {
    const bool dynamic = field.at("type") == "vector";
    bool changed = false;
    constexpr int per_page = 16;
    const auto page_id = ImGui::GetID("collection-page");
    auto* storage = ImGui::GetStateStorage();
    const int pages = std::max(1, int((value.size() + per_page - 1) / per_page));
    int page = std::clamp(storage->GetInt(page_id), 0, pages - 1);
    ImGui::TextDisabled("%zu items%s", value.size(), dynamic ? "" : " (fixed)");
    ui::help("A collection is one property and one Undo step per committed edit. Unknown fields "
             "remain attached to their existing entries.");
    if (pages > 1) {
        ImGui::BeginDisabled(page == 0);
        if (ui::button("Previous", "Show the preceding sixteen entries."))
            --page;
        ImGui::EndDisabled();
        ui::next_text_button("Next");
        ImGui::BeginDisabled(page + 1 == pages);
        if (ui::button("Next", "Show the following sixteen entries."))
            ++page;
        ImGui::EndDisabled();
        ImGui::Text("Page %d / %d", page + 1, pages);
        ui::help("Pagination keeps large collections usable without drawing every entry.");
    }
    storage->SetInt(page_id, page);
    const auto end = std::min(value.size(), std::size_t((page + 1) * per_page));
    for (std::size_t i = std::size_t(page * per_page); i < end; ++i) {
        ui::IdScope entry(std::to_string(i).c_str());
        auto element = field.at("element");
        element["id"] = "value";
        element["display_name"] = "Item " + std::to_string(i + 1);
        changed |= property_field(root, element, value[i], commit_on_enter);
        if (dynamic) {
            if (ui::button("Remove item",
                           "Remove this entry. The whole collection edit is undoable.")) {
                value.erase(value.begin() + std::ptrdiff_t(i));
                changed = true;
                break;
            }
            ui::next_text_button("Move up");
            ImGui::BeginDisabled(i == 0);
            if (ui::button("Move up", "Move this complete entry, including its unknown fields, one "
                                      "position earlier.")) {
                std::swap(value[i - 1], value[i]);
                changed = true;
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
    }
    if (dynamic) {
        const auto maximum = field.value("maximum_count", 4096u);
        ImGui::BeginDisabled(value.size() >= maximum);
        // One bounded draft per field; it never enters the scene before Add.
        struct Draft {
            Json value;
            std::string error;
            int seen = 0;
        };
        static std::map<ImGuiID, Draft> drafts;
        const auto id = ImGui::GetID("new-collection-item");
        const auto frame = ImGui::GetFrameCount();
        if (auto found = drafts.find(id); found != drafts.end())
            found->second.seen = frame;
        if (ui::button("Add item...", "Prepare a new entry. Add validates the candidate "
                                      "collection; Cancel leaves it unchanged.")) {
            // Never invalidate an outer popup's draft during nested drawing.
            std::erase_if(drafts, [&](const auto& row) { return row.second.seen != frame; });
            if (drafts.size() < 64 || drafts.contains(id)) {
                drafts[id] = {property_draft_seed(field.at("element")), {}, frame};
                ImGui::OpenPopup("New collection item");
            }
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("New collection item")) {
            auto found = drafts.find(id);
            if (found != drafts.end()) {
                auto& draft = found->second;
                auto element = field.at("element");
                element["id"] = "new-item";
                element["display_name"] = "New item";
                property_field(root, element, draft.value, false);
                if (ui::button("Add",
                               "Validate this entry and add it in one property operation.")) {
                    try {
                        auto candidate = value;
                        candidate.push_back(draft.value);
                        detail::validate_reflected_json(field, candidate);
                        value = std::move(candidate);
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    } catch (const std::exception& e) {
                        draft.error = e.what();
                    }
                }
                ui::next_text_button("Cancel");
                if (ui::button("Cancel", "Discard this new entry without changing the collection."))
                    ImGui::CloseCurrentPopup();
                if (!draft.error.empty())
                    ui::field_error(draft.error.c_str());
            }
            ImGui::EndPopup();
        } else
            drafts.erase(id);
    }
    return changed;
}
inline bool property_field_body(const std::filesystem::path& root, const Json& field, Json& value,
                                bool commit_on_enter) {
    auto visible_label = property_label(field);
    const auto unit = field.value("unit", std::string("unitless"));
    if (unit != "unitless" && !unit.empty())
        visible_label += " (" + unit + ")";
    const std::string type = field.at("type");
    const auto label = "##" + property_label(field);
    if (type == "struct" || type == "array" || type == "vector") {
        const bool open = ImGui::TreeNode(visible_label.c_str());
        ui::help(
            field
                .value("description", std::string("Expand this reflected value. Changes use the "
                                                  "owning component's validation and history."))
                .c_str());
        bool changed = false;
        if (open) {
            if (type == "struct") {
                for (const auto& member : field.at("fields")) {
                    const auto key = member.at("id").get<std::string>();
                    ui::IdScope scope(key.c_str());
                    changed |= property_field(root, member, value.at(key), commit_on_enter);
                }
            } else
                changed = property_collection(root, field, value, commit_on_enter);
            ImGui::TreePop();
        }
        return changed;
    }
    ui::property_label_row(visible_label.c_str(),
                           field.value("description", std::string{}).c_str());
    if (type == "asset_ref")
        return asset_ref_picker(root, value, field.at("asset_type"), label.c_str());
    bool changed = false;
    if (type == "bitmask") {
        auto bits = value.get<std::uint32_t>();
        if (ImGui::BeginCombo(label.c_str(), value.dump().c_str())) {
            for (const auto& choice : field.at("choices")) {
                const auto flag = choice.at("value").get<std::uint32_t>();
                const bool selected = flag ? (bits & flag) == flag : bits == 0;
                if (ImGui::Selectable(choice.at("label").get_ref<const std::string&>().c_str(),
                                      selected, ImGuiSelectableFlags_NoAutoClosePopups)) {
                    bits = flag ? (selected ? bits & ~flag : bits | flag) : 0;
                    value = bits;
                    changed = true;
                }
                ui::help("Toggle the declared flags. Choosing zero clears every flag. Unknown bits "
                         "are rejected before commit.");
            }
            ImGui::EndCombo();
        }
    } else if (field.contains("choices")) {
        std::string current = "Unsupported value";
        for (const auto& choice : field.at("choices"))
            if (choice.at("value") == value)
                current = choice.at("label");
        if (ImGui::BeginCombo(label.c_str(), current.c_str())) {
            for (const auto& choice : field.at("choices")) {
                if (ImGui::Selectable(choice.at("label").get_ref<const std::string&>().c_str(),
                                      choice.at("value") == value)) {
                    value = choice.at("value");
                    changed = true;
                }
                ui::help(field.value("description", std::string{}).c_str());
            }
            ImGui::EndCombo();
        }
    } else if (type == "bool") {
        bool next = value;
        changed = ImGui::Checkbox(label.c_str(), &next);
        if (changed)
            value = next;
    } else if (type == "int8")
        changed = numeric_property<std::int8_t>(label.c_str(), ImGuiDataType_S8, "%d", value,
                                                commit_on_enter);
    else if (type == "uint8")
        changed = numeric_property<std::uint8_t>(label.c_str(), ImGuiDataType_U8, "%u", value,
                                                 commit_on_enter);
    else if (type == "int16")
        changed = numeric_property<std::int16_t>(label.c_str(), ImGuiDataType_S16, "%d", value,
                                                 commit_on_enter);
    else if (type == "uint16")
        changed = numeric_property<std::uint16_t>(label.c_str(), ImGuiDataType_U16, "%u", value,
                                                  commit_on_enter);
    else if (type == "uint32")
        changed = numeric_property<std::uint32_t>(label.c_str(), ImGuiDataType_U32, "%u", value,
                                                  commit_on_enter);
    else if (type == "int32")
        changed = numeric_property<std::int32_t>(label.c_str(), ImGuiDataType_S32, "%d", value,
                                                 commit_on_enter);
    else if (type == "uint64")
        changed = numeric_property<std::uint64_t>(label.c_str(), ImGuiDataType_U64, "%llu", value,
                                                  commit_on_enter);
    else if (type == "int64")
        changed = numeric_property<std::int64_t>(label.c_str(), ImGuiDataType_S64, "%lld", value,
                                                 commit_on_enter);
    else if (type == "float32" &&
             field.value("property_id", std::string{}).starts_with("forge.local_scale."))
        changed = numeric_property<double>(label.c_str(), ImGuiDataType_Double, "%.9g", value,
                                           commit_on_enter);
    else if (type == "float32")
        changed = numeric_property<float>(label.c_str(), ImGuiDataType_Float, "%.3f", value,
                                          commit_on_enter);
    else if (type == "float64")
        changed = numeric_property<double>(label.c_str(), ImGuiDataType_Double, "%.3f", value,
                                           commit_on_enter);
    else if (type == "string") {
        auto buffer = value.get<std::string>();
        changed = ImGui::InputText(label.c_str(), &buffer,
                                   commit_on_enter ? ImGuiInputTextFlags_EnterReturnsTrue : 0);
        if (changed)
            value = std::move(buffer);
    } else if (type == "entity_ref" && ui::editor_context && ui::editor_context->scene) {
        changed = entity_ref_picker(*ui::editor_context->scene, value, label.c_str());
    } else
        ImGui::TextWrapped("%s: unsupported reflected type %s (read only)", label.c_str(),
                           type.c_str());
    auto description =
        field.value("description", std::string{}) +
        (commit_on_enter ? " Numeric/text edits commit with Enter."
                         : " Draft value; publish/save validates before changing project data.");
    if (field.contains("minimum"))
        description +=
            " Range: " + field.at("minimum").dump() + " to " + field.at("maximum").dump() + ".";
    ui::help(description.c_str());
    if (value.is_number()) {
        const double n = value.get<double>();
        for (const char* key : {"error_range", "warning_range"}) {
            if (!field.contains(key))
                continue;
            const auto& range = field.at(key);
            const double low = range.at("minimum"), high = range.at("maximum");
            const double stored_low = type == "float32" ? double(float(low)) : low;
            if (!std::isfinite(n) || n < stored_low || n > high) {
                const auto message =
                    std::string(key == std::string("error_range") ? "Outside supported range: "
                                                                  : "Outside recommended range: ") +
                    range.at("minimum").dump() + " to " + range.at("maximum").dump();
                if (std::string(key) == "error_range")
                    ui::field_error(message.c_str());
                else
                    ImGui::TextWrapped("%s", message.c_str());
                ui::help("Ranges come from Flecs member metadata. FORGE validates before commit; "
                         "recommended ranges are advisory.");
                break;
            }
        }
    }
    return changed;
}
inline bool property_field(const std::filesystem::path& root, const Json& field, Json& value,
                           bool commit_on_enter) {
    const bool read_only = field.value("read_only", false);
    ImGui::BeginDisabled(read_only);
    const bool changed = property_field_body(root, field, value, commit_on_enter);
    ImGui::EndDisabled();
    return changed && !read_only;
}
} // namespace forge
