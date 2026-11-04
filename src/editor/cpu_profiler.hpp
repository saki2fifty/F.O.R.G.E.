#pragma once
#include "../cpu_profile_trace.hpp"
#include "../native_system_access.hpp"
#include "event_time.hpp"
#include "native_system_dependencies.hpp"
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
    unsigned native_stage = 0;
};
inline std::vector<CpuProfileRow> summarize_cpu_profiles(const nlohmann::json& samples,
                                                         bool gpu = false) {
    if (!samples.is_array() || samples.size() > profile_record_capacity)
        throw std::runtime_error("CPU profile snapshot exceeds its record bound");
    std::map<std::tuple<std::string, std::string, unsigned>, CpuProfileRow> groups;
    for (const auto& sample : samples) {
        const auto value = profile_record_value(sample, gpu);
        const auto& category = value.category;
        const auto& name = value.name;
        const auto seconds = value.seconds;
        auto& row = groups[{category, name, value.native_stage}];
        row.category = category;
        row.name = name;
        row.native_stage = value.native_stage;
        ++row.count;
        row.total += seconds;
        row.maximum = std::max(row.maximum, seconds);
        row.latest = seconds;
        row.tick = value.position;
    }
    std::vector<CpuProfileRow> result;
    for (auto& [key, row] : groups)
        result.push_back(std::move(row));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.total != b.total)
            return a.total > b.total;
        return std::tie(a.category, a.name, a.native_stage) <
               std::tie(b.category, b.name, b.native_stage);
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
              const std::function<bool(bool, bool)>& submit_gpu = {},
              const std::function<bool(bool)>& submit_systems = {}) {
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
                            draw_records("game", game, true, pending, error, submit, false, false,
                                         submit_systems);
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
    std::map<std::string, bool> timeline_views_;
    bool dependency_nodes_ = false;
    void draw_records(const char* owner, const nlohmann::json& status, bool ready, bool pending,
                      const std::string& error, const std::function<bool(bool, bool)>& submit,
                      bool gpu = false, bool systems = false,
                      const std::function<bool(bool)>& submit_systems = {}) {
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
            : systems ? "Native lifetime totals; not per-tick or frame times."
                      : "CPU wall time only. Nested scopes overlap.");
        if (systems)
            help("Clear removes this observation, not native lifetime counters.");
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
            if (status.contains("ecs"))
                draw_systems(status.at("ecs"));
            else
                ImGui::TextWrapped("Waiting for native system data.");
            ImGui::PopID();
            return;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Scope filter", "Filter category or scope...", filter_,
                                 sizeof(filter_));
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":filter");
        if (!gpu && std::string_view(owner) == "game") {
            const bool supported = status.value("native_systems_available", false);
            bool native = status.value("native_systems", false);
            ImGui::BeginDisabled(pending || !ready || !submit_systems || !supported);
            if (ImGui::Checkbox("Trace ECS systems", &native)) {
                try {
                    local_error =
                        submit_systems(native) ? "" : "Collection control is busy; try again.";
                } catch (const std::exception& exception) {
                    local_error = exception.what();
                }
            }
            FORGE_UI_PROBE("profiler:game:native-systems");
            ImGui::EndDisabled();
            help("With collection on, retain actual inclusive native ECS dispatch intervals "
                 "on the caller and supported native worker stages, up to depth64. This "
                 "adds overhead and shares the512-record history. Native stage labels "
                 "identify measured dispatches, not OS IDs or hardware CPU utilization. "
                 "Custom clocks and external worker configurations retain owner-only capture.");
            if (!supported)
                ImGui::TextWrapped("Native system tracing is unavailable in this Play host.");
            if (const auto dropped = status.value("native_systems_dropped", std::uint64_t{}))
                ImGui::TextWrapped("Skipped native dispatches (depth/buffer limits): %llu",
                                   static_cast<unsigned long long>(dropped));
        }
        if (!gpu) {
            bool& timeline = timeline_views_[owner];
            ImGui::Checkbox("Timeline", &timeline);
            FORGE_UI_PROBE(std::string("profiler:") + owner + ":timeline");
            help("Measured CPU intervals only. Duration-only SDK/explicit records remain in "
                 "the summary. Native stages identify measured threads; logical overlap "
                 "lanes within a stage are layout only.");
            if (timeline) {
                try {
                    draw_timeline(owner, status);
                } catch (const std::exception& exception) {
                    ImGui::TextWrapped("Profile data rejected: %s", exception.what());
                }
                ImGui::PopID();
                return;
            }
        }
        try {
            const auto rows = summarize_cpu_profiles(status.at("samples"), gpu);
            const bool stages = std::any_of(rows.begin(), rows.end(),
                                            [](const auto& row) { return row.native_stage != 0; });
            ImGui::Text("Recent records: %zu / 512 | Scope groups: %zu",
                        status.at("samples").size(), rows.size());
            if (rows.empty())
                ImGui::TextWrapped("Start collection, then use the editor or run the game. "
                                   "Clear starts a fresh observation window.");
            if (ImGui::BeginTable(
                    "CPU scopes", stages ? 8 : 7,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                    {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)})) {
                for (const char* column : {"Category", "Scope", "Count", "Average ms", "Max ms",
                                           "Latest ms", gpu ? "Frame" : "Tick"})
                    ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                            (std::string(column) == "Scope" ? 220.f : 105.f) *
                                                interface_scale);
                if (stages)
                    ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed,
                                            105.f * interface_scale);
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
                    FORGE_UI_PROBE(
                        std::string("profiler:") + owner + ":row:" + row.name +
                        (row.native_stage ? ":stage:" + std::to_string(row.native_stage) : ""));
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
                    if (stages) {
                        ImGui::TableNextColumn();
                        if (row.native_stage)
                            ImGui::Text("Worker %u", row.native_stage);
                        else
                            ImGui::TextUnformatted("Owner");
                    }
                }
                ImGui::EndTable();
            }
        } catch (const std::exception& exception) {
            ImGui::TextWrapped("Profile data rejected: %s", exception.what());
        }
        ImGui::PopID();
    }
    void draw_timeline(const char* owner, const nlohmann::json& status) {
        const auto timeline = cpu_scope_timeline(status.at("samples"));
        ImGui::TextWrapped("Measured threads: %zu | Intervals: %zu | Duration-only records: %zu",
                           timeline.measured_threads, timeline.spans.size(),
                           timeline.duration_only);
        ImGui::Text("Retained window: %.3f ms", timeline.seconds * 1000);
        ImGui::BeginDisabled(timeline.spans.empty());
        if (ImGui::Button("Copy trace JSON")) {
            try {
                const auto trace = cpu_scope_trace_text(
                    status,
                    std::string_view(owner) == "editor" ? "FORGE Editor" : "FORGE Play runtime",
                    event_time_local());
                ImGui::SetClipboardText(trace.c_str());
                local_errors_[owner].clear();
            } catch (const std::exception& exception) {
                local_errors_[owner] = exception.what();
            }
        }
        FORGE_UI_PROBE(std::string("profiler:") + owner + ":trace-copy");
        ImGui::EndDisabled();
        help("Copies all retained measured CPU spans as Trace Event JSON, including capture "
             "date/time and UTC offset. Filter changes this view only. Native ECS stage "
             "tracks identify measured workers; overlap lanes stay within each stage. "
             "Editor/runtime clocks are separate; stage IDs are not OS IDs.");
        if (timeline.spans.empty()) {
            ImGui::TextWrapped("Collect completed host scopes or render frames to see intervals. "
                               "A duration without its measured start cannot be placed here.");
            return;
        }
        std::vector<const CpuScopeSpan*> visible;
        for (const auto& span : timeline.spans)
            if (!*filter_ || span.category.find(filter_) != std::string::npos ||
                span.name.find(filter_) != std::string::npos)
                visible.push_back(&span);
        if (!ImGui::BeginTable(
                "Measured CPU intervals", timeline.native_workers ? 5 : 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_ScrollX,
                {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)}))
            return;
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 220 * interface_scale);
        ImGui::TableSetupColumn("Start ms", ImGuiTableColumnFlags_WidthFixed,
                                100 * interface_scale);
        ImGui::TableSetupColumn("Duration ms", ImGuiTableColumnFlags_WidthFixed,
                                110 * interface_scale);
        ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthStretch);
        if (timeline.native_workers)
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed,
                                    100 * interface_scale);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(visible.size()));
        const auto window_seconds = std::max(timeline.seconds, .000001);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& span = *visible[std::size_t(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted((span.category + " / " + span.name).c_str());
                FORGE_UI_PROBE(
                    std::string("profiler:") + owner + ":span:" + span.name +
                    (span.native_stage ? ":stage:" + std::to_string(span.native_stage) : ""));
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", span.start * 1000);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", span.seconds * 1000);
                ImGui::TableNextColumn();
                const auto origin = ImGui::GetCursorScreenPos();
                const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
                const float height = 18 * interface_scale;
                ImGui::InvisibleButton("##Measured interval", {width, height});
                const auto minimum =
                    origin.x +
                    std::min(width - 1.f,
                             width * float(std::clamp(span.start / window_seconds, 0., 1.)));
                const auto maximum = std::min(
                    origin.x + width,
                    std::max(minimum + 1,
                             origin.x + width * float(std::clamp((span.start + span.seconds) /
                                                                     window_seconds,
                                                                 0., 1.))));
                ImGui::GetWindowDrawList()->AddRectFilled(
                    {minimum, origin.y + 2}, {maximum, origin.y + height - 2},
                    ImGui::GetColorU32(ImGuiCol_PlotHistogram));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(span.name.c_str());
                    ImGui::Text("Start %.6f ms | Duration %.6f ms", span.start * 1000,
                                span.seconds * 1000);
                    ImGui::Text("Tick %llu | Sequence %llu",
                                static_cast<unsigned long long>(span.tick),
                                static_cast<unsigned long long>(span.sequence));
                    ImGui::EndTooltip();
                }
                if (timeline.native_workers) {
                    ImGui::TableNextColumn();
                    if (span.native_stage)
                        ImGui::Text("Worker %u", span.native_stage);
                    else
                        ImGui::TextUnformatted("Owner");
                }
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    void draw_system_access(const nlohmann::json& entries, const nlohmann::json* observation,
                            bool overlaps) {
        if (!observation || observation->empty()) {
            ImGui::TextWrapped("Declared access is unavailable in this snapshot. Collect and "
                               "refresh a fixed tick with a matching runtime.");
            return;
        }
        const auto& diagnostics = *observation;
        ImGui::TextWrapped("%u / %u terms copied | %u unresolved | %zu possible overlaps",
                           diagnostics.at("copied_terms").get<unsigned>(),
                           diagnostics.at("total_terms").get<unsigned>(),
                           diagnostics.at("unresolved_terms").get<unsigned>(),
                           diagnostics.at("pairs").size());
        if (diagnostics.at("terms_truncated").get<bool>() ||
            diagnostics.at("truncated").get<bool>())
            ImGui::TextWrapped("Partial observation: a term, comparison or overlap limit was "
                               "reached. An empty list does not prove independent access.");
        const char* explanation =
            overlaps
                ? "Possible shared component access in current native order. Query filters may "
                  "select disjoint entities. These are declarations, not measured races."
                : "Declared query fields, not observed reads/writes. Default access permits owned "
                  "writes; shared default fields are read only. Review unresolved sources "
                  "separately.";
        const auto available = ImGui::GetContentRegionAvail();
        const auto explanation_height =
            ImGui::CalcTextSize(explanation, nullptr, false, std::max(1.f, available.x)).y;
        const bool compact = available.y - explanation_height < 160 * interface_scale;
        ImGui::TextWrapped("%s", compact ? "Declarations only. Possible overlaps are not races."
                                         : explanation);
        if (compact)
            help(explanation);
        if (!ImGui::BeginTable(
                overlaps ? "Access overlaps" : "Declared access", overlaps ? 5 : 7,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX |
                    ImGuiTableFlags_ScrollY,
                {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)}))
            return;
        if (overlaps) {
            for (const char* column :
                 {"Earlier system", "Later system", "Component", "Access", "Scope"})
                ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                        (std::string_view(column) == "Scope" ? 180.f : 230.f) *
                                            interface_scale);
        } else {
            for (const char* column :
                 {"Order", "System", "Component", "Access", "Source", "Operator", "Review"})
                ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed,
                                        (std::string_view(column) == "Order" ? 55.f
                                         : std::string_view(column) == "System" ||
                                                 std::string_view(column) == "Component"
                                             ? 230.f
                                             : 130.f) *
                                            interface_scale);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        if (overlaps) {
            for (const auto& pair : diagnostics.at("pairs")) {
                const auto& first = entries.at(pair.at("first").get<std::size_t>());
                const auto& second = entries.at(pair.at("second").get<std::size_t>());
                const auto a = first.at("name").get<std::string>();
                const auto b = second.at("name").get<std::string>();
                const auto component = pair.at("component").get<std::string>();
                if (*filter_ && a.find(filter_) == std::string::npos &&
                    b.find(filter_) == std::string::npos &&
                    component.find(filter_) == std::string::npos)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(a.c_str());
                FORGE_UI_PROBE(std::string("profiler:game:overlap:") + a + ":" + b);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(component.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pair.at("kind") == "write_write" ? "Write / write"
                                                                        : "Read / write");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pair.at("scope") == "same_entity"
                                           ? "Same fixed entity"
                                           : "Possible entity overlap");
            }
        } else {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& row = entries[i];
                if (!row.contains("access"))
                    continue;
                const auto name = row.at("name").get<std::string>();
                for (const auto& term : row.at("access").at("terms")) {
                    const auto component = term.at("name").get<std::string>();
                    auto source = term.at("source") == "entity"
                                      ? term.at("source_name").get<std::string>()
                                      : term.at("source").get<std::string>();
                    if (term.at("source") != "entity" && term.at("source_id") != 0)
                        source += " from " + term.at("source_name").get<std::string>();
                    if (*filter_ && name.find(filter_) == std::string::npos &&
                        component.find(filter_) == std::string::npos &&
                        source.find(filter_) == std::string::npos)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", i + 1);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name.c_str());
                    FORGE_UI_PROBE(std::string("profiler:game:access:") + name + ":" +
                                   std::to_string(term.at("index").get<unsigned>()));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(component.c_str());
                    ImGui::TableNextColumn();
                    auto access = term.at("access").get<std::string>();
                    if (access == "default" && term.at("analysis") == "included")
                        access += term.at("writes").get<bool>() ? " (R/W)" : " (read)";
                    ImGui::TextUnformatted(access.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(source.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", source.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        term.at("operator").get_ref<const std::string&>().c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(term.at("analysis") == "included"  ? "Compared"
                                           : term.at("analysis") == "ignored" ? "Not compared"
                                                                              : "Unresolved");
                }
            }
        }
        ImGui::EndTable();
    }
    void draw_worker_writes(const Json& entries, const Json* observation) {
        if (!observation) {
            ImGui::TextWrapped("Worker access is unavailable in this snapshot. Collect and "
                               "refresh a fixed tick with a matching runtime.");
            return;
        }
        const auto& data = *observation;
        ImGui::TextWrapped(
            "%u stages | %u configured systems | %u / %u terms", data.at("workers").get<unsigned>(),
            data.at("system_count").get<unsigned>(), data.at("copied_terms").get<unsigned>(),
            data.at("total_terms").get<unsigned>());
        ImGui::TextWrapped("%zu / %u fixed-source writes | %u unresolved terms",
                           data.at("writes").size(), data.at("total_writes").get<unsigned>(),
                           data.at("unresolved_terms").get<unsigned>());
        if (data.at("terms_truncated").get<bool>() || data.at("truncated").get<bool>())
            ImGui::TextWrapped("Partial observation: terms or write rows were omitted.");
        ImGui::TextWrapped("Shared write declarations in partitioned systems; not measured races.");
        help("Only multi-threaded systems with more than one configured stage and a This-source "
             "query are inspected. Concrete fixed-entity writes can share storage across "
             "callbacks. Owned self fields and shared reads are excluded from this check. "
             "Optional fields may be absent. Traversal, complex queries, omitted declarations "
             "and custom callback/run side effects remain outside the resolved coverage. "
             "An empty table does not establish race freedom or measured concurrency.");
        if (!ImGui::BeginTable(
                "Worker shared writes", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX |
                    ImGuiTableFlags_ScrollY,
                {0, std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y)}))
            return;
        for (const char* label : {"System", "Component", "Fixed source", "Term"})
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed,
                                    (std::string_view(label) == "Term" ? 80.f : 250.f) *
                                        interface_scale);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& write : data.at("writes")) {
            const auto& row = entries.at(write.at("system").get<std::size_t>());
            if (!native_plan_row_matches(row, filter_))
                continue;
            const auto& term = row.at("access").at("terms").at(write.at("term").get<std::size_t>());
            const auto& name = row.at("name").get_ref<const std::string&>();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            FORGE_UI_PROBE(std::string("profiler:game:worker-write:") + name + ":" +
                           std::to_string(write.at("term").get<unsigned>()));
            for (const char* key : {"name", "source_name"}) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(term.at(key).get_ref<const std::string&>().c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", term.at("index").get<unsigned>());
        }
        ImGui::EndTable();
    }
    void draw_systems(const nlohmann::json& status) {
        try {
            if (!status.value("available", false)) {
                ImGui::TextWrapped(
                    "%s", status.value("reason", "Waiting for native system data.").c_str());
                return;
            }
            const auto& entries = status.at("entries");
            if (!entries.is_array() || entries.size() > detail::native_system_plan_capacity ||
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
            const auto* diagnostics =
                status.contains("access_diagnostics") ? &status.at("access_diagnostics") : nullptr;
            detail::validate_native_system_access(entries, diagnostics);
            const auto* worker_access =
                status.contains("worker_access") ? &status.at("worker_access") : nullptr;
            if (worker_access)
                detail::validate_native_system_worker_access(entries, *worker_access,
                                                             status.value("ecs_workers", 1u));
            const auto* dependencies =
                status.contains("dependencies") ? &status.at("dependencies") : nullptr;
            if (dependencies) {
                std::vector<ecs_entity_t> roots;
                for (const auto& row : entries)
                    if (row.at("kind") == "system")
                        roots.push_back(row.at("id").get<ecs_entity_t>());
                detail::validate_native_system_dependencies(roots, *dependencies);
            }
            ImGui::Text("Native plan at tick %llu | %zu / %u entries",
                        static_cast<unsigned long long>(status.at("tick").get<std::uint64_t>()),
                        entries.size(), status.at("total_entries").get<unsigned>());
            ImGui::TextWrapped("Native order; not an execution trace.");
            help("Systems without matches are absent. Merge rows are native synchronization "
                 "points; conditional systems need not run on every invocation.");
            if (const auto workers = status.value("ecs_workers", 1u); workers > 1) {
                ImGui::TextWrapped("%u ECS stages. Per-system lifetime timings are unavailable "
                                   "with workers; whole fixed-tick timings remain available.",
                                   workers);
                ImGui::TextWrapped("%s", status.value("system_trace_scope", "owner_thread") ==
                                                 "native_stages"
                                             ? "Native dispatch capture supports caller and worker "
                                               "stages. Enable Trace ECS systems in CPU scopes."
                                             : "Native dispatch capture covers the owner thread "
                                               "only in this configuration.");
            }
            if (status.at("truncated").get<bool>())
                ImGui::TextWrapped("Showing the first 256 entries; the native plan is larger.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##System filter", "Filter system, target or component...",
                                     filter_, sizeof(filter_));
            FORGE_UI_PROBE("profiler:game:system-filter");
            if (ImGui::BeginTabBar("Native ECS views")) {
                const bool plan = ImGui::BeginTabItem("Plan");
                FORGE_UI_PROBE("profiler:game:plan-tab");
                if (plan) {
                    if (ImGui::BeginTable("Native systems", 5,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                                          {0, std::max(80.f * interface_scale,
                                                       ImGui::GetContentRegionAvail().y)})) {
                        for (const char* column :
                             {"Order", "System", "First target", "CPU lifetime ms", "Mode"})
                            ImGui::TableSetupColumn(
                                column, ImGuiTableColumnFlags_WidthFixed,
                                (std::string_view(column) == "System" ? 250.f : 145.f) *
                                    interface_scale);
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableHeadersRow();
                        for (std::size_t i = 0; i < entries.size(); ++i) {
                            const auto& row = entries[i];
                            const bool merge = row.at("kind") == "merge";
                            const auto name = merge ? "Merge" : row.at("name").get<std::string>();
                            const auto phase = merge ? "" : row.at("phase").get<std::string>();
                            if (!native_plan_row_matches(row, filter_))
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
                                ImGui::TextUnformatted(
                                    row.at("multi_threaded").get<bool>() ? "Multi-threaded"
                                    : row.at("immediate").get<bool>()    ? "Immediate"
                                                                         : "Deferred");
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                const bool access = ImGui::BeginTabItem("Declared access");
                FORGE_UI_PROBE("profiler:game:access-tab");
                if (access) {
                    draw_system_access(entries, diagnostics, false);
                    ImGui::EndTabItem();
                }
                const bool overlaps = ImGui::BeginTabItem("Access overlaps");
                FORGE_UI_PROBE("profiler:game:overlaps-tab");
                if (overlaps) {
                    draw_system_access(entries, diagnostics, true);
                    ImGui::EndTabItem();
                }
                const bool ordering = ImGui::BeginTabItem("Ordering links");
                FORGE_UI_PROBE("profiler:game:dependencies-tab");
                if (ordering) {
                    draw_native_system_dependencies(entries, dependencies, filter_,
                                                    dependency_nodes_);
                    ImGui::EndTabItem();
                }
                const bool worker_writes = ImGui::BeginTabItem("Worker writes");
                FORGE_UI_PROBE("profiler:game:worker-writes-tab");
                if (worker_writes) {
                    draw_worker_writes(entries, worker_access);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        } catch (const std::exception& error) {
            ImGui::TextWrapped("System data rejected: %s", error.what());
        }
    }
};
} // namespace forge::ui
