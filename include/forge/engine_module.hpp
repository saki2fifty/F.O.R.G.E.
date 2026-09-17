#pragma once
#include <flecs.h>
#include <forge/input.hpp>
#include <forge/services.hpp>
#include <functional>
#include <memory>
namespace forge {
enum class WorldRole { Authoring, Runtime, Preview, Validation };
constexpr unsigned role_mask(WorldRole role) { return 1u << static_cast<unsigned>(role); }
constexpr unsigned all_world_roles = 15;
inline const char* world_role_name(WorldRole role) {
    switch (role) {
    case WorldRole::Authoring:
        return "authoring";
    case WorldRole::Runtime:
        return "runtime";
    case WorldRole::Preview:
        return "preview";
    case WorldRole::Validation:
        return "validation";
    }
    return "invalid";
}
struct FixedSimulation {};
class WorldContext;
struct ModuleContext {
    flecs::world& world;
    WorldRole role;
    ServiceAccess services;
    std::string id;
    const InputSnapshot* input = nullptr; // Borrowed during a fixed tick only.
    WorldContext* owner = nullptr;
    std::shared_ptr<void> state; // Host bridge, retained until after Flecs finalization.
};
// Internal/source contract. Flecs owns ECS registrations; FORGE owns policy and lifetime.
struct EngineModule {
    std::string id, implementation = "1";
    std::vector<std::string> dependencies;
    unsigned schema_roles = all_world_roles, runtime_roles = 0;
    unsigned required_services = 0, allowed_services = 0, provided_services = 0;
    std::shared_ptr<void> code;
    std::function<void(ModuleContext&)> schemas, start, stop;
};
class ModuleLifecycle {
  public:
    ~ModuleLifecycle();
    void bootstrap(flecs::world&, WorldRole, ServiceAccess, std::vector<EngineModule>,
                   WorldContext* owner = nullptr);
    void stop() noexcept;
    void begin_tick(const InputSnapshot&);
    void end_tick() noexcept;
    const std::vector<std::string>& order() const { return order_; }

  private:
    struct Entry {
        EngineModule module;
        std::unique_ptr<ModuleContext> context;
        bool started = false;
    };
    std::vector<Entry> entries_;
    std::vector<std::string> order_;
    bool bootstrapped_ = false;
};
bool valid_module_id(const std::string&);
std::string canonical_module_id(const std::string&); // Phase5.5 aliases preserved.
} // namespace forge
