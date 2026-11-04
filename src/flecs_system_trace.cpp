#include "flecs_system_trace.hpp"
#include "json_output_text.hpp"
#include "utf8_boundary.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <flecs.h>
#include <forge/runtime_execution.hpp>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
namespace forge::detail {
struct WorkerTraceRecord {
    std::array<char, 129> name{};
    std::uint64_t capture = 0;
    double start = 0, seconds = 0;
    bool shortened = false;
};
struct WorkerTraceSlot {
    std::mutex mutex;
    std::array<WorkerTraceRecord, profile_record_capacity> records;
    std::size_t count = 0;
    std::atomic<std::uint64_t> dropped{0};
};
struct NativeWorkerTraceState {
    unsigned count = 0, created = 0;
    std::unique_ptr<WorkerTraceSlot[]> slots;
    std::atomic<std::uint64_t> active{0};
    std::atomic<bool> poisoned{false};
    std::uint64_t serial = 0;
    explicit NativeWorkerTraceState(unsigned stages)
        : count(stages - 1), slots(std::make_unique<WorkerTraceSlot[]>(count)) {}
};
struct NativeSystemTraceState {
    ServiceAccess services;
    std::uint64_t tick = 0;
    std::uint64_t epoch = 0, dropped = 0;
    std::array<std::optional<ProfileScope>, profile_system_depth_capacity> scopes;
    std::size_t depth = 0, excess = 0;
    std::shared_ptr<NativeWorkerTraceState> workers;
    std::uint64_t worker_capture = 0;
    NativeSystemTraceState(ServiceAccess owner, std::uint64_t position)
        : services(std::move(owner)), tick(position), epoch(services.profile_epoch()) {}
};
namespace {
thread_local NativeSystemTraceState* current = nullptr;
thread_local const std::shared_ptr<NativeWorkerTraceState>* starting_workers = nullptr;
struct WorkerThreadTrace {
    ecs_os_thread_callback_t callback;
    void* argument;
    std::shared_ptr<NativeWorkerTraceState> owner;
    unsigned stage;
    std::array<WorkerTraceRecord, profile_system_depth_capacity> stack;
    std::size_t depth = 0, excess = 0;
    std::uint64_t seen_capture = 0;
    WorkerThreadTrace(ecs_os_thread_callback_t action, void* value,
                      std::shared_ptr<NativeWorkerTraceState> state, unsigned index)
        : callback(action), argument(value), owner(std::move(state)), stage(index) {}
};
thread_local WorkerThreadTrace* worker_current = nullptr;
ecs_os_api_thread_new_t original_thread_new = nullptr;
double trace_now() noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void dropped(WorkerTraceSlot& slot) noexcept {
    auto value = slot.dropped.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint64_t>::max() &&
           !slot.dropped.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
    }
}
void worker_push(const char* name) noexcept {
    auto* thread = worker_current;
    if (!thread)
        return;
    auto& slot = thread->owner->slots[thread->stage - 1];
    const auto capture = thread->owner->active.load(std::memory_order_acquire);
    if (capture && capture != thread->seen_capture) {
        // Native progress has synchronized before a new capture can start.
        // Discard unfinished older scopes on their worker, never from the owner.
        thread->depth = thread->excess = 0;
        thread->seen_capture = capture;
    }
    if (thread->excess || thread->depth == thread->stack.size() || (!capture && thread->depth)) {
        ++thread->excess;
        if (capture)
            dropped(slot);
        return;
    }
    if (!capture)
        return;
    auto& frame = thread->stack[thread->depth++];
    frame = {};
    frame.capture = capture;
    frame.start = trace_now();
    if (!name)
        return;
    std::size_t count = 0;
    while (count < frame.name.size() - 1 && name[count]) {
        frame.name[count] = name[count];
        ++count;
    }
    frame.shortened = count == frame.name.size() - 1 && name[count];
}
void worker_pop() noexcept {
    auto* thread = worker_current;
    if (!thread)
        return;
    if (thread->excess) {
        --thread->excess;
        return;
    }
    if (!thread->depth)
        return;
    auto frame = thread->stack[--thread->depth];
    if (!frame.name[0] || frame.capture != thread->owner->active.load(std::memory_order_acquire))
        return;
    auto& slot = thread->owner->slots[thread->stage - 1];
    frame.seconds = trace_now() - frame.start;
    if (!std::isfinite(frame.start) || !std::isfinite(frame.seconds) || frame.seconds < 0 ||
        frame.seconds > 3600) {
        dropped(slot);
        return;
    }
    try {
        std::lock_guard guard(slot.mutex);
        if (frame.capture != thread->owner->active.load(std::memory_order_acquire))
            return;
        if (slot.count == slot.records.size())
            dropped(slot);
        else
            slot.records[slot.count++] = frame;
    } catch (...) {
        dropped(slot);
    }
}
void* traced_thread(void* raw) {
    const std::unique_ptr<WorkerThreadTrace> thread(static_cast<WorkerThreadTrace*>(raw));
    struct Binding {
        WorkerThreadTrace* previous = worker_current;
        ~Binding() { worker_current = previous; }
    } binding;
    worker_current = thread.get();
    return thread->callback(thread->argument);
}
ecs_os_thread_t trace_thread_new(ecs_os_thread_callback_t callback, void* argument) {
    if (!starting_workers || !*starting_workers)
        return original_thread_new(callback, argument);
    const auto& state = *starting_workers;
    if (state->created >= state->count) {
        state->poisoned.store(true, std::memory_order_relaxed);
        return original_thread_new(callback, argument);
    }
    const auto stage = ++state->created;
    auto* thread = new (std::nothrow) WorkerThreadTrace{callback, argument, state, stage};
    if (!thread) {
        state->poisoned.store(true, std::memory_order_relaxed);
        return original_thread_new(callback, argument);
    }
    const auto handle = original_thread_new(traced_thread, thread);
    if (!handle) {
        delete thread;
        state->poisoned.store(true, std::memory_order_relaxed);
    }
    return handle;
}
bool worker_hooks_available() noexcept {
    return native_system_tracing_available() && original_thread_new &&
           ecs_os_api.thread_new_ == trace_thread_new;
}
std::string trace_label(std::string_view path, bool shortened = false) {
    auto label = json_output_text(std::string(path.substr(0, utf8_range_end(path, 0, 128))))
                     .get<std::string>();
    if (shortened || path.size() > 128 || label.size() > 128) {
        label.resize(utf8_range_end(label, 0, 125));
        label += "...";
    }
    return label;
}
void push(const char*, std::size_t, const char* name) noexcept {
    if (!current) {
        worker_push(name);
        return;
    }
    if (current->depth == current->scopes.size()) {
        ++current->excess;
        ++current->dropped;
        return;
    }
    auto& entry = current->scopes[current->depth++];
    try {
        if (!name || !*name)
            return;
        const auto label = trace_label(name);
        entry.emplace(current->services.profile("ecs.system", label.c_str(), current->tick));
    } catch (...) {
        // Instrumentation never escapes the native C callback boundary.
    }
}
void pop(const char*, std::size_t, const char*) noexcept {
    if (!current) {
        worker_pop();
        return;
    }
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
    original_thread_new = api.thread_new_;
    if (original_thread_new)
        api.thread_new_ = trace_thread_new;
    ecs_os_set_api(&api);
    return native_system_tracing_available();
}
NativeWorkerTraceDomain::NativeWorkerTraceDomain(unsigned stages) {
#ifndef FORGE_DISABLE_PROFILING
    if (stages > 1 && stages <= runtime_ecs_worker_limit && worker_hooks_available())
        state_ = std::make_shared<NativeWorkerTraceState>(stages);
#else
    (void)stages;
#endif
}
NativeWorkerTraceDomain::~NativeWorkerTraceDomain() {
    if (state_)
        state_->active.store(0, std::memory_order_release);
}
bool NativeWorkerTraceDomain::available(ServiceAccess services) const noexcept {
    try {
        return state_ && state_->created == state_->count &&
               state_->serial != std::numeric_limits<std::uint64_t>::max() &&
               !state_->poisoned.load(std::memory_order_acquire) && worker_hooks_available() &&
               services.available(Capability::Profiling) &&
               services.native_profile_clock_compatible();
    } catch (...) {
        return false;
    }
}
std::unique_ptr<NativeWorkerTraceDomain> make_native_worker_trace_domain(unsigned stages) noexcept {
    try {
        if (stages > 1 && stages <= runtime_ecs_worker_limit && worker_hooks_available())
            return std::make_unique<NativeWorkerTraceDomain>(stages);
    } catch (...) {
        // Trace allocation is optional; the native execution owner still runs.
    }
    return {};
}
NativeWorkerTraceStartup::NativeWorkerTraceStartup(NativeWorkerTraceDomain* domain) noexcept
    : previous_(starting_workers) {
    starting_workers = domain && domain->state_ ? &domain->state_ : nullptr;
}
NativeWorkerTraceStartup::~NativeWorkerTraceStartup() { starting_workers = previous_; }
NativeSystemTraceCapture::NativeSystemTraceCapture(ServiceAccess services, std::uint64_t tick,
                                                   NativeWorkerTraceDomain* workers) noexcept
    : previous_(current), exceptions_(std::uncaught_exceptions()) {
    current = nullptr;
    try {
        if (native_system_tracing_available() && services.available(Capability::Profiling) &&
            services.profiling_enabled() && services.profiling_systems_enabled()) {
            state_ = std::make_unique<NativeSystemTraceState>(std::move(services), tick);
            current = state_.get();
            if (workers && workers->available(state_->services)) {
                const auto& domain = workers->state_;
                if (!domain->active.load(std::memory_order_acquire) &&
                    domain->serial != std::numeric_limits<std::uint64_t>::max()) {
                    for (unsigned i = 0; i < domain->count; ++i) {
                        auto& slot = domain->slots[i];
                        std::lock_guard guard(slot.mutex);
                        slot.count = 0;
                        slot.dropped.store(0, std::memory_order_relaxed);
                    }
                    state_->workers = domain;
                    state_->worker_capture = ++domain->serial;
                    domain->active.store(state_->worker_capture, std::memory_order_release);
                }
            }
        }
    } catch (...) {
    }
}
NativeSystemTraceCapture::~NativeSystemTraceCapture() {
    if (state_) {
        if (state_->workers) {
            const auto& domain = state_->workers;
            if (std::uncaught_exceptions() > exceptions_)
                domain->poisoned.store(true, std::memory_order_release);
            domain->active.store(0, std::memory_order_release);
            for (unsigned i = 0; i < domain->count; ++i) {
                auto& slot = domain->slots[i];
                try {
                    std::lock_guard guard(slot.mutex);
                    for (std::size_t j = 0; j < slot.count; ++j) {
                        const auto& record = slot.records[j];
                        if (record.capture == state_->worker_capture)
                            state_->services.record_native_profile_span(
                                state_->epoch, i + 1,
                                trace_label(record.name.data(), record.shortened), record.start,
                                record.seconds, state_->tick);
                    }
                    state_->services.record_system_profile_drops(
                        state_->epoch, slot.dropped.exchange(0, std::memory_order_relaxed));
                } catch (...) {
                    // Optional diagnostics never escape owner publication.
                }
            }
        }
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
