#pragma once
#include <forge/services.hpp>
#include <memory>
namespace forge::detail {
// Compose this host's native callbacks before worlds/threads exist. An external
// OS API remains authoritative; unsupported configurations are not overwritten.
bool initialize_native_system_tracing() noexcept;
bool native_system_tracing_available() noexcept;
struct NativeWorkerTraceState;
class NativeWorkerTraceStartup;
class NativeWorkerTraceDomain {
  public:
    explicit NativeWorkerTraceDomain(unsigned stages);
    ~NativeWorkerTraceDomain();
    NativeWorkerTraceDomain(const NativeWorkerTraceDomain&) = delete;
    NativeWorkerTraceDomain& operator=(const NativeWorkerTraceDomain&) = delete;
    bool available(ServiceAccess) const noexcept;

  private:
    friend class NativeWorkerTraceStartup;
    friend class NativeSystemTraceCapture;
    std::shared_ptr<NativeWorkerTraceState> state_;
};
std::unique_ptr<NativeWorkerTraceDomain> make_native_worker_trace_domain(unsigned stages) noexcept;
// Borrowed only while the native owner creates its explicit long-running stages.
// Thread creation/handles/join remain delegated to the selected native OS API.
class NativeWorkerTraceStartup {
  public:
    explicit NativeWorkerTraceStartup(NativeWorkerTraceDomain*) noexcept;
    ~NativeWorkerTraceStartup();
    NativeWorkerTraceStartup(const NativeWorkerTraceStartup&) = delete;
    NativeWorkerTraceStartup& operator=(const NativeWorkerTraceStartup&) = delete;

  private:
    const std::shared_ptr<NativeWorkerTraceState>* previous_ = nullptr;
};
struct NativeSystemTraceState;
class NativeSystemTraceCapture {
  public:
    NativeSystemTraceCapture(ServiceAccess services, std::uint64_t tick,
                             NativeWorkerTraceDomain* workers = nullptr) noexcept;
    ~NativeSystemTraceCapture();
    NativeSystemTraceCapture(const NativeSystemTraceCapture&) = delete;
    NativeSystemTraceCapture& operator=(const NativeSystemTraceCapture&) = delete;

  private:
    std::unique_ptr<NativeSystemTraceState> state_;
    NativeSystemTraceState* previous_ = nullptr;
    int exceptions_ = 0;
};
} // namespace forge::detail
