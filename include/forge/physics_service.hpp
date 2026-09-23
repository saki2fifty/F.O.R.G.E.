#pragma once
#include <forge/character_service.hpp>
#include <forge/identity.hpp>
#include <forge/runtime_resource_service.hpp>
#include <forge/transform.hpp>
#include <optional>
#include <vector>
namespace forge {
struct PhysicsQueryFilter {
    std::uint32_t mask = UINT32_MAX;
    bool include_sensors = true;
};
// Linear convex sweep. Dimensions: box full XYZ; sphere radius; capsule/cylinder
// radius and straight height. Local origin is the shape center; no angular sweep.
struct PhysicsSweep {
    enum class Shape : std::uint32_t { Box, Sphere, Capsule, Cylinder };
    Shape shape = Shape::Sphere;
    Double3 dimensions{.5, 0, 0};
    LocalTranslation origin;
    LocalRotation rotation;
    Double3 displacement{};
};
struct PhysicsHit {
    EntityRef entity;
    Double3 position{}, normal{};
    double fraction{};
};
struct PhysicsContact {
    EntityRef first, second;
    bool begin{};
    std::uint64_t tick{};
};
// Owner-thread only; valid while this Runtime world's physics module is alive.
class PhysicsService : public CharacterService {
  public:
    virtual ~PhysicsService() = default;
    virtual std::optional<PhysicsHit> raycast(Double3 origin, Double3 displacement) const = 0;
    virtual std::optional<PhysicsHit> raycast_filtered(Double3 origin, Double3 displacement,
                                                       PhysicsQueryFilter) const = 0;
    virtual std::optional<PhysicsHit> shape_cast(const PhysicsSweep&, PhysicsQueryFilter) const = 0;
    virtual std::uint64_t request_collision_asset(AssetId) = 0;
    virtual RuntimeResourceStatus inspect_collision_asset(std::uint64_t) const = 0;
    virtual bool release_collision_asset(std::uint64_t) = 0;
    virtual void teleport(EntityRef, LocalTranslation, LocalRotation, bool clear_velocity) = 0;
    virtual void move_kinematic(EntityRef, LocalTranslation, LocalRotation) = 0;
    virtual const std::vector<PhysicsContact>& contacts() const = 0;
};
} // namespace forge
