#pragma once
#include "../native_system_dependencies.hpp"
#include "widgets.hpp"
#include <map>
#include <string_view>
namespace forge::ui {
inline bool native_plan_row_matches(const Json& row, const char* filter) {
    if (!*filter || row.at("kind") == "merge")
        return true;
    if (row.at("name").get_ref<const std::string&>().find(filter) != std::string::npos ||
        row.at("phase").get_ref<const std::string&>().find(filter) != std::string::npos)
        return true;
    if (row.contains("access"))
        for (const auto& term : row.at("access").at("terms"))
            if (term.at("name").get_ref<const std::string&>().find(filter) != std::string::npos)
                return true;
    return false;
}
// Presentation only; the enclosing Profiler owns view/filter state and validates first.
inline void draw_native_system_dependencies(const Json& entries, const Json* snapshot,
                                            const char* filter, bool& show_nodes) {
    if (!snapshot) {
        ImGui::TextWrapped("Ordering targets are unavailable in this snapshot. Collect and "
                           "refresh a fixed tick with a matching runtime.");
        return;
    }
    const auto& data = *snapshot;
    std::map<ecs_entity_t, const Json*> nodes, roots;
    for (const auto& item : data.at("nodes"))
        nodes.emplace(item.at("id").get<ecs_entity_t>(), &item);
    for (const auto& row : entries)
        if (row.at("kind") == "system")
            roots.emplace(row.at("id").get<ecs_entity_t>(), &row);
    const auto matches = [&](const Json& node) {
        if (!*filter ||
            node.at("name").get_ref<const std::string&>().find(filter) != std::string::npos)
            return true;
        const auto found = roots.find(node.at("id").get<ecs_entity_t>());
        return found != roots.end() && native_plan_row_matches(*found->second, filter);
    };
    ImGui::TextWrapped("%zu nodes | %zu native targets", data.at("nodes").size(),
                       data.at("links").size());
    if (data.at("truncated").get<bool>())
        ImGui::TextWrapped("Partial targets: a node, link or per-source limit was reached.");
    ImGui::TextWrapped("Native lookups; Plan shows execution order.");
    help("Lookup may resolve inherited targets. Phase tag marks a native phase entity. "
         "Owned pair means the source contains that relation pair, not a measured race or "
         "a computed ordering verdict. Only copied plan roots and returned targets are inspected.");
    ImGui::Checkbox("Show nodes", &show_nodes);
    FORGE_UI_PROBE("profiler:game:dependency-nodes");
    if (!ImGui::BeginTable("Native ordering targets", 5,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                           {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)}))
        return;
    if (show_nodes) {
        for (const char* column : {"Name", "Native ID", "Copied plan", "Phase tag", "More targets"})
            ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                    (std::string_view(column) == "Name" ? 260.f : 145.f) *
                                        interface_scale);
    } else {
        for (const char* column :
             {"Source", "Depends on target", "Phase tag", "Owned pair", "Slot"})
            ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                    (std::string_view(column) == "Source" ||
                                             std::string_view(column) == "Depends on target"
                                         ? 260.f
                                         : 110.f) *
                                        interface_scale);
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    if (show_nodes) {
        for (const auto& node : data.at("nodes")) {
            if (!matches(node))
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(node.at("name").get_ref<const std::string&>().c_str());
            FORGE_UI_PROBE(std::string("profiler:game:dependency-node:") +
                           node.at("name").get<std::string>());
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(node.at("id").get<ecs_entity_t>()));
            for (const char* key : {"root", "phase_tag", "truncated"}) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(node.at(key).get<bool>() ? "Yes" : "No");
            }
        }
    } else {
        for (const auto& link : data.at("links")) {
            const auto& source = *nodes.at(link.at("source").get<ecs_entity_t>());
            const auto& target = *nodes.at(link.at("target").get<ecs_entity_t>());
            if (!matches(source) && !matches(target))
                continue;
            const auto name = source.at("name").get<std::string>();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            FORGE_UI_PROBE(std::string("profiler:game:dependency:") + name + ":" +
                           std::to_string(link.at("slot").get<unsigned>()));
            help(source.at("truncated").get<bool>()
                     ? "More native targets were omitted for this source."
                     : "Source of the native DependsOn lookup; row position is not execution "
                       "order.");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(target.at("name").get_ref<const std::string&>().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(target.at("phase_tag").get<bool>() ? "Yes" : "No");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(link.at("pair_owned").get<bool>() ? "Yes" : "No");
            ImGui::TableNextColumn();
            ImGui::Text("%u", link.at("slot").get<unsigned>());
        }
    }
    ImGui::EndTable();
}
} // namespace forge::ui
