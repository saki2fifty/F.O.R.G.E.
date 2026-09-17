#include <algorithm>
#include <deque>
#include <forge/services.hpp>
#include <set>
#include <thread>
namespace forge {
using Json = nlohmann::json;
namespace detail {
struct ServiceState {
    std::thread::id owner = std::this_thread::get_id();
    bool alive = true, profiling = false;
    EngineServices::Now now;
    std::deque<Json> diagnostics, profiles;
    void check() const {
        if (!alive || owner != std::this_thread::get_id())
            throw std::runtime_error("Engine service access requires its live owner thread");
    }
};
} // namespace detail
Json diagnostic_json(const Diagnostic& d) {
    static const char* names[] = {"trace", "info", "warning", "error", "fatal"};
    const auto severity = static_cast<unsigned>(d.severity);
    if (severity >= 5 || d.category.empty() || d.text.empty())
        throw std::runtime_error("Invalid diagnostic severity/category/text");
    Json result{{"severity", names[severity]}, {"category", d.category}, {"text", d.text}};
    auto& context = result["context"] = Json::object();
    if (d.context.entity)
        context["entity"] = *d.context.entity;
    if (d.context.asset)
        context["asset"] = *d.context.asset;
    if (d.context.member)
        context["member"] = *d.context.member;
    if (d.context.related_entity)
        context["related_entity"] = *d.context.related_entity;
    if (d.context.tick)
        context["tick"] = *d.context.tick;
    if (!d.context.source.empty())
        context["source"] = d.context.source;
    if (!d.context.property.empty())
        context["property"] = d.context.property;
    if (!d.context.session.empty())
        context["session"] = d.context.session;
    if (!d.context.module.empty())
        context["module"] = d.context.module;
    if (!d.context.world_role.empty())
        result["context"]["world_role"] = d.context.world_role;
    if (!d.context.dependency.empty())
        result["context"]["dependency"] = d.context.dependency;
    return result;
}
[[noreturn]] void fail_invariant(ServiceAccess services, const std::string& text) {
    if (services.available(Capability::Diagnostics))
        services.emit({Severity::Fatal, "core", text, {}});
    throw std::logic_error(text); // Owner boundary must unwind; never treat as content validation.
}
EngineServices::EngineServices(bool profiling, Now now)
    : state_(std::make_shared<detail::ServiceState>()) {
    state_->profiling = profiling;
    state_->now = now ? std::move(now) : [] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
}
EngineServices::~EngineServices() { state_->alive = false; }
ServiceAccess EngineServices::access(unsigned allowed) const {
    ServiceAccess a;
    a.state_ = state_;
    a.allowed_ = allowed & 11;
    return a;
}
ServiceAccess ServiceAccess::world_scope() const {
    auto result = *this;
    result.physics_ = std::make_shared<detail::PhysicsSlot>();
    return result;
}
void ServiceAccess::publish_physics(const std::shared_ptr<PhysicsService>& service) const {
    auto state = state_.lock();
    if (!state || !(allowed_ & capability(Capability::Physics)))
        throw std::runtime_error("Physics publication requires the owner's allowed capability");
    state->check();
    if (!physics_)
        throw std::logic_error("Physics requires a scoped world service slot");
    if (!physics_->service.expired() && service)
        throw std::logic_error("Physics provider already installed");
    physics_->service = service;
}
std::shared_ptr<PhysicsService> ServiceAccess::physics() const {
    require(Capability::Physics);
    return physics_->service.lock();
}
bool ServiceAccess::available(Capability c) const {
    const auto state = state_.lock();
    return (allowed_ & capability(c)) && state && state->alive &&
           (c != Capability::Physics || (physics_ && !physics_->service.expired()));
}
void ServiceAccess::require(Capability c) const {
    if (!available(c))
        throw std::runtime_error("Required engine service is absent or its owner has expired");
    state_.lock()->check();
}
ServiceAccess ServiceAccess::restricted(unsigned allowed) const {
    auto result = *this;
    result.allowed_ &= allowed;
    return result;
}
void ServiceAccess::emit(Diagnostic d) const {
    require(Capability::Diagnostics);
    auto state = state_.lock();
    // Both count and individual payload size are bounded.
    d.text.resize(std::min<std::size_t>(d.text.size(), 8192));
    auto record = diagnostic_json(d);
    if (record.dump().size() > 16384)
        throw std::runtime_error("Diagnostic context exceeds 16 KiB");
    if (state->diagnostics.size() == 256)
        state->diagnostics.pop_front();
    state->diagnostics.push_back(std::move(record));
}
std::vector<Json> ServiceAccess::diagnostics() const {
    require(Capability::Diagnostics);
    const auto s = state_.lock();
    return {s->diagnostics.begin(), s->diagnostics.end()};
}
std::vector<Json> ServiceAccess::profiles() const {
    require(Capability::Profiling);
    const auto s = state_.lock();
    return {s->profiles.begin(), s->profiles.end()};
}
void ServiceAccess::profiling(bool enabled) const {
    require(Capability::Profiling);
    state_.lock()->profiling = enabled;
}
ProfileScope ServiceAccess::profile(const char* category, const char* name,
                                    std::uint64_t tick) const {
    ProfileScope scope;
#ifndef FORGE_DISABLE_PROFILING
    if (!available(Capability::Profiling))
        return scope;
    const auto state = state_.lock();
    state->check();
    if (!state->profiling)
        return scope;
    scope.state_ = state;
    scope.name_ = name;
    scope.category_ = category;
    scope.tick_ = tick;
    scope.start_ = state->now();
#else
    (void)category;
    (void)name;
    (void)tick;
#endif
    return scope;
}
ProfileScope::ProfileScope(ProfileScope&& o) noexcept
    : state_(std::move(o.state_)), name_(std::move(o.name_)), category_(std::move(o.category_)),
      tick_(o.tick_), start_(o.start_) {
    o.state_.reset();
}
ProfileScope::~ProfileScope() {
    const auto state = state_.lock();
    if (!state || !state->alive || !state->profiling || state->owner != std::this_thread::get_id())
        return;
    try {
        const double duration = std::max(0.0, state->now() - start_);
        if (state->profiles.size() == 512)
            state->profiles.pop_front();
        state->profiles.push_back({{"category", category_},
                                   {"name", name_},
                                   {"seconds", duration},
                                   {"count", 1},
                                   {"tick", tick_}});
    } catch (...) { /* Instrumentation must not alter engine control flow. */
    }
}
std::vector<std::string> module_order(const std::vector<ModuleRequirement>& modules,
                                      unsigned available) {
    std::map<std::string, const ModuleRequirement*> entries;
    for (const auto& m : modules) {
        if (m.id.empty() || !entries.emplace(m.id, &m).second)
            throw std::runtime_error("Duplicate/empty built-in module identity");
        if ((m.required_services & available) != m.required_services)
            throw std::runtime_error("Missing required services for module: " + m.id);
    }
    std::set<std::string> active, done;
    std::vector<std::string> order;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (done.contains(id))
            return;
        if (!entries.contains(id))
            throw std::runtime_error("Missing required built-in module: " + id);
        if (!active.insert(id).second)
            throw std::runtime_error("Built-in module dependency cycle: " + id);
        for (const auto& dep : entries.at(id)->requires_modules)
            visit(dep);
        active.erase(id);
        done.insert(id);
        order.push_back(id);
    };
    for (const auto& [id, m] : entries) {
        (void)m;
        visit(id);
    }
    return order;
}
} // namespace forge
