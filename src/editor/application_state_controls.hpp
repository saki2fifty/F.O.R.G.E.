#pragma once
#include "widgets.hpp"
#include <cstdint>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace forge::ui {
// Draw copied observations only. The caller releases gameplay input and submits
// the selected request through PlaySession; no state or queue is owned here.
struct ApplicationStateControls {
    struct Request {
        std::string domain, state;
        std::uint64_t revision;
    };
    static std::optional<Request> draw(const nlohmann::json& status, bool available, bool pending,
                                       const std::string& error) {
        if (status.is_null())
            return std::nullopt;
        const bool expanded = ImGui::CollapsingHeader("Application states");
        FORGE_UI_PROBE("application_states:live_header");
        if (!expanded)
            return std::nullopt;
        ImGui::TextUnformatted("Runtime only. Controls remain available while paused.");
        std::optional<Request> request;
        for (const auto& row : status.at("domains")) {
            const auto& key = row.at("key").get_ref<const std::string&>();
            const auto& label = row.at("label").get_ref<const std::string&>();
            const auto& active = row.at("active").get_ref<const std::string&>();
            const auto& active_label = row.at("active_label").get_ref<const std::string&>();
            const bool computed = row.at("computed").get<bool>();
            ImGui::PushID(key.c_str());
            ImGui::BeginDisabled(!available || pending || computed || active.empty());
            ImGui::SetNextItemWidth(
                std::min(14.f * ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x * .45f));
            const auto preview = active.empty()         ? "Inactive"
                                 : active_label.empty() ? active.c_str()
                                                        : active_label.c_str();
            const bool opened =
                ImGui::BeginCombo(label.empty() ? key.c_str() : label.c_str(), preview);
            FORGE_UI_PROBE("application_states:live_domain:" + key);
            if (opened) {
                for (const auto& choice : row.at("states")) {
                    const auto& value = choice.at("key").get_ref<const std::string&>();
                    const auto& title = choice.at("label").get_ref<const std::string&>();
                    ImGui::PushID(value.c_str());
                    if (ImGui::Selectable(title.empty() ? value.c_str() : title.c_str(),
                                          value == active))
                        request = Request{key, value, row.at("revision").get<std::uint64_t>()};
                    FORGE_UI_PROBE("application_states:live_choice:" + key + ":" + value);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (computed) {
                ImGui::SameLine();
                ImGui::TextDisabled("Computed");
                help("This domain follows its ordered rules and cannot be changed directly.");
            } else if (active.empty()) {
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("Waiting for its parent condition");
                ImGui::PopTextWrapPos();
            }
            ImGui::PopID();
        }
        if (pending)
            ImGui::TextUnformatted("State request pending…");
        if (!error.empty())
            ImGui::TextWrapped("State request: %s", error.c_str());
        return request;
    }
};
} // namespace forge::ui
