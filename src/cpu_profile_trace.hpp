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
};
ProfileRecordValue profile_record_value(const nlohmann::json&, bool gpu = false);
struct CpuScopeSpan {
    std::string category, name;
    double start = 0, seconds = 0; // Relative seconds within this retained snapshot.
    std::uint64_t tick = 0, sequence = 0;
    std::size_t lane = 0; // Layout only; never a measured worker/OS thread ID.
};
struct CpuScopeTimeline {
    std::vector<CpuScopeSpan> spans;
    std::size_t duration_only = 0, lanes = 0;
    double origin = 0, seconds = 0;
};
CpuScopeTimeline cpu_scope_timeline(const nlohmann::json& samples);
// Independent Trace Event layout tracks keep crossing intervals valid. All records
// still belong to one service owner thread. No cross-process clock correlation.
nlohmann::json cpu_scope_trace(const nlohmann::json& status, std::string_view owner,
                               std::string_view captured_at);
nlohmann::json cpu_scope_trace(const nlohmann::json& status, std::string_view owner,
                               std::string_view captured_at, std::size_t offset, std::size_t limit);
std::string cpu_scope_trace_text(const nlohmann::json& status, std::string_view owner,
                                 std::string_view captured_at);
} // namespace forge
