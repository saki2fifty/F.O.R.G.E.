#pragma once
#include "editor_state.hpp"
#include "widgets.hpp"
#include <array>
#include <cstdio>
#include <functional>
#include <string>
namespace forge {
// Stateless presentation of the existing Play controller's accepted clock policy.
inline void simulation_speed_controls(double current, bool ready, bool pending,
                                      const std::function<bool(double)>& submit,
                                      bool menu = false) {
    constexpr std::array rates{0., .25, .5, 1., 2., 4.};
    constexpr std::array labels{"Hold motion (0x)",  "Quarter speed (0.25x)",
                                "Half speed (0.5x)", "Normal speed (1x)",
                                "Double speed (2x)", "Four times speed (4x)"};
    ui::IdScope scope("simulation-speed");
    bool open = false;
    if (menu) {
        open = ImGui::BeginMenu("Simulation speed", ready && !pending);
        FORGE_UI_PROBE("simulation-speed:chooser");
    } else {
        ImGui::BeginDisabled(!ready || pending);
        char text[32];
        if (pending)
            std::snprintf(text, sizeof(text), "Applying...");
        else
            std::snprintf(text, sizeof(text), "%.3gx", current);
        ImGui::SetNextItemWidth(100 * ui::interface_scale);
        open = ImGui::BeginCombo("Speed", text);
        FORGE_UI_PROBE("simulation-speed:chooser");
        ui::help("Slow or speed up gameplay. Zero holds motion; choose 1 to continue. "
                 "Sound keeps its normal speed. Stop Play restores 1.");
    }
    if (!open) {
        if (!menu)
            ImGui::EndDisabled();
        return;
    }
    for (std::size_t i = 0; i < rates.size(); ++i) {
        const bool chosen = menu ? ImGui::MenuItem(labels[i], nullptr, current == rates[i])
                                 : ImGui::Selectable(labels[i], current == rates[i]);
        FORGE_UI_PROBE("simulation-speed:" + std::to_string(i));
        if (chosen)
            (void)submit(rates[i]);
    }
    if (menu)
        ImGui::EndMenu();
    else {
        ImGui::EndCombo();
        ImGui::EndDisabled();
    }
}
} // namespace forge
