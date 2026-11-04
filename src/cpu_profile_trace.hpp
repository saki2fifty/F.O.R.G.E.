#pragma once
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
// Common copied-record admission for native summaries and measured CPU intervals.
struct ProfileRecordValue {
    std::string category, name;
    double seconds = 0;
    std::uint64_t position = 0;
    unsigned native_stage = 0;
};
ProfileRecordValue profile_record_value(const nlohmann::json&, bool gpu = false);
struct CpuScopeSpan {
    std::string category, name;
    double start = 0, seconds = 0; // Relative seconds within this retained snapshot.
    std::uint64_t tick = 0, sequence = 0;
    std::size_t lane = 0;      // Layout within a measured stage; not an OS thread ID.
    unsigned native_stage = 0; // Caller0 or an admitted native worker stage.
};
struct CpuScopeTimeline {
    std::vector<CpuScopeSpan> spans;
    std::size_t duration_only = 0, lanes = 0;
    double origin = 0, seconds = 0;
    std::vector<unsigned> lane_stages;
    std::size_t measured_threads = 0;
    bool native_workers = false;
};
CpuScopeTimeline cpu_scope_timeline(const nlohmann::json& samples);
std::string cpu_scope_lane_label(const CpuScopeTimeline&, std::size_t lane);
// Independent Trace Event layout tracks keep crossing intervals valid. All records
// publish through one service owner; worker records retain native stage identity.
// No cross-process clock correlation or OS thread IDs are inferred.
nlohmann::json cpu_scope_trace(const nlohmann::json& status, std::string_view owner,
                               std::string_view captured_at);
nlohmann::json cpu_scope_trace(const nlohmann::json& status, std::string_view owner,
                               std::string_view captured_at, std::size_t offset, std::size_t limit);
std::string cpu_scope_trace_text(const nlohmann::json& status, std::string_view owner,
                                 std::string_view captured_at);
} // namespace forge
