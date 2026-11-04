#include "cpu_profile_trace.hpp"
#include <algorithm>
#include <cmath>
#include <forge/runtime_execution.hpp>
#include <forge/services.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
namespace forge {
using Json = nlohmann::json;
ProfileRecordValue profile_record_value(const Json& sample, bool gpu) {
    const auto* position = gpu ? "frame" : "tick";
    if (!sample.is_object() || sample.value("clock", "cpu") != (gpu ? "gpu" : "cpu") ||
        !sample.at("category").is_string() || !sample.at("name").is_string() ||
        !sample.at("seconds").is_number() || !sample.at(position).is_number_integer() ||
        sample.at(position) < (gpu ? 1 : 0) || sample.at("count") != 1)
        throw std::runtime_error("Invalid profile snapshot record");
    ProfileRecordValue result{sample.at("category"), sample.at("name"), sample.at("seconds"),
                              sample.at(position)};
    if (result.category.empty() || result.category.size() > 128 || result.name.empty() ||
        result.name.size() > 128 || !std::isfinite(result.seconds) || result.seconds < 0 ||
        result.seconds > 3600)
        throw std::runtime_error("Invalid profile snapshot value");
    if (sample.contains("native_stage")) {
        const auto& stage = sample.at("native_stage");
        if (gpu || result.category != "ecs.system" || !sample.contains("start_seconds") ||
            !stage.is_number_integer() || stage <= 0 || stage >= runtime_ecs_worker_limit)
            throw std::runtime_error("Invalid measured native profile stage");
        result.native_stage = stage.get<unsigned>();
    }
    return result;
}
CpuScopeTimeline cpu_scope_timeline(const Json& samples) {
    if (!samples.is_array() || samples.size() > profile_record_capacity)
        throw std::runtime_error("CPU timeline exceeds the native record bound");
    CpuScopeTimeline result;
    std::set<std::uint64_t> sequences;
    std::set<unsigned> stages;
    for (const auto& sample : samples) {
        const auto value = profile_record_value(sample);
        if (!sample.contains("start_seconds")) {
            ++result.duration_only;
            continue;
        }
        if (!sample.at("start_seconds").is_number() || !sample.at("sequence").is_number_integer() ||
            sample.at("sequence") <= 0)
            throw std::runtime_error("Invalid CPU interval start or sequence");
        CpuScopeSpan span{value.category, value.name,     sample.at("start_seconds"),
                          value.seconds,  value.position, sample.at("sequence")};
        span.native_stage = value.native_stage;
        if (!std::isfinite(span.start) || !std::isfinite(span.start + span.seconds) ||
            !sequences.insert(span.sequence).second)
            throw std::runtime_error("CPU interval clock or sequence is invalid");
        result.spans.push_back(std::move(span));
        stages.insert(value.native_stage);
        result.native_workers |= value.native_stage != 0;
    }
    std::sort(result.spans.begin(), result.spans.end(), [](const auto& a, const auto& b) {
        if (a.start != b.start)
            return a.start < b.start;
        if (a.seconds != b.seconds)
            return a.seconds > b.seconds;
        return a.sequence < b.sequence;
    });
    if (!result.spans.empty())
        result.origin = result.spans.front().start;
    result.measured_threads = stages.size();
    std::vector<std::vector<double>> lane_ends;
    for (auto& span : result.spans) {
        span.start -= result.origin;
        const auto end = span.start + span.seconds;
        if (!std::isfinite(span.start * 1000000) || !std::isfinite(end * 1000000) ||
            end > double(std::numeric_limits<std::int64_t>::max()) / 1000000000)
            throw std::runtime_error("CPU interval window cannot be represented in microseconds");
        result.seconds = std::max(result.seconds, end);
        std::size_t lane = 0;
        for (; lane < lane_ends.size(); ++lane) {
            if (result.lane_stages[lane] != span.native_stage)
                continue;
            auto& ends = lane_ends[lane];
            while (!ends.empty() && ends.back() <= span.start)
                ends.pop_back();
            if (ends.empty() || end <= ends.back())
                break;
        }
        if (lane == lane_ends.size()) {
            lane_ends.emplace_back();
            result.lane_stages.push_back(span.native_stage);
        }
        lane_ends[lane].push_back(end);
        span.lane = lane;
    }
    result.lanes = lane_ends.size();
    return result;
}
std::string cpu_scope_lane_label(const CpuScopeTimeline& timeline, std::size_t lane) {
    const auto stage = timeline.lane_stages.at(lane);
    std::string label = stage ? "ECS worker stage " + std::to_string(stage) : "Owner-thread scopes";
    const auto overlap =
        std::count(timeline.lane_stages.begin(), timeline.lane_stages.begin() + lane, stage);
    if (overlap)
        label += " / logical overlap lane " + std::to_string(overlap + 1);
    return label;
}
Json cpu_scope_trace(const Json& status, std::string_view owner, std::string_view captured_at,
                     std::size_t offset, std::size_t limit) {
    if (offset > profile_record_capacity || !limit || limit > profile_record_capacity)
        throw std::runtime_error("CPU trace page exceeds the native record bound");
    if (owner.empty() || owner.size() > 128 || captured_at.empty() || captured_at.size() > 128)
        throw std::runtime_error("CPU trace requires a bounded owner and capture timestamp");
    const auto timeline = cpu_scope_timeline(status.at("samples"));
    Json source = Json::object();
    for (const auto* field : {"sequence", "epoch", "world_generation", "native_systems_dropped"})
        if (status.contains(field)) {
            if (!status.at(field).is_number_integer() || status.at(field) < 0)
                throw std::runtime_error("CPU trace source metadata is invalid");
            source[field] = std::to_string(status.at(field).get<std::uint64_t>());
        }
    const auto begin = std::min(offset, timeline.spans.size());
    const auto count = std::min(limit, timeline.spans.size() - begin);
    std::set<std::size_t> selected_lanes;
    for (std::size_t i = begin; i < begin + count; ++i)
        selected_lanes.insert(timeline.spans[i].lane);
    Json events = Json::array();
    events.push_back(
        {{"ph", "M"}, {"name", "process_name"}, {"pid", 1}, {"args", {{"name", owner}}}});
    for (const auto lane : selected_lanes)
        events.push_back({{"ph", "M"},
                          {"name", "thread_name"},
                          {"pid", 1},
                          {"tid", lane + 1},
                          {"args", {{"name", cpu_scope_lane_label(timeline, lane)}}}});
    for (std::size_t i = begin; i < begin + count; ++i) {
        const auto& span = timeline.spans[i];
        Json arguments{{"tick", std::to_string(span.tick)},
                       {"sequence", std::to_string(span.sequence)}};
        if (timeline.native_workers)
            arguments["native_stage"] = span.native_stage;
        events.push_back({{"ph", "X"},
                          {"name", span.name},
                          {"cat", span.category},
                          {"pid", 1},
                          {"tid", span.lane + 1},
                          {"ts", span.start * 1000000},
                          {"dur", span.seconds * 1000000},
                          {"args", std::move(arguments)}});
    }
    Json result{{"traceEvents", std::move(events)},
                {"displayTimeUnit", "ms"},
                {"otherData",
                 {{"producer", "FORGE CPU scopes"},
                  {"owner", owner},
                  {"captured_at", captured_at},
                  {"clock", "owner-local CPU clock; relative microseconds"},
                  {"origin_seconds", timeline.origin},
                  {"source", std::move(source)},
                  {"measured_spans", timeline.spans.size()},
                  {"exported_spans", count},
                  {"page", {{"offset", offset}, {"limit", limit}}},
                  {"duration_only_records", timeline.duration_only},
                  {"owner_threads", 1},
                  {"layout_lanes", timeline.lanes},
                  {"lane_semantics", "Logical overlap layout for one owner thread; no measured "
                                     "concurrency or OS thread IDs"}}}};
    if (timeline.native_workers) {
        auto& metadata = result["otherData"];
        const std::set<unsigned> stages(timeline.lane_stages.begin(), timeline.lane_stages.end());
        metadata["native_stages"] = stages;
        metadata["measured_threads"] = timeline.measured_threads;
        metadata["clock"] = "process-local monotonic CPU clock; relative microseconds";
        metadata["lane_semantics"] =
            "Measured native ECS stages; logical overlap lanes stay within their stage. "
            "Stage/track identifiers are not OS thread IDs.";
    }
    return result;
}
Json cpu_scope_trace(const Json& status, std::string_view owner, std::string_view captured_at) {
    return cpu_scope_trace(status, owner, captured_at, 0, profile_record_capacity);
}
std::string cpu_scope_trace_text(const Json& status, std::string_view owner,
                                 std::string_view captured_at) {
    return cpu_scope_trace(status, owner, captured_at)
        .dump(2, ' ', false, Json::error_handler_t::replace);
}
} // namespace forge
