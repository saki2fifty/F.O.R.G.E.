#pragma once
#include "../standalone_gpu_profile.hpp"
#include "help.hpp"
#include "ui_probe.hpp"

namespace forge::ui {
// Rendering and event routing only; ExportTask retains process/report authority.
template <class Start>
void draw_game_gpu_profile_actions(bool supported, bool locked, Start start,
                                   bool live_supported = false) {
    ImGui::BeginDisabled(!supported || locked);
    if (button("Profile game",
               "Run the exported game with native GPU duration queries. Close its window "
               "normally to accept the report."))
        start(false, false);
    FORGE_UI_PROBE("export:gpu-profile-game");
    if (button("Verify GPU timings",
               "Run finite graphical verification with native asynchronous GPU duration "
               "queries. WARP timings describe software rendering."))
        start(true, false);
    FORGE_UI_PROBE("export:gpu-profile");
    ImGui::BeginDisabled(!live_supported);
    if (button("Watch GPU timings",
               "Run the game with provisional GPU observations updated at most once per second. "
               "The final report still requires a successful normal exit."))
        start(false, true);
    FORGE_UI_PROBE("export:gpu-profile-live");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!supported)
        ImGui::TextWrapped("GPU reports require the runtime kit packaged with this editor.");
    else if (locked)
        ImGui::TextWrapped("Finish the current editor task before starting the exported game.");
}
inline void draw_game_gpu_profile_report(const StandaloneGpuProfileReceiver& receiver) {
    if (!receiver.error().empty()) {
        ImGui::TextWrapped("GPU report: %s", receiver.error().c_str());
        return;
    }
    const bool provisional = receiver.report().is_null() && !receiver.live_report().is_null();
    if (receiver.report().is_null() && !provisional) {
        ImGui::TextUnformatted(receiver.live_enabled() && !receiver.finished()
                                   ? "Waiting for the first live GPU observation."
                                   : "GPU report pending successful game exit.");
        return;
    }
    const auto& report = provisional ? receiver.live_report() : receiver.report();
    if (provisional) {
        ImGui::TextColored({1.f, .78f, .32f, 1.f}, "LIVE GPU TIMINGS — PROVISIONAL");
        ImGui::TextWrapped("The game is still running. These readings are not a successful-run "
                           "report. Close the game normally to accept its final report.");
    }
    const auto& gpu = report.at("gpu");
    const auto& samples = gpu.at("samples");
    const auto& device = gpu.at("device");
    const auto& adapter = device.at("adapter").get_ref<const std::string&>();
    ImGui::TextWrapped("%s | %s%s", device.at("backend").get_ref<const std::string&>().c_str(),
                       adapter.empty() ? "Unidentified adapter" : adapter.c_str(),
                       device.at("software").get<bool>() ? " | Software" : "");
    ImGui::Text("GPU durations: %u retained | %u pending | %llu skipped", unsigned(samples.size()),
                gpu.at("pending").get<unsigned>(),
                static_cast<unsigned long long>(gpu.at("skipped").get<std::uint64_t>()));
    if (!gpu.at("available").get<bool>())
        ImGui::TextWrapped("%s", gpu.at("reason").get<std::string>().c_str());
    ImGui::TextWrapped("Native GPU queue durations; frames are presentation submissions. "
                       "Software verification is not hardware timing.");
    ImGui::TextWrapped("The shared profiling history retains at most %u records. Scroll to "
                       "inspect individual completed samples.",
                       report.at("history").at("capacity").get<unsigned>());
    if (button(provisional ? "Copy live GPU observation" : "Copy GPU report",
               "Copy the observation, adapter, run details and provisional status as JSON.")) {
        auto copied = report;
        if (provisional)
            copied = {
                {"provisional", true}, {"snapshot", receiver.live_snapshot()}, {"report", report}};
        ImGui::SetClipboardText(copied.dump(2).c_str());
    }
    FORGE_UI_PROBE("export:gpu-profile-copy");
    if (ImGui::BeginTable("Exported game GPU durations", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          {0, 280 * interface_scale})) {
        ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_None, 1.5f);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_None, 3.f);
        ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_None, 1.f);
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_None, 1.f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(int(samples.size()));
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const auto& sample = samples[std::size_t(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sample.at("category").get_ref<const std::string&>().c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sample.at("name").get_ref<const std::string&>().c_str());
                help(sample.at("name").get_ref<const std::string&>().c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", sample.at("seconds").get<double>() * 1000);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(
                                        sample.at("frame").get<std::uint64_t>()));
            }
        ImGui::EndTable();
    }
}
} // namespace forge::ui
