#pragma once
#include "widgets.hpp"
#include <array>
#include <chrono>
namespace forge::ui {
// CPU wall-clock sections, deliberately not presented as GPU execution timings.
struct Performance {
    using Clock = std::chrono::steady_clock;
    bool visible = false, continuous = false;
    std::array<double, 4> average{}, sum{};
    unsigned samples = 0;
    double elapsed = 0, scene_ms = 0;
    Clock::time_point start;
    static double milliseconds(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }
    void begin() {
        start = Clock::now();
        scene_ms = 0;
    }
    void finish(Clock::time_point submit, Clock::time_point present) {
        const auto end = Clock::now();
        const std::array<double, 4> current{std::max(0.0, milliseconds(start, submit) - scene_ms),
                                            scene_ms, milliseconds(submit, present),
                                            milliseconds(present, end)};
        for (unsigned i = 0; i < 4; ++i)
            sum[i] += current[i];
        ++samples;
        elapsed += milliseconds(start, end);
        if (elapsed >= 500) {
            for (unsigned i = 0; i < 4; ++i)
                average[i] = sum[i] / samples;
            sum = {};
            samples = 0;
            elapsed = 0;
        }
    }
    void menu() {
        ImGui::MenuItem("Performance", nullptr, &visible);
        help("Inspect CPU frame sections and compare static scene reuse with continuous redraw.");
    }
    void draw(std::uint64_t redraws, std::uint64_t retained) {
        if (!visible)
            return;
        ImGui::SetNextWindowSize({440 * interface_scale, 300 * interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Performance", &visible)) {
            heading("CPU frame sections", "Half-second averages of CPU wall time. GPU work is "
                                          "asynchronous; these are not GPU execution times.");
            const char* labels[] = {"Update and editor UI", "Scene render submission",
                                    "UI render submission", "Present call"};
            const char* descriptions[] = {
                "Input, workers, authoring snapshots, panels and overlays; excludes the three "
                "sections below.",
                "CPU work to redraw or reuse the offscreen scene texture. GPU execution is not "
                "timed.",
                "Backbuffer setup and Dear ImGui rendering commands, including driver work.",
                "CPU time inside Present(0). Driver or compositor waits can occur here or during "
                "submission."};
            for (unsigned i = 0; i < 4; ++i) {
                ImGui::Text("%s: %.3f ms", labels[i], average[i]);
                help(descriptions[i]);
            }
            ImGui::Separator();
            ImGui::Text("Scene redraws: %llu | Retained: %llu",
                        static_cast<unsigned long long>(redraws),
                        static_cast<unsigned long long>(retained));
            help("Cumulative scene renders and unchanged EDIT texture reuses since launch. The "
                 "interface still renders every frame; Play always redraws.");
            ImGui::Checkbox("Redraw static scene every frame", &continuous);
            help("Session-only comparison switch. Disable for normal use. Does not change VSync, "
                 "FPS limits, or play simulation.");
            ImGui::TextWrapped("Compare the same window size, scene, selection and camera. Idle "
                               "EDIT throughput is not gameplay performance.");
            help("Selection draws the Inspector and object overlays; viewport resolution also "
                 "affects GPU cost.");
        }
        ImGui::End();
    }
};
} // namespace forge::ui
