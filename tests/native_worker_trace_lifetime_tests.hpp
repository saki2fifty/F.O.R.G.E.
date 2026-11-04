#pragma once
#include "flecs_system_trace.hpp"
#include <atomic>
#include <chrono>
#include <flecs.h>
#include <semaphore>

namespace forge::test {
template <class Check> void worker_trace_unwind(Check check) {
    struct NativeOsLifetime {
        NativeOsLifetime() { ecs_os_init(); }
        ~NativeOsLifetime() { ecs_os_fini(); }
    } os;
    EngineServices owner(true);
    const auto services = owner.access();
    services.profiling_systems(true);
    detail::NativeWorkerTraceDomain domain(2);
    struct Gates {
        std::counting_semaphore<4> start{0}, entered{0}, finish{0};
        std::atomic<bool> retired{false};
    } gates;
    const auto callback = +[](void* raw) -> void* {
        auto& gates = *static_cast<Gates*>(raw);
        gates.start.acquire();
        ecs_os_api.perf_trace_push_("worker unwind", 1, "Held worker dispatch");
        gates.entered.release();
        gates.finish.acquire();
        ecs_os_api.perf_trace_pop_("worker unwind", 1, "Held worker dispatch");
        gates.retired = true;
        return nullptr;
    };
    ecs_os_thread_t thread = 0;
    {
        detail::NativeWorkerTraceStartup startup(&domain);
        thread = ecs_os_thread_new(callback, &gates);
    }
    struct Join {
        Gates& gates;
        ecs_os_thread_t handle;
        ~Join() {
            if (handle) {
                gates.start.release();
                gates.finish.release();
                ecs_os_thread_join(handle);
            }
        }
    } join{gates, thread};
    check(thread != 0 && domain.available(services),
          "Native unwind fixture could not bind its stage");
    struct ExpectedUnwind {};
    try {
        detail::NativeSystemTraceCapture capture(services, 99, &domain);
        gates.start.release();
        check(gates.entered.try_acquire_for(std::chrono::seconds(5)),
              "Native worker did not enter its measured dispatch");
        throw ExpectedUnwind{};
    } catch (const ExpectedUnwind&) {
    }
    check(!domain.available(services), "Unwinding capture retained a reusable worker trace domain");
    gates.finish.release();
    ecs_os_thread_join(thread);
    join.handle = 0;
    check(gates.retired && services.profiles().empty(),
          "Incomplete worker dispatch was published during owner unwind");
    {
        detail::NativeSystemTraceCapture capture(services, 100, &domain);
        ecs_os_api.perf_trace_push_("owner", 1, "Owner remains available");
        ecs_os_api.perf_trace_pop_("owner", 1, "Owner remains available");
    }
    check(services.profiles().size() == 1 && !services.profiles()[0].contains("native_stage"),
          "Poisoned worker capture disabled or contaminated owner-only fallback");
}
} // namespace forge::test
