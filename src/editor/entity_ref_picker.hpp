#pragma once
#include "editor_state.hpp"
#include "search.hpp"
#include <array>
#include <forge/scene.hpp>
#include <map>

namespace forge {
struct EntityPickerEntry {
    EntityId id;
    std::string label;
};
// A disposable hierarchy projection, never a second identity registry. Build
// parent paths iteratively so a deep valid hierarchy cannot exhaust the stack.
inline std::vector<EntityPickerEntry> entity_picker_entries(const Json& document,
                                                            std::string_view search) {
    struct Node {
        EntityId id;
        std::string name, path;
        std::vector<std::size_t> children;
        bool matches = false;
    };
    const auto& entities = document.at("entities");
    const auto query = search_key(std::string(search));
    std::vector<Node> nodes;
    std::map<std::string, std::size_t> indices;
    for (const auto& entity : entities) {
        const auto id = entity.at("id").get<EntityId>();
        indices.emplace(id.str(), nodes.size());
        nodes.push_back({id, entity.at("name"), {}, {}, false});
    }
    std::vector<std::size_t> queue;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto parent = entities[i].value("parent", std::string{});
        const auto found = indices.find(parent);
        if (found == indices.end()) {
            nodes[i].path = nodes[i].name;
            queue.push_back(i);
        } else
            nodes[found->second].children.push_back(i);
    }
    std::vector<EntityPickerEntry> rows;
    for (std::size_t i = 0; i < queue.size(); ++i) {
        auto& node = nodes[queue[i]];
        node.matches = node.matches || search_key(node.path).find(query) != std::string::npos ||
                       node.id.str().find(query) != std::string::npos;
        if (node.matches)
            rows.push_back({node.id, node.path});
        for (auto child : node.children) {
            auto& target = nodes[child];
            target.path = node.path + " / " + target.name;
            // Keep display memory bounded. Elide only complete ancestors; their
            // search match still propagates through descendants.
            while (target.path.size() > 1024) {
                const auto slash = target.path.find(" / ", target.path.starts_with("… / ") ? 6 : 0);
                if (slash == std::string::npos)
                    break;
                target.path = "… / " + target.path.substr(slash + 3);
            }
            target.matches = node.matches;
            queue.push_back(child);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.label == b.label ? a.id < b.id : a.label < b.label;
    });
    return rows;
}
inline std::string entity_ref_label(const Scene& scene, const Json& value) {
    if (value.is_null())
        return "None";
    try {
        const auto reference = value.get<EntityRef>();
        if (reference.scene != scene.asset_id())
            return "Another scene / " + reference.entity.str().substr(0, 8);
        const auto target = scene.entity(reference.entity.str());
        if (target && target.has<AuthoredName>())
            return target.get<AuthoredName>().value;
        return "Missing entity / " + reference.entity.str().substr(0, 8);
    } catch (const std::exception&) {
        return "Invalid entity reference";
    }
}
inline bool entity_ref_picker(const Scene& scene, Json& value, const char* title) {
    ui::IdScope scope(title);
    bool changed = false;
    const auto label = entity_ref_label(scene, value);
    if (ImGui::BeginCombo(title, label.c_str())) {
        static std::map<ImGuiID, std::array<char, 192>> searches;
        if (searches.size() > 1024)
            searches.clear();
        auto& search = searches[ImGui::GetID("search")];
        ImGui::InputTextWithHint("##search", "Search hierarchy or entity ID...", search.data(),
                                 search.size());
        ui::help("Search names, ancestor paths or persistent IDs in the current scene. Choosing "
                 "an entity records its scene asset and entity UUID, never a runtime handle.");
        if (ImGui::Selectable("None / Clear", value.is_null())) {
            value = nullptr;
            changed = true;
        }
        ui::help("Clear this entity reference. The component's validation still applies.");
        const auto rows = entity_picker_entries(scene.document(), search.data());
        const auto height = ImGui::GetTextLineHeight();
        ImGuiListClipper clipper;
        clipper.Begin(int(rows.size()), height + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& row = rows[std::size_t(i)];
                ui::IdScope item(row.id.str().c_str());
                const Json reference = EntityRef{scene.asset_id(), row.id};
                const auto pos = ImGui::GetCursorScreenPos();
                const auto width = ImGui::GetContentRegionAvail().x;
                if (ImGui::Selectable("##entity", value == reference, 0, {width, height})) {
                    value = reference;
                    changed = true;
                }
                ui::help((row.label + "\nEntity: " + row.id.str()).c_str());
                auto* draw = ImGui::GetWindowDrawList();
                draw->PushClipRect(pos, {pos.x + width, pos.y + height}, true);
                draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
                draw->PopClipRect();
            }
        if (rows.empty())
            ImGui::TextUnformatted("No matching entities in this scene.");
        ImGui::EndCombo();
    }
    ui::help("Select an entity in the current scene. Missing and other-scene references are "
             "preserved until you explicitly replace or clear them.");
    if (label.starts_with("Another scene")) {
        ImGui::TextWrapped("This reference targets another scene. Open that scene to inspect its "
                           "target, or choose an entity from this scene here.");
        ui::help("This picker does not resolve another scene or a runtime loaded instance.");
    } else if (label.starts_with("Missing entity") || label == "Invalid entity reference")
        ui::field_error(label.c_str());
    return changed;
}
} // namespace forge
