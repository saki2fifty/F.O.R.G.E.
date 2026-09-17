#pragma once
#include <cstdint>
#include <flecs.h>
#include <forge/engine_module.hpp>
#include <forge/identity.hpp>
#include <forge/services.hpp>
#include <forge/transform.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace forge {
using Json = nlohmann::json;
struct Tint {
    float r = 0.2f, g = 0.6f, b = 0.7f;
    bool operator==(const Tint&) const = default;
};
struct Primitive {
    std::uint32_t kind = 0;
    bool operator==(const Primitive&) const = default;
}; // Cube, sphere, cylinder, plane.
struct StableId {
    std::string value;
};

// Display names need not be unique Flecs symbol names.
struct AuthoredName {
    std::string value;
};
// Definition identity is inherited for lookup, never used as an instance EntityId.
struct TemplateMember {
    PrefabMemberId id;
};
struct SceneMember {};
// Distinguish an authored prefab declaration from ChildOf's implicit Prefab tag.
struct AuthoredPrefab {};
// Derived availability marker, never authored or inherited.
struct MissingStructuralParent {};
EngineModule physics_schema_module();
EngineModule audio_schema_module();
EngineModule animation_schema_module();
class Scene;
class WorldContext {
  public:
    explicit WorldContext(WorldRole role = WorldRole::Authoring, ServiceAccess services = {},
                          std::vector<EngineModule> modules = {});
    ServiceAccess services() const { return services_; }
    ModuleLifecycle& modules() { return modules_; }
    ~WorldContext();
    WorldContext(const WorldContext&) = delete;
    WorldContext& operator=(const WorldContext&) = delete;
    flecs::world& world() { return world_; }
    WorldRole role() const { return role_; }
    const Json& schema() const { return schema_; }
    enum class ResolveState { Available, Missing, Unresolved, Ambiguous };
    struct Resolution {
        ResolveState state;
        flecs::entity_t entity = 0;
    };
    Resolution resolve(EntityRef ref, flecs::entity_t membership = 0) const;
    std::optional<EntityRef> reference(flecs::entity_t entity) const;
    LocalTransform get_local_transform(flecs::entity entity) const;
    void evaluate_world_transforms();
    // Effective local poses and spatial graph for derived runtime presentation.
    std::map<std::uint64_t, TransformNode> transform_nodes() const { return collect_transforms(); }

  private:
    friend class Scene;
    struct Content {
        std::map<std::string, flecs::entity_t> entities;
        std::map<EntityId, flecs::entity_t> persistent;
        AssetId asset;
        std::uint64_t serial = 0;
    };
    std::map<std::uint64_t, TransformNode> collect_transforms() const;
    void translate_content(flecs::entity_t membership, Double3 delta);
    flecs::entity_t owner_of(flecs::entity entity) const;
    flecs::entity_t attach();
    void detach(flecs::entity_t root);
    TransformEvaluator transform_evaluator_;
    std::uint64_t transform_epoch_ = 1, evaluated_epoch_ = 0;
    bool evaluating_transforms_ = false;
    ServiceAccess services_;
    WorldRole role_;
    std::map<flecs::entity_t, Content> content_;
    Json schema_;
    // Destroy the world before state used by its observers/hooks.
    ModuleLifecycle modules_; // Code/providers must outlive world finalization.
    flecs::world world_;
};
// Application composition root. Services/code owners are declared before this
// object by applications, so they outlive its world. No generic service locator.
class EngineContext {
  public:
    explicit EngineContext(WorldRole role = WorldRole::Authoring, bool profiling = false,
                           std::vector<EngineModule> modules = {})
        : services_(profiling), world_(role, services_.access(), std::move(modules)) {}
    ServiceAccess services() const { return world_.services(); }
    WorldContext& world() { return world_; }

  private:
    EngineServices services_;
    WorldContext world_;
};
} // namespace forge
