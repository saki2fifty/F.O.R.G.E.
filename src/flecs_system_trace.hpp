#pragma once
#include <forge/services.hpp>
#include <memory>
namespace forge::detail {
// Compose this host's native callbacks before worlds/threads exist. An external
// OS API remains authoritative; unsupported configurations are not overwritten.
bool initialize_native_system_tracing() noexcept;
bool native_system_tracing_available() noexcept;
struct NativeSystemTraceState;
class NativeSystemTraceCapture {
  public:
    NativeSystemTraceCapture(ServiceAccess services, std::uint64_t tick) noexcept;
    ~NativeSystemTraceCapture();
    NativeSystemTraceCapture(const NativeSystemTraceCapture&) = delete;
    NativeSystemTraceCapture& operator=(const NativeSystemTraceCapture&) = delete;

  private:
    std::unique_ptr<NativeSystemTraceState> state_;
    NativeSystemTraceState* previous_ = nullptr;
};
} // namespace forge::detail
