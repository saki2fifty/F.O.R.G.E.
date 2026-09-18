#pragma once
#include "editor_state.hpp"
#include "search.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <forge/project_paths.hpp>
#include <map>
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
    auto name = path_utf8(record.source);
    if (record.metadata.contains("clip_name"))
        name += " / " + record.metadata.at("clip_name").get<std::string>();
    return name;
}
inline bool asset_ref_picker(const std::filesystem::path& project, Json& value,
                             const std::string& type, const char* title) {
    ui::IdScope scope(title);
    try {
        const auto catalog = AssetCatalog::open_project(project);
        std::string label = "None";
        if (!value.is_null()) {
            auto resolved = catalog.resolve(value.get<AssetId>(), type);
            label = resolved.record ? asset_display(*resolved.record) : "Unresolved asset";
            if (resolved.state != AssetState::Available)
                label += " (missing / incompatible)";
        }
        bool changed = false;
        if (ImGui::BeginCombo(title, label.c_str())) {
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
            unsigned count = 0;
            for (const auto& [id, record] : catalog.records()) {
                if (record.type != type ||
                    search_key(asset_display(record)).find(search_key(search.data())) ==
                        std::string::npos)
                    continue;
                ui::IdScope item(id.str().c_str());
                if (ImGui::Selectable(asset_display(record).c_str(), value == Json(id))) {
                    value = id;
                    changed = true;
                }
                ui::help("Assign this registered asset; its persistent identity survives supported "
                         "relocation.");
                ++count;
            }
            if (!count)
                ImGui::TextWrapped("No matching assets. Use Content > Create / Register for "
                                   "supported asset workflows.");
            ImGui::EndCombo();
        }
        ui::help("Choose a compatible asset, search its path, or drop it from Content. Reveal "
                 "opens its Content selection.");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(
                    "FORGE_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (payload->DataSize == 37) {
                    const auto id =
                        AssetId::parse(std::string(static_cast<const char*>(payload->Data), 36));
                    const auto it = catalog.records().find(id);
                    const bool compatible =
                        it != catalog.records().end() && it->second.type == type;
                    if (!compatible)
                        ImGui::SetTooltip("This field requires %s", type.c_str());
                    if (compatible && payload->IsDelivery()) {
                        value = id;
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (!value.is_null() && ui::editor_context) {
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
                           bool commit_on_enter = true) {
    const auto visible_label = property_label(field);
    const std::string type = field.at("type");
    const auto label = type == "bool" ? visible_label : "##" + visible_label;
    if (type != "bool") {
        ImGui::TextUnformatted(visible_label.c_str());
        ui::help(field.value("description", std::string{}).c_str());
        ImGui::SetNextItemWidth(-1);
    }
    if (type == "asset_ref")
        return asset_ref_picker(root, value, field.at("asset_type"), label.c_str());
    bool changed = false;
    if (field.contains("choices")) {
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
                ui::help(field.at("description").get_ref<const std::string&>().c_str());
            }
            ImGui::EndCombo();
        }
    } else if (type == "bool") {
        bool next = value;
        changed = ImGui::Checkbox(label.c_str(), &next);
        if (changed)
            value = next;
    } else if (type == "uint32")
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
    else if (type == "float32")
        changed = numeric_property<float>(label.c_str(), ImGuiDataType_Float, "%.3f", value,
                                          commit_on_enter);
    else if (type == "float64")
        changed = numeric_property<double>(label.c_str(), ImGuiDataType_Double, "%.3f", value,
                                           commit_on_enter);
    else if (type == "string") {
        std::array<char, 4096> buffer{};
        SDL_strlcpy(buffer.data(), value.get<std::string>().c_str(), buffer.size());
        changed = ImGui::InputText(label.c_str(), buffer.data(), buffer.size(),
                                   commit_on_enter ? ImGuiInputTextFlags_EnterReturnsTrue : 0);
        if (changed)
            value = buffer.data();
    } else if (type == "entity_ref" && ui::editor_context && ui::editor_context->scene) {
        auto& scene = *ui::editor_context->scene;
        const auto document = scene.document();
        std::string current = value.is_null() ? "None" : "Unresolved entity";
        if (!value.is_null())
            for (const auto& e : document.at("entities"))
                if (value.at("scene") == Json(scene.asset_id()) && value.at("entity") == e.at("id"))
                    current = e.at("name");
        if (ImGui::BeginCombo(label.c_str(), current.c_str())) {
            if (ImGui::Selectable("None / Clear", value.is_null())) {
                value = nullptr;
                changed = true;
            }
            for (const auto& e : document.at("entities")) {
                const std::string id = e.at("id");
                ui::IdScope scope(id.c_str());
                if (ImGui::Selectable(e.at("name").get_ref<const std::string&>().c_str())) {
                    value = scene.reference(id);
                    changed = true;
                }
                ui::help("Persistent reference to this authored scene asset and entity. This is "
                         "not a runtime instance handle.");
            }
            ImGui::EndCombo();
        }
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
    const auto unit = field.value("unit", std::string("unitless"));
    if (unit != "unitless" && !unit.empty()) {
        ImGui::TextDisabled("Unit: %s", unit.c_str());
        ui::help("Units of the preceding reflected property.");
    }
    return changed;
}
} // namespace forge
