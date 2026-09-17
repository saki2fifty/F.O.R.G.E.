#pragma once
#include <cstdint>
#include <flecs.h>
#include <forge/identity.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace forge {
using Json = nlohmann::json;
struct Position {
    float x{}, y{}, z{};
    bool operator==(const Position&) const = default;
};
struct Rotation {
    float x{}, y{}, z{};
    bool operator==(const Rotation&) const = default;
}; // Degrees, local X then Y then Z.
struct Scale {
    float x = 1, y = 1, z = 1;
    bool operator==(const Scale&) const = default;
};
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
struct SceneMember {};
// Distinguish an authored prefab declaration from ChildOf's implicit Prefab tag.
struct AuthoredPrefab {};
enum class WorldRole { Authoring, Runtime, Preview, Validation };
class Scene;
class WorldContext {
  public:
    explicit WorldContext(WorldRole role = WorldRole::Authoring);
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

  private:
    friend class Scene;
    struct Content {
        std::map<std::string, flecs::entity_t> entities;
        std::map<EntityId, flecs::entity_t> persistent;
        AssetId asset;
        std::uint64_t serial = 0;
    };
    flecs::entity_t owner_of(flecs::entity entity) const;
    flecs::entity_t attach();
    void detach(flecs::entity_t root);
    WorldRole role_;
    std::map<flecs::entity_t, Content> content_;
    Json schema_;
    // Destroy the world before state used by its observers/hooks.
    flecs::world world_;
};
// Application composition root. Services/code owners are declared before this
// object by applications, so they outlive its world. No generic service locator.
class EngineContext {
  public:
    explicit EngineContext(WorldRole role = WorldRole::Authoring) : world_(role) {}
    WorldContext& world() { return world_; }

  private:
    WorldContext world_;
};
} // namespace forge
