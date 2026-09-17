#pragma once
#include <chrono>
#include <forge/identity.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace forge {
enum class Severity { Trace, Info, Warning, Error, Fatal };
struct DiagnosticContext {
    std::optional<EntityId> entity;
    std::optional<AssetId> asset;
    std::optional<PrefabMemberId> member;
    std::string source, property, session, module;
    std::optional<std::uint64_t> tick;
};
struct Diagnostic {
    Severity severity = Severity::Info;
    std::string category, text;
    DiagnosticContext context;
};
nlohmann::json diagnostic_json(const Diagnostic& diagnostic);
enum class Capability : unsigned { Diagnostics = 1, Profiling = 2 };
constexpr unsigned capability(Capability value) { return static_cast<unsigned>(value); }
struct ModuleRequirement {
    std::string id;
    std::vector<std::string> requires_modules;
    unsigned required_services = 0;
};
std::vector<std::string> module_order(const std::vector<ModuleRequirement>& modules,
                                      unsigned available);
namespace detail {
struct ServiceState;
}
class ProfileScope {
  public:
    ProfileScope() = default;
    ~ProfileScope();
    ProfileScope(ProfileScope&&) noexcept;
    ProfileScope& operator=(ProfileScope&&) = delete;
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

  private:
    friend class ServiceAccess;
    std::weak_ptr<detail::ServiceState> state_;
    std::string name_, category_;
    std::uint64_t tick_ = 0;
    double start_ = 0;
};
class ServiceAccess {
  public:
    bool available(Capability capability) const;
    void require(Capability capability) const;
    ServiceAccess restricted(unsigned allowed) const;
    void emit(Diagnostic diagnostic) const;
    std::vector<nlohmann::json> diagnostics() const;
    std::vector<nlohmann::json> profiles() const;
    ProfileScope profile(const char* category, const char* name, std::uint64_t tick = 0) const;
    void profiling(bool enabled) const;

  private:
    friend class EngineServices;
    std::weak_ptr<detail::ServiceState> state_;
    unsigned allowed_ = 0;
};
// Owned before worlds; access handles are weak and cannot extend owner lifetime.
// All access is on the construction thread. Worker results cross through callers' queues.
[[noreturn]] void fail_invariant(ServiceAccess services, const std::string& text);
class EngineServices {
  public:
    using Now = std::function<double()>;
    explicit EngineServices(bool profiling = false, Now now = {});
    ~EngineServices();
    EngineServices(const EngineServices&) = delete;
    EngineServices& operator=(const EngineServices&) = delete;
    ServiceAccess access(unsigned allowed = 3) const;

  private:
    std::shared_ptr<detail::ServiceState> state_;
};
} // namespace forge
