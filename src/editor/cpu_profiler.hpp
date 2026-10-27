#pragma once
#include "telemetry.hpp"
#include "widgets.hpp"
#include <forge/services.hpp>
#include <map>
#include <tuple>
namespace forge::ui {
struct CpuProfileRow {
    std::string category, name;
    std::size_t count = 0;
    double total = 0, maximum = 0, latest = 0;
    std::uint64_t tick = 0;
};
inline std::vector<CpuProfileRow> summarize_cpu_profiles(const nlohmann::json& samples,
                                                         bool gpu = false) {
    if (!samples.is_array() || samples.size() > 512)
        throw std::runtime_error("CPU profile snapshot exceeds its record bound");
    std::map<std::pair<std::string, std::string>, CpuProfileRow> groups;
    for (const auto& sample : samples) {
        const char* position = gpu ? "frame" : "tick";
        if (!sample.is_object() || sample.value("clock", "cpu") != (gpu ? "gpu" : "cpu") ||
            !sample.at("category").is_string() || !sample.at("name").is_string() ||
            !sample.at("seconds").is_number() || !sample.at(position).is_number_integer() ||
            sample.at(position) < (gpu ? 1 : 0) || sample.at("count") != 1)
            throw std::runtime_error("Invalid CPU profile snapshot record");
        const auto category = sample.at("category").get<std::string>();
        const auto name = sample.at("name").get<std::string>();
        const auto seconds = sample.at("seconds").get<double>();
        if (category.empty() || category.size() > 128 || name.empty() || name.size() > 128 ||
            !std::isfinite(seconds) || seconds < 0 || seconds > 3600)
            throw std::runtime_error("Invalid CPU profile snapshot value");
        auto& row = groups[{category, name}];
        row.category = category;
        row.name = name;
        ++row.count;
        row.total += seconds;
        row.maximum = std::max(row.maximum, seconds);
        row.latest = seconds;
        row.tick = sample.at(position).get<std::uint64_t>();
    }
    std::vector<CpuProfileRow> result;
    for (auto& [key, row] : groups)
        result.push_back(std::move(row));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.total != b.total)
            return a.total > b.total;
        return std::tie(a.category, a.name) < std::tie(b.category, b.name);
    });
    return result;
}
class CpuProfiler {
  public:
    // Returns whether the Game tab needs optional transport observations this frame.
    bool draw(bool& open, ServiceAccess editor, const Telemetry& telemetry,
              const nlohmann::json& game, bool game_ready, bool pending, const std::string& error,
              const std::function<bool(bool, bool)>& submit,
              const nlohmann::json& gpu = nlohmann::json::object(),
              const std::function<bool(bool, bool)>& submit_gpu = {}) {
        if (!open)
            return false;
        bool watch = false;
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::max(1.f, std::min(820 * interface_scale, available.x - 20)),
                                  std::max(1.f, std::min(500 * interface_scale, available.y - 20))},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Profiler###CpuProfiler", &open)) {
            ImGui::TextWrapped("CPU and GPU use separate clocks; collection adds overhead.");
            if (ImGui::BeginTabBar("CPU owners")) {
                if (ImGui::BeginTabItem("Editor")) {
                    FORGE_UI_PROBE("profiler:editor-tab");
                    ImGui::Text("Editor loop: %.0f FPS | %.2f ms/frame", telemetry.frames.fps,
                                telemetry.frames.milliseconds);
                    help("Editor loop throughput includes CPU submission and waits. It is not "
                         "game simulation FPS or GPU execution time.");
                    if (editor.available(Capability::Profiling))
                        draw_records("editor", editor.profile_status(), true, false, "",
                                     [&](bool enabled, bool clear) {
                                         if (clear)
                                             editor.clear_profiles();
                                         editor.profiling(enabled);
                                         return true;
                                     });
                    else
                        ImGui::TextWrapped("Editor CPU profiling is unavailable.");
                    ImGui::EndTabItem();
                } else
                    FORGE_UI_PROBE("profiler:editor-tab");
                if (ImGui::BeginTabItem("Play runtime")) {
                    FORGE_UI_PROBE("profiler:game-tab");
                    watch = true;
                    if (!game_ready)
                        ImGui::TextWrapped(
                            "Start Play and wait for an active world to inspect "
                            "the separate runtime worker. Game GPU time is not collected.");
                    else if (ImGui::BeginTabBar("Runtime CPU views")) {
                        if (ImGui::BeginTabItem("CPU scopes")) {
                            draw_records("game", game, true, pending, error, submit);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("ECS systems")) {
                            FORGE_UI_PROBE("profiler:game:systems-tab");
                            draw_records("game", game, true, pending, error, submit, false, true);
                            ImGui::EndTabItem();
                        } else
                            FORGE_UI_PROBE("profiler:game:systems-tab");
                        ImGui::EndTabBar();
                    }
                    ImGui::EndTabItem();
                } else
                    FORGE_UI_PROBE("profiler:game-tab");
                if (ImGui::BeginTabItem("Graphics GPU")) {
                    FORGE_UI_PROBE("profiler:gpu-tab");
                    ImGui::TextWrapped(
                        "GPU pass durations from this editor. Results arrive on later frames.");
                    help("Scene, Game, previews and UI share the graphics device. Cached Scene "
                         "images do not redraw. These timings do not measure standalone game FPS "
                         "or total GPU utilization. CPU and GPU share 512 editor records.");
                    if (gpu.contains("samples")) {
                        ImGui::TextWrapped(
                            "Pending: %u / %u | Skipped: %llu", gpu.value("pending", 0u),
                            gpu.value("query_capacity", 0u),
                            static_cast<unsigned long long>(gpu.value("skipped", std::uint64_t{})));
                        ImGui::TextWrapped(
                            "Discarded old results: %llu | Latest delay: %llu frames",
                            static_cast<unsigned long long>(gpu.value("stale", std::uint64_t{})),
                            static_cast<unsigned long long>(
                                gpu.value("latency_frames", std::uint64_t{})));
                        draw_records("gpu", gpu, gpu.value("available", false), false,
                                     gpu.value("reason", ""), submit_gpu, true);
                    } else
                        ImGui::TextWrapped("GPU duration queries are unavailable.");
                    ImGui::EndTabItem();
                } else
                    FORGE_UI_PROBE("profiler:gpu-tab");
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        return watch;
    }

