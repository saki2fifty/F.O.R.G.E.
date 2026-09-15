#pragma once
#include "telemetry.hpp"
#include "widgets.hpp"
#include <cstdio>
#include <string>
#include <vector>
namespace forge::ui {
inline void status_bar(const Telemetry& stats, bool playing, std::size_t entities) {
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
        {playing ? "PLAY" : "EDIT",
         "Current play-process state. Authoring remains separate from the play world."},
        {std::to_string(entities) + " entities",
         "Number of authored entities, including prefab definitions."}};
    const auto& style = ImGui::GetStyle();
    auto* viewport = ImGui::GetMainViewport();
    const float available = std::max(1.0f, viewport->Size.x - 2 * style.WindowPadding.x);
    const float gap = style.ItemSpacing.x * 2;
    float used = 0;
    int rows = 1;
    for (const auto& field : fields) {
        const float width = ImGui::CalcTextSize(field.text.c_str()).x;
        if (used > 0 && used + gap + width > available) {
            ++rows;
            used = 0;
        }
        used += (used > 0 ? gap : 0) + width;
    }
    const float height = 2 * style.WindowPadding.y + rows * ImGui::GetTextLineHeight() +
                         (rows - 1) * style.ItemSpacing.y;
    const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_NoNav;
    if (ImGui::BeginViewportSideBar("##FORGE-status", viewport, ImGuiDir_Down, height, flags)) {
        used = 0;
        for (const auto& field : fields) {
            const float width = ImGui::CalcTextSize(field.text.c_str()).x;
            if (used > 0 && used + gap + width <= available) {
                ImGui::SameLine(0, gap);
                used += gap;
            } else {
                used = 0;
            }
            ImGui::TextUnformatted(field.text.c_str());
            help(field.help);
            used += width;
        }
    }
    ImGui::End();
}
} // namespace forge::ui
