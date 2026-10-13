#include <algorithm>
#include <cmath>
#include <deque>
#include <forge/services.hpp>
#include <limits>
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
    std::uint64_t diagnostic_sequence = 0;
    std::uint64_t profile_generation = 0, profile_sequence = 0;
    void append_profile(const std::string& category, const std::string& name, double seconds,
                        std::uint64_t tick) {
        if (profiles.size() == 512)
            profiles.pop_front();
        profiles.push_back({{"category", category},
                            {"name", name},
                            {"seconds", seconds},
                            {"count", 1},
                            {"tick", tick},
                            {"sequence", ++profile_sequence}});
    }
    void check() const {
        if (owner != std::this_thread::get_id() || !alive)
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
    a.allowed_ = allowed & supplied_capabilities;
    return a;
}
ServiceAccess ServiceAccess::world_scope() const {
    auto result = *this;
    result.animation_ = std::make_shared<detail::ServiceSlot<AnimationService>>();
    result.physics_ = std::make_shared<detail::ServiceSlot<PhysicsService>>();
    result.audio_ = std::make_shared<detail::ServiceSlot<AudioService>>();
    result.navigation_ = std::make_shared<detail::ServiceSlot<NavigationService>>();
    result.ui_ = std::make_shared<detail::ServiceSlot<UiService>>();
    result.resources_ = std::make_shared<detail::ServiceSlot<RuntimeResourceService>>();
    result.game_ = std::make_shared<detail::ServiceSlot<GameControlService>>();
    return result;
}
template <class T>
void ServiceAccess::publish(Capability cap, const std::shared_ptr<detail::ServiceSlot<T>>& slot,
                            const std::shared_ptr<T>& service) const {
    auto state = state_.lock();
    if (!state || !(allowed_ & capability(cap)))
        throw std::runtime_error("Service publication requires live owner capability");
    state->check();
    if (!slot)
        throw std::logic_error("Service publication requires a world-scoped slot");
    if (!slot->service.expired() && service)
        throw std::logic_error("Service provider already installed");
    slot->service = service;
}
void ServiceAccess::publish_animation(const std::shared_ptr<AnimationService>& service) const {
    publish(Capability::Animation, animation_, service);
}
std::shared_ptr<AnimationService> ServiceAccess::animation() const {
    require(Capability::Animation);
    return animation_->service.lock();
}
void ServiceAccess::publish_physics(const std::shared_ptr<PhysicsService>& service) const {
    publish(Capability::Physics, physics_, service);
}
std::shared_ptr<PhysicsService> ServiceAccess::physics() const {
    require(Capability::Physics);
    return physics_->service.lock();
}
void ServiceAccess::publish_audio(const std::shared_ptr<AudioService>& service) const {
    publish(Capability::Audio, audio_, service);
}
std::shared_ptr<AudioService> ServiceAccess::audio() const {
    require(Capability::Audio);
    return audio_->service.lock();
}
void ServiceAccess::publish_navigation(const std::shared_ptr<NavigationService>& service) const {
    publish(Capability::Navigation, navigation_, service);
}
std::shared_ptr<NavigationService> ServiceAccess::navigation() const {
    require(Capability::Navigation);
    return navigation_->service.lock();
}
void ServiceAccess::publish_ui(const std::shared_ptr<UiService>& service) const {
    publish(Capability::Ui, ui_, service);
}
std::shared_ptr<UiService> ServiceAccess::ui() const {
    require(Capability::Ui);
    return ui_->service.lock();
}
void ServiceAccess::publish_resources(const std::shared_ptr<RuntimeResourceService>& value) const {
    publish(Capability::Resources, resources_, value);
}
void ServiceAccess::publish_game(const std::shared_ptr<GameControlService>& value) const {
    publish(Capability::Game, game_, value);
}
std::shared_ptr<GameControlService> ServiceAccess::game() const {
    require(Capability::Game);
    return game_->service.lock();
}
std::shared_ptr<RuntimeResourceService> ServiceAccess::resources() const {
    require(Capability::Resources);
    return resources_->service.lock();
}
bool ServiceAccess::available(Capability c) const {
    const auto state = state_.lock();
    const auto bit = capability(c);
    return bit && !(bit & (bit - 1)) && !(bit & ~known_capabilities) && (allowed_ & bit) && state &&
           state->owner == std::this_thread::get_id() && state->alive &&
           (c != Capability::Animation || (animation_ && !animation_->service.expired())) &&
           (c != Capability::Physics || (physics_ && !physics_->service.expired())) &&
           (c != Capability::Audio || (audio_ && !audio_->service.expired())) &&
           (c != Capability::Navigation || (navigation_ && !navigation_->service.expired())) &&
           (c != Capability::Ui || (ui_ && !ui_->service.expired())) &&
           (c != Capability::Resources || (resources_ && !resources_->service.expired())) &&
           (c != Capability::Game || (game_ && !game_->service.expired()));
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
    if (state->diagnostic_sequence == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Diagnostic sequence exhausted");
    record["sequence"] = ++state->diagnostic_sequence;
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
Json ServiceAccess::profile_status() const {
    require(Capability::Profiling);
    const auto state = state_.lock();
#ifdef FORGE_DISABLE_PROFILING
    constexpr bool compiled = false;
#else
    constexpr bool compiled = true;
#endif
    return {{"compiled", compiled},
            {"enabled", compiled && state->profiling},
            {"capacity", 512},
            {"sequence", state->profile_sequence},
            {"samples", state->profiles}};
}
bool ServiceAccess::profiling_enabled() const {
    require(Capability::Profiling);
#ifdef FORGE_DISABLE_PROFILING
    return false;
#else
    return state_.lock()->profiling;
#endif
}
void ServiceAccess::clear_profiles() const {
    require(Capability::Profiling);
    const auto state = state_.lock();
    state->profiles.clear();
    ++state->profile_generation;
}
void ServiceAccess::profiling(bool enabled) const {
    require(Capability::Profiling);
    const auto state = state_.lock();
    if (state->profiling != enabled) {
        state->profiling = enabled;
        ++state->profile_generation;
    }
}
void ServiceAccess::record_profile(const std::string& category, const std::string& name,
                                   double seconds, std::uint64_t tick) const {
    require(Capability::Profiling);
    if (category.empty() || category.size() > 128 || name.empty() || name.size() > 128 ||
        !std::isfinite(seconds) || seconds < 0 || seconds > 3600)
        throw std::runtime_error("Invalid CPU profile sample");
#ifndef FORGE_DISABLE_PROFILING
    auto state = state_.lock();
    if (!state->profiling)
        return;
    state->append_profile(category, name, seconds, tick);
#endif
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
    if (!category || !name || !*category || !*name ||
        std::char_traits<char>::length(category) > 128 ||
        std::char_traits<char>::length(name) > 128)
        return scope;
    try {
        scope.start_ = state->now();
        if (!std::isfinite(scope.start_))
            return scope;
        scope.name_ = name;
        scope.category_ = category;
        scope.tick_ = tick;
        scope.generation_ = state->profile_generation;
        scope.state_ = state;
    } catch (...) {
        // A diagnostic clock or instrumentation allocation must not change engine behavior.
    }
#else
    (void)category;
    (void)name;
    (void)tick;
#endif
    return scope;
}
ProfileScope::ProfileScope(ProfileScope&& o) noexcept
    : state_(std::move(o.state_)), name_(std::move(o.name_)), category_(std::move(o.category_)),
      tick_(o.tick_), generation_(o.generation_), start_(o.start_) {
    o.state_.reset();
}
ProfileScope::~ProfileScope() {
    const auto state = state_.lock();
    if (!state || !state->alive || !state->profiling ||
        state->owner != std::this_thread::get_id() || state->profile_generation != generation_)
        return;
    try {
        const double duration = state->now() - start_;
        if (!std::isfinite(duration) || duration < 0 || duration > 3600)
            return;
        state->append_profile(category_, name_, duration, tick_);
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
