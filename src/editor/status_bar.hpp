#pragma once
#include "telemetry.hpp"
#include "widgets.hpp"
#include <cstdio>
#include <string>
#include <vector>
namespace forge::ui {
inline void status_bar(const Telemetry& stats, bool playing, std::size_t entities,
                       const std::string& runtime_state = {}, std::size_t problems = 0,
                       bool selected = false, bool building = false) {
    struct Field {
        std::string text;
        const char* help;
    };
    char fps[64], cpu[64], memory[64];
    if (stats.frames.fps > 0)
        std::snprintf(fps, sizeof(fps), "%.0f FPS | %.2f ms", stats.frames.fps,
                      stats.frames.milliseconds);
    else
        std::snprintf(fps, sizeof(fps), "FPS -- | ms --");
    if (stats.cpu)
        std::snprintf(cpu, sizeof(cpu), "Editor CPU %.1f%%", *stats.cpu);
    else
        std::snprintf(cpu, sizeof(cpu), "Editor CPU --");
    if (stats.process.working_set)
        std::snprintf(memory, sizeof(memory), "Editor RAM %.1f MiB",
                      double(*stats.process.working_set) / 1048576);
    else
        std::snprintf(memory, sizeof(memory), "Editor RAM --");
    const std::vector<Field> fields{
        {fps, "Editor frame-loop throughput and average wall time per frame, sampled every half "
              "second. Not GPU execution time or game simulation FPS."},
        {cpu, "Editor process CPU time divided by elapsed time and all logical processors: 100% "
              "means the whole machine's CPU capacity. Excludes play/build workers. Sampled every "
              "half second."},
        {memory, "Editor process resident working set in MiB (1,048,576 bytes). Excludes "
                 "play/build workers and dedicated GPU memory."},
        {"VSync off",
         "Diligent Present(0): no requested vertical synchronization and no application FPS cap. "
         "Driver or desktop compositor settings may still limit presentation."},
        {runtime_state.empty() ? (playing ? "PLAY" : "EDIT") : runtime_state,
         "Current play-process state. Authoring remains separate from the play world."},
        {std::to_string(entities) + " entities",
         "Number of authored entities, including prefab definitions."},
        {std::to_string(selected ? 1 : 0) + " selected",
         "Authored entity selection count. Asset selection has a separate Inspector scope."},
        {std::to_string(problems) + " problems",
         "Open Window > Problems for actionable diagnostics."},
        {building ? "Building..." : "Build idle",
         "Gameplay build state; details are in Gameplay Code."}};
    const auto& style = ImGui::GetStyle();
    auto* viewport = ImGui::GetMainViewport();
    const float gap = style.ItemSpacing.x * 2;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2{8 * interface_scale, 4 * interface_scale});
    const float height = std::ceil(ImGui::GetTextLineHeight() + 8 * interface_scale);
    const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::BeginViewportSideBar("##FORGE-status", viewport, ImGuiDir_Down, height, flags)) {
        const auto summary = fields[4].text + " | " + std::to_string(problems) + " problems" +
                             (building ? " | Building" : "");
        ImGui::TextUnformatted(summary.c_str());
        help("Runtime state and actionable Problems count. Open Window > Problems for details.");
        const float reserve = 80 * interface_scale;
        float used = ImGui::CalcTextSize(summary.c_str()).x;
        const float available = ImGui::GetContentRegionAvail().x;
        for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u}) {
            const auto& f = fields[i];
            if (used + ImGui::CalcTextSize(f.text.c_str()).x + gap + reserve > available)
                continue;
            used += ImGui::CalcTextSize(f.text.c_str()).x + gap;
            ImGui::SameLine(0, gap);
            ImGui::TextUnformatted(f.text.c_str());
            help(f.help);
        }
        ImGui::SameLine(0, gap);
        if (ImGui::SmallButton("Details"))
            ImGui::OpenPopup("Status details");
        help(
            "All performance, scene and build information, including fields hidden at this width.");
        if (ImGui::BeginPopup("Status details")) {
            for (const auto& f : fields) {
                ImGui::TextUnformatted(f.text.c_str());
                help(f.help);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace forge::ui
