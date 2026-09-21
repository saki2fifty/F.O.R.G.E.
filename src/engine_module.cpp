#include <algorithm>
#include <forge/engine_module.hpp>
#include <map>
namespace forge {
bool valid_module_id(const std::string& id) {
    if (id.empty() || id.size() > 128 || id.front() == '.' || id.back() == '.' ||
        id.find('.') == std::string::npos || id.find("..") != std::string::npos)
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    });
}
std::string canonical_module_id(const std::string& id) {
    if (id == "core" || id == "transforms" || id == "input" || id == "prefabs")
        return "forge." + id;
    return id;
}
ModuleLifecycle::~ModuleLifecycle() {
    // Owner destroys Flecs first. Release contexts/callback closures/code in reverse order.
    while (!entries_.empty())
        entries_.pop_back();
}
void ModuleLifecycle::bootstrap(flecs::world& world, WorldRole role, ServiceAccess services,
                                std::vector<EngineModule> modules, WorldContext* owner) {
    if (bootstrapped_)
        throw std::logic_error("Module bootstrap is once per WorldContext");
    bootstrapped_ = true;
    std::string current, dependency;
    try {
        unsigned available = 0;
        for (auto c : {Capability::Diagnostics, Capability::Profiling})
            if (services.available(c))
                available |= capability(c);
        std::map<std::string, EngineModule> selected;
        std::vector<ModuleRequirement> requirements;
        for (auto& m : modules) {
            current = m.id;
            if (!valid_module_id(m.id) || m.implementation.empty() || !m.schema_roles ||
                (m.schema_roles & ~all_world_roles) || (m.runtime_roles & ~m.schema_roles) ||
                (m.required_services & ~m.allowed_services) ||
                (m.allowed_services & ~known_capabilities) ||
                (m.provided_services & ~subsystem_capabilities))
                throw std::runtime_error("Invalid module descriptor");
            if (selected.contains(m.id))
                throw std::runtime_error("Duplicate module ID");
            selected.emplace(m.id, std::move(m));
        }
        for (const auto& [id, m] : selected)
            if (m.schema_roles & role_mask(role)) {
                current = id;
                for (const auto& dep : m.dependencies)
                    if (!selected.contains(dep) ||
                        !(selected.at(dep).schema_roles & role_mask(role))) {
                        dependency = dep;
                        throw std::runtime_error("Missing/role-excluded module dependency: " + dep);
                    }
                requirements.push_back({id, m.dependencies, m.required_services});
            }
        for (const auto& [id, m] : selected) {
            if ((m.runtime_roles & role_mask(role)) && m.provided_services) {
                if (available & m.provided_services)
                    throw std::runtime_error("Duplicate service provider");
                available |= m.provided_services;
                for (const auto& [consumer, n] : selected)
                    if (consumer != id && (n.schema_roles & role_mask(role)) &&
                        (n.required_services & m.provided_services) &&
                        std::find(n.dependencies.begin(), n.dependencies.end(), id) ==
                            n.dependencies.end())
                        throw std::runtime_error(
                            "Required runtime capability needs an explicit provider dependency");
            }
        }
        order_ = module_order(requirements, available);
        entries_.reserve(order_.size());
        // Retain every participating provider before executing any registration.
        for (const auto& id : order_) {
            auto m = std::move(selected.at(id));
            auto context = std::make_unique<ModuleContext>(ModuleContext{
                world, role, services.restricted(m.allowed_services), id, nullptr, owner});
            entries_.push_back({std::move(m), std::move(context), false});
        }
        for (auto& entry : entries_) {
            current = entry.module.id;
            auto scope = services.profile("module", "Registration");
            if (entry.module.schemas)
                entry.module.schemas(*entry.context);
        }
        for (auto& entry : entries_)
            if (entry.module.runtime_roles & role_mask(role)) {
                current = entry.module.id;
                for (auto cap : {Capability::Diagnostics, Capability::Profiling,
                                 Capability::Rendering, Capability::Physics, Capability::Audio,
                                 Capability::Navigation, Capability::Ui, Capability::Resources})
                    if (entry.module.required_services & capability(cap))
                        entry.context->services.require(cap);
                entry.started = true; // Stop must handle partial startup.
                auto scope = services.profile("module", "Startup");
                if (entry.module.start)
                    entry.module.start(*entry.context);
            }
    } catch (const std::exception& e) {
        stop();
        try {
            if (services.available(Capability::Diagnostics)) {
                Diagnostic d{Severity::Error, "module", e.what(), {}};
                d.context.module = current;
                d.context.world_role = world_role_name(role);
                d.context.dependency = dependency;
                services.emit(std::move(d));
            }
        } catch (...) { /* Preserve the original bootstrap failure. */
        }
        throw; // Context construction must fail; no native registration rollback claim.
    } catch (...) {
        stop();
        throw;
    }
}
void ModuleLifecycle::stop() noexcept {
    end_tick();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->started) {
            it->started = false;
            try {
                auto scope = it->context->services.profile("module", "Shutdown");
                if (it->module.stop)
                    it->module.stop(*it->context);
            } catch (...) {
                // Shutdown hooks are required not to throw. Continuing could invalidate callbacks.
                std::terminate();
            }
        }
}
void ModuleLifecycle::begin_tick(const InputSnapshot& input) {
    for (auto& e : entries_)
        e.context->input = &input;
}
void ModuleLifecycle::end_tick() noexcept {
    for (auto& e : entries_)
        e.context->input = nullptr;
}
} // namespace forge
