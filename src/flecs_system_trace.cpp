#include "flecs_system_trace.hpp"
#include "json_output_text.hpp"
#include "utf8_boundary.hpp"
#include <array>
#include <flecs.h>
#include <optional>
#include <string>
namespace forge::detail {
struct NativeSystemTraceState {
    ServiceAccess services;
    std::uint64_t tick = 0;
    std::uint64_t epoch = 0, dropped = 0;
    std::array<std::optional<ProfileScope>, profile_system_depth_capacity> scopes;
    std::size_t depth = 0, excess = 0;
    NativeSystemTraceState(ServiceAccess owner, std::uint64_t position)
        : services(std::move(owner)), tick(position), epoch(services.profile_epoch()) {}
};
namespace {
thread_local NativeSystemTraceState* current = nullptr;
void push(const char*, std::size_t, const char* name) noexcept {
    if (!current)
        return;
    if (current->depth == current->scopes.size()) {
        ++current->excess;
        ++current->dropped;
        return;
    }
    auto& entry = current->scopes[current->depth++];
    try {
        if (!name || !*name)
            return;
        const std::string_view path(name);
        auto label = json_output_text(std::string(path.substr(0, utf8_range_end(path, 0, 128))))
                         .get<std::string>();
        if (path.size() > 128 || label.size() > 128) {
            label.resize(utf8_range_end(label, 0, 125));
            label += "...";
        }
        entry.emplace(current->services.profile("ecs.system", label.c_str(), current->tick));
    } catch (...) {
        // Instrumentation never escapes the native C callback boundary.
    }
}
void pop(const char*, std::size_t, const char*) noexcept {
    if (!current)
        return;
    if (current->excess) {
        --current->excess;
        return;
    }
    if (current->depth)
        current->scopes[--current->depth].reset();
}
} // namespace
bool native_system_tracing_available() noexcept {
    return ecs_os_api.perf_trace_push_ == push && ecs_os_api.perf_trace_pop_ == pop;
}
bool initialize_native_system_tracing() noexcept {
    if (native_system_tracing_available())
        return true;
    if (ecs_os_api.perf_trace_push_ || ecs_os_api.perf_trace_pop_)
        return false;
    ecs_os_set_api_defaults();
    auto api = ecs_os_get_api();
    api.perf_trace_push_ = push;
    api.perf_trace_pop_ = pop;
    ecs_os_set_api(&api);
    return native_system_tracing_available();
}
NativeSystemTraceCapture::NativeSystemTraceCapture(ServiceAccess services,
                                                   std::uint64_t tick) noexcept
    : previous_(current) {
    current = nullptr;
    try {
        if (native_system_tracing_available() && services.available(Capability::Profiling) &&
            services.profiling_enabled() && services.profiling_systems_enabled()) {
            state_ = std::make_unique<NativeSystemTraceState>(std::move(services), tick);
            current = state_.get();
        }
    } catch (...) {
    }
}
NativeSystemTraceCapture::~NativeSystemTraceCapture() {
    if (state_) {
        for (auto& scope : state_->scopes)
            if (scope)
                scope->cancel(); // No native pop means no completed dispatch.
        if (state_->dropped)
            try {
                state_->services.record_system_profile_drops(state_->epoch, state_->dropped);
            } catch (...) {
            }
    }
    current = previous_;
}
} // namespace forge::detail
