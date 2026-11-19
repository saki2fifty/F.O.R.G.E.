#pragma once
#include "widgets.hpp"
#include <algorithm>
#include <forge/navigation_build.hpp>
#include <imgui.h>
#include <optional>

namespace forge::ui {
// Selection is view state only. PlaySession owns transport and NavigationRuntime
// owns work, source validation and query adoption.
class NavigationRebuildControls {
    AssetId selected_;

  public:
    struct Request {
        std::string operation;
        AssetId asset;
        std::uint64_t token = 0;
    };
    std::optional<Request> draw(const AssetCatalog* catalog, AssetId scene,
                                const nlohmann::json& status, bool available, bool pending,
                                const std::string& error) {
        if (!ImGui::CollapsingHeader("Runtime navigation"))
            return {};
        FORGE_UI_PROBE("navigation_rebuild:header");
        ImGui::TextWrapped("Rebuild navigation from current supported static Scene geometry.");
        std::map<std::string, AssetId> profiles;
        try {
            if (catalog && scene)
                profiles = navigation_profiles(*catalog, scene);
        } catch (const std::exception& e) {
            ImGui::TextWrapped("%s", e.what());
            return {};
        }
        auto selected = std::ranges::find_if(
            profiles, [&](const auto& row) { return row.second == selected_; });
        if (selected == profiles.end() && !profiles.empty()) {
            selected = profiles.begin();
            selected_ = selected->second;
        }
        if (profiles.empty()) {
            selected_ = {};
            ImGui::TextWrapped("Build a navigation asset for this Scene before starting Play.");
        }
        const auto phase = status.is_object() ? status.value("phase", "") : std::string{};
        const bool active = phase == "Queued" || phase == "Preparing" || phase == "Building";
        const bool retiring = status.is_object() && status.value("retiring", false);
        ImGui::BeginDisabled(!available || pending || active || retiring);
        if (ImGui::BeginCombo("Profile",
                              selected == profiles.end() ? "None" : selected->first.c_str())) {
            for (const auto& [name, asset] : profiles)
                if (ImGui::Selectable(name.c_str(), asset == selected_))
                    selected_ = asset;
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!selected_);
        const bool start = ui::button("Rebuild navigation",
                                      "Replace this world's private navigation "
                                      "basis. Authored assets and other worlds remain unchanged.");
        FORGE_UI_PROBE("navigation_rebuild:start");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!available || pending || !active);
        const bool cancel =
            ui::button("Cancel rebuild", "Cancel pending work without publishing its result.");
        FORGE_UI_PROBE("navigation_rebuild:cancel");
        ImGui::EndDisabled();
        if (!phase.empty())
            ImGui::Text("Rebuild: %s", phase.c_str());
        if (retiring)
            ImGui::TextUnformatted("Finishing request...");
        if (status.is_object() && !status.value("diagnostic", "").empty())
            ImGui::TextWrapped("%s", status.at("diagnostic").get_ref<const std::string&>().c_str());
        if (!error.empty())
            ImGui::TextWrapped("%s", error.c_str());
        ImGui::TextWrapped("Rebuilt geometry requires a clean Play restart after a crash.");
        if (start)
            return Request{"start", selected_};
        if (cancel)
            return Request{"cancel", {}, status.at("token").get<std::uint64_t>()};
        return {};
    }
};
} // namespace forge::ui
