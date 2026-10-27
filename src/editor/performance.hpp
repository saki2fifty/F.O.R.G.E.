#pragma once
#include "widgets.hpp"
#include <array>
#include <chrono>
#include <forge/services.hpp>
#include <optional>
namespace forge::ui {
// CPU wall-clock sections, deliberately not presented as GPU execution timings.
struct Performance {
    using Clock = std::chrono::steady_clock;
    bool visible = false, continuous = false;
    std::array<double, 4> average{}, sum{};
    unsigned samples = 0;
    double elapsed = 0, scene_ms = 0;
    Clock::time_point start;
    std::optional<std::pair<Clock::time_point, Clock::time_point>> scene_interval;
    static double milliseconds(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }
    void begin() {
        start = Clock::now();
        scene_ms = 0;
        scene_interval.reset();
    }
    void scene_submission(Clock::time_point first, Clock::time_point last) {
        scene_ms = milliseconds(first, last);
        scene_interval = std::pair{first, last};
    }
    void finish(Clock::time_point submit, Clock::time_point present, ServiceAccess profiling = {}) {
        const auto end = Clock::now();
        const std::array<double, 4> current{std::max(0.0, milliseconds(start, submit) - scene_ms),
                                            scene_ms, milliseconds(submit, present),
                                            milliseconds(present, end)};
        if (profiling.available(Capability::Profiling) && profiling.profiling_enabled()) {
            const char* scopes[] = {"UpdateAndEditorUI", "SceneSubmission", "UISubmission",
                                    "PresentCall"};
            try {
                // Exclusive Update/UI is fragmented around Scene work, so its
                // duration has no single start. Preserve it as an untimed sample.
                profiling.record_profile("editor", scopes[0], current[0] / 1000);
                const auto interval = [&](unsigned index, Clock::time_point first) {
                    profiling.record_profile_span(
                        "editor", scopes[index],
                        std::chrono::duration<double>(first.time_since_epoch()).count(),
                        current[index] / 1000);
                };
                if (scene_interval)
                    interval(1, scene_interval->first);
                else
                    profiling.record_profile("editor", scopes[1], current[1] / 1000);
                interval(2, submit);
                interval(3, present);
            } catch (...) {
                // Timing collection must not change editor control flow.
            }
        }
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
