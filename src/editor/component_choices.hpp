#pragma once
#include "search.hpp"
#include "widgets.hpp"
#include <forge/scene.hpp>
#include <functional>
#include <map>
namespace forge::ui {
inline void component_choices(const Json& schema, const Json& components, const char* filter,
                              const std::function<void(const Json&)>& add) {
    unsigned count = 0;
    std::map<std::string, std::vector<const Json*>> categories;
    for (const auto& type : schema.at("components")) {
        if (!type.value("optional", false))
            continue;
        const std::string key = type.at("id"), label = type.value("display_name", key),
                          category = type.value("category", std::string("Components"));
        if (search_key(category + " " + label).find(search_key(filter)) != std::string::npos)
            categories[category].push_back(&type);
    }
    for (const auto& [category, types] : categories) {
        ImGui::SeparatorText(category.c_str());
        help("Registered components in this category.");
        for (const auto* entry : types) {
            const auto& type = *entry;
            const std::string key = type.at("id"), label = type.value("display_name", key);
            ++count;
            const bool present = components.contains(key);
            ImGui::BeginDisabled(present);
            if (ImGui::Selectable((label + (present ? " (Added)" : "")).c_str()))
                add(type);
            const auto description =
                type.value("description", std::string{}) +
                (present ? " Already attached, including inherited components."
                         : " Add with registered defaults, then assign any required assets.");
            help(description.c_str());
            ImGui::EndDisabled();
        }
    }
    if (!count)
        ImGui::TextUnformatted("No matching components.");
}
} // namespace forge::ui