  private:
    char filter_[128]{};
    std::map<std::string, std::string> local_errors_;
    void draw_records(const char* owner, const nlohmann::json& status, bool ready, bool pending,
                      const std::string& error, const std::function<bool(bool, bool)>& submit,
                      bool gpu = false, bool systems = false) {
        ImGui::PushID(owner);
        auto& local_error = local_errors_[owner];
        if (!status.value("compiled", false)) {
            ImGui::TextWrapped(status.contains("compiled")
                                   ? "Profiling was disabled in this build."
                                   : "Waiting for a CPU profile snapshot from the runtime.");
            if (!error.empty())
                ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopID();
            return;
        }
        const bool enabled = status.value("enabled", false);
        ImGui::TextUnformatted(enabled ? "COLLECTING" : "STOPPED - samples retained");
        ImGui::TextWrapped(
            gpu       ? "GPU pass durations; separate rows must not be summed into a frame time."
            : systems ? "Native system lifetime CPU totals. Clear removes this view, not "
                        "Flecs counters. These are not per-tick or frame times."
                      : "CPU wall time only. Nested scopes overlap.");
        ImGui::BeginDisabled(pending);
        ImGui::BeginDisabled(!ready || !submit);
        if (ImGui::Button(enabled ? "Stop collection" : "Start collection")) {
            try {
                local_error =
                    submit(!enabled, false) ? "" : "Collection control is busy; try again.";
            } catch (const std::exception& exception) {
                local_error = exception.what();
            }
        }
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":collection");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!submit);
        if (ImGui::Button(gpu ? "Clear editor samples" : "Clear samples")) {
            try {
                local_error = submit(enabled, true) ? "" : "Collection control is busy; try again.";
            } catch (const std::exception& exception) {
                local_error = exception.what();
            }
        }
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":clear");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (gpu || std::string(owner) == "editor")
            help("Clear removes both editor CPU and GPU records and discards their unfinished "
                 "samples.");
        if (pending)
            ImGui::TextUnformatted("Waiting for runtime acknowledgement...");
        if (!error.empty() || !local_error.empty())
            ImGui::TextWrapped("%s", error.empty() ? local_error.c_str() : error.c_str());
        if (systems) {
            draw_systems(status.value("ecs", nlohmann::json::object()));
            ImGui::PopID();
            return;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Scope filter", "Filter category or scope...", filter_,
                                 sizeof(filter_));
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":filter");
        try {
            const auto rows = summarize_cpu_profiles(status.at("samples"), gpu);
            ImGui::Text("Recent records: %zu / 512 | Scope groups: %zu",
                        status.at("samples").size(), rows.size());
            if (rows.empty())
                ImGui::TextWrapped("Start collection, then use the editor or run the game. "
                                   "Clear starts a fresh observation window.");
            if (ImGui::BeginTable(
                    "CPU scopes", 7,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                    {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)})) {
                for (const char* column : {"Category", "Scope", "Count", "Average ms", "Max ms",
                                           "Latest ms", gpu ? "Frame" : "Tick"})
                    ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                            (std::string(column) == "Scope" ? 220.f : 105.f) *
                                                interface_scale);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (const auto& row : rows) {
                    if (*filter_ && row.category.find(filter_) == std::string::npos &&
                        row.name.find(filter_) == std::string::npos)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.category.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.name.c_str());
                    FORGE_UI_PROBE(std::string("profiler:") + owner + ":row:" + row.name);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", row.count);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", row.total * 1000 / row.count);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", row.maximum * 1000);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", row.latest * 1000);
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(row.tick));
                }
                ImGui::EndTable();
            }
        } catch (const std::exception& exception) {
            ImGui::TextWrapped("Profile data rejected: %s", exception.what());
        }
        ImGui::PopID();
    }
    void draw_systems(const nlohmann::json& status) {
        try {
            if (!status.value("available", false)) {
                ImGui::TextWrapped(
                    "%s", status.value("reason", "Waiting for native system data.").c_str());
                return;
            }
            const auto& entries = status.at("entries");
            if (!entries.is_array() || entries.size() > 256 ||
                status.at("timing") != "native_system_lifetime" ||
                !status.at("tick").is_number_unsigned() || status.at("tick") == 0)
                throw std::runtime_error("Invalid native system snapshot");
            // Validate before starting a table so malformed transport data cannot
            // leave an unfinished ImGui table on an exception.
            for (const auto& row : entries) {
                if (row.at("kind") == "merge")
                    continue;
                if (row.at("kind") != "system" || !row.at("id").is_number_unsigned() ||
                    row.at("id") == 0 || !row.at("name").is_string() ||
                    !row.at("phase").is_string() ||
                    row.at("name").get_ref<const std::string&>().size() > 480 ||
                    row.at("phase").get_ref<const std::string&>().size() > 480 ||
                    !row.at("immediate").is_boolean() || !row.at("multi_threaded").is_boolean() ||
                    (!row.at("cpu_seconds").is_null() &&
                     (!row.at("cpu_seconds").is_number() ||
                      !std::isfinite(row.at("cpu_seconds").get<double>()) ||
                      row.at("cpu_seconds") < 0)))
                    throw std::runtime_error("Invalid native system row");
            }
            ImGui::Text("Native plan at tick %llu | %zu / %u entries",
                        static_cast<unsigned long long>(status.at("tick").get<std::uint64_t>()),
                        entries.size(), status.at("total_entries").get<unsigned>());
            ImGui::TextWrapped("Systems without matches are absent. Merge rows are native "
                               "synchronization points. This plan is not an execution trace.");
            if (status.at("truncated").get<bool>())
                ImGui::TextWrapped("Showing the first 256 entries; the native plan is larger.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##System filter", "Filter system or phase...", filter_,
                                     sizeof(filter_));
            FORGE_UI_PROBE("profiler:game:system-filter");
            if (ImGui::BeginTable(
                    "Native systems", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                    {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)})) {
                for (const char* column : {"Order", "System", "Phase", "CPU lifetime ms", "Mode"})
                    ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                            (std::string_view(column) == "System" ? 250.f : 145.f) *
                                                interface_scale);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    const auto& row = entries[i];
                    const bool merge = row.at("kind") == "merge";
                    const auto name = merge ? "Merge" : row.at("name").get<std::string>();
                    const auto phase = merge ? "" : row.at("phase").get<std::string>();
                    if (*filter_ && !merge && name.find(filter_) == std::string::npos &&
                        phase.find(filter_) == std::string::npos)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", i + 1);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name.c_str());
                    FORGE_UI_PROBE(std::string("profiler:game:system:") + name);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(phase.c_str());
                    ImGui::TableNextColumn();
                    if (!merge && !row.at("cpu_seconds").is_null())
                        ImGui::Text("%.3f", row.at("cpu_seconds").get<double>() * 1000);
                    else
                        ImGui::TextUnformatted("--");
                    ImGui::TableNextColumn();
                    if (!merge)
                        ImGui::TextUnformatted(row.at("multi_threaded").get<bool>()
                                                   ? "Multi-threaded"
                                               : row.at("immediate").get<bool>() ? "Immediate"
                                                                                 : "Deferred");
                }
                ImGui::EndTable();
            }
        } catch (const std::exception& error) {
            ImGui::TextWrapped("System data rejected: %s", error.what());
        }
    }
};
} // namespace forge::ui
