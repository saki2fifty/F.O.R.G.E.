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
inline std::vector<CpuProfileRow> summarize_cpu_profiles(const nlohmann::json& samples) {
    if (!samples.is_array() || samples.size() > 512)
        throw std::runtime_error("CPU profile snapshot exceeds its record bound");
    std::map<std::pair<std::string, std::string>, CpuProfileRow> groups;
    for (const auto& sample : samples) {
        if (!sample.is_object() || !sample.at("category").is_string() ||
            !sample.at("name").is_string() || !sample.at("seconds").is_number() ||
            !sample.at("tick").is_number_integer() || sample.at("tick") < 0 ||
            sample.at("count") != 1)
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
        row.tick = sample.at("tick").get<std::uint64_t>();
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
              const std::function<bool(bool, bool)>& submit) {
        if (!open)
            return false;
        bool watch = false;
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::max(1.f, std::min(820 * interface_scale, available.x - 20)),
                                  std::max(1.f, std::min(500 * interface_scale, available.y - 20))},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("CPU Profiler###CpuProfiler", &open)) {
            ImGui::TextWrapped("CPU wall time only. Nested scopes overlap. The most recent 512 "
                               "completed scopes are retained; collection adds overhead.");
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
                    else
                        draw_records("game", game, true, pending, error, submit);
                    ImGui::EndTabItem();
                } else
                    FORGE_UI_PROBE("profiler:game-tab");
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
                      const std::string& error, const std::function<bool(bool, bool)>& submit) {
        ImGui::PushID(owner);
        auto& local_error = local_errors_[owner];
        if (!status.value("compiled", false)) {
            ImGui::TextWrapped(status.contains("compiled")
                                   ? "CPU collection was disabled in this build."
                                   : "Waiting for a CPU profile snapshot from the runtime.");
            if (!error.empty())
                ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopID();
            return;
        }
        const bool enabled = status.value("enabled", false);
        ImGui::TextUnformatted(enabled ? "COLLECTING" : "STOPPED - samples retained");
        ImGui::BeginDisabled(!ready || pending);
        if (ImGui::Button(enabled ? "Stop collection" : "Start collection"))
            local_error = submit(!enabled, false) ? "" : "Runtime is busy; try again.";
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":collection");
        ImGui::SameLine();
        if (ImGui::Button("Clear samples"))
            local_error = submit(enabled, true) ? "" : "Runtime is busy; try again.";
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":clear");
        ImGui::EndDisabled();
        if (pending)
            ImGui::TextUnformatted("Waiting for runtime acknowledgement...");
        if (!error.empty() || !local_error.empty())
            ImGui::TextWrapped("%s", error.empty() ? local_error.c_str() : error.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Scope filter", "Filter category or scope...", filter_,
                                 sizeof(filter_));
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":filter");
        try {
            const auto rows = summarize_cpu_profiles(status.at("samples"));
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
                for (const char* column :
                     {"Category", "Scope", "Count", "Average ms", "Max ms", "Latest ms", "Tick"})
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
            ImGui::TextWrapped("CPU profile data rejected: %s", exception.what());
        }
        ImGui::PopID();
    }
};
} // namespace forge::ui
