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
// Copied realized solver state. Linear velocity is at the native center of mass;
// angular velocity is a world vector in radians/s, not authored degrees/s.
struct PhysicsBodyState {
    std::uint32_t motion{}; // Static, Kinematic, Dynamic.
    bool active{};          // An inactive Static body is not a sleeping Dynamic.
    Double3 centre_of_mass{}, linear_velocity{}, angular_velocity{};
    bool operator==(const PhysicsBodyState&) const = default;
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
    // World-space loads, queued in body-command order for one positive fixed tick.
    // Force/torque: N / N*m. Impulse/angular impulse: N*s / N*m*s.
    // Optional point is an absolute world position; omission means center of mass.
    virtual void add_force(EntityRef, Double3,
                           std::optional<LocalTranslation> world_point = {}) = 0;
    virtual void add_impulse(EntityRef, Double3,
                             std::optional<LocalTranslation> world_point = {}) = 0;
    virtual void add_torque(EntityRef, Double3) = 0;
    virtual void add_angular_impulse(EntityRef, Double3) = 0;
    // Reads do not flush pending commands or wake bodies. Unavailable/unrealized
    // identities return no value; configuration changes adopt at synchronization.
    virtual std::optional<PhysicsBodyState> body_state(EntityRef) const = 0;
    virtual std::optional<Double3> point_velocity(EntityRef,
                                                  LocalTranslation world_point) const = 0;
    virtual const std::vector<PhysicsContact>& contacts() const = 0;
};
} // namespace forge
