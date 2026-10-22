#pragma once
#include <cstddef>
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
// Instantaneous native volume query; geometry conventions match PhysicsSweep.
struct PhysicsOverlap {
    using Shape = PhysicsSweep::Shape;
    Shape shape = Shape::Sphere;
    Double3 dimensions{.5, 0, 0};
    LocalTranslation origin;
    LocalRotation rotation;
};
inline constexpr std::size_t physics_overlap_limit = 256;
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
// Copied XY-domain state. Positions are world metres; angular values use radians.
// Epoch identifies this simulation realization, not a durable source identity.
struct PhysicsBodyState2D {
    std::uint32_t motion{};
    bool active{};
    std::uint64_t epoch{};
    Double2 position{}, centre_of_mass{}, linear_velocity{};
    double angle{}, angular_velocity{}, mass{}, rotational_inertia{};
    bool operator==(const PhysicsBodyState2D&) const = default;
};
struct PhysicsHit2D {
    EntityRef entity;
    Double2 position{}, normal{};
    double fraction{};
};
struct PhysicsOverlap2D {
    enum class Shape : std::uint32_t { Box, Circle, Capsule };
    Shape shape = Shape::Circle;
    // Full XY box size; circle radius in X; capsule radius/X, straight height/Y.
    Double2 dimensions{.5, 0}, origin{};
    double angle{}; // Radians about +Z.
};
// Linear XY sweep; centered geometry matches PhysicsOverlap2D. No angular sweep
// or depenetration. A native initial overlap reports fraction0 and zero normal.
struct PhysicsSweep2D {
    using Shape = PhysicsOverlap2D::Shape;
    Shape shape = Shape::Circle;
    Double2 dimensions{.5, 0}, origin{}, displacement{};
    double angle{};
    std::optional<EntityRef> ignore;     // Exclude all shapes of this canonical body.
    bool ignore_initial_overlap = false; // Skip native fraction0 hits, including touching.
};
struct PhysicsContact2D {
    EntityRef first, second;
    std::uint64_t first_epoch{}, second_epoch{}, tick{};
    bool begin{}, sensor{}, first_available{}, second_available{};
    bool operator==(const PhysicsContact2D&) const = default;
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
    // Copied world velocity commands (m/s and rad/s), preserving the other
    // channel. Zero stops motion; native limits and activation remain authoritative.
    virtual void set_linear_velocity(EntityRef, Double3) = 0;
    virtual void set_angular_velocity(EntityRef, Double3) = 0;
    // Reads do not flush pending commands or wake bodies. Unavailable/unrealized
    // identities return no value; configuration changes adopt at synchronization.
    virtual std::optional<PhysicsBodyState> body_state(EntityRef) const = 0;
    virtual std::optional<Double3> point_velocity(EntityRef,
                                                  LocalTranslation world_point) const = 0;
    // Adopted observations expire at the next admitted synchronization/adoption.
    virtual const std::vector<PhysicsContact>& contacts() const = 0;
    // Copied slot from the same adopted list. Current realized body/character
    // generations must still match both canonical references. No consumption.
    virtual std::optional<PhysicsContact> contact_event(std::size_t index) const = 0;
    // Copied unique current realized references, sorted by canonical identity.
    // Overflow rejects, never truncates. Surface meshes retain native semantics.
    virtual std::vector<EntityRef> overlap_shape(const PhysicsOverlap&,
                                                 PhysicsQueryFilter) const = 0;
    // Distinct XY-domain controls, sharing the existing body-command FIFO.
    // Target positions preserve authored Z/scale; angles are radians about +Z.
    virtual void teleport_2d(EntityRef, Double2, double angle, bool clear_velocity) = 0;
    virtual void move_kinematic_2d(EntityRef, Double2, double angle) = 0;
    virtual void add_force_2d(EntityRef, Double2, std::optional<Double2> world_point = {}) = 0;
    virtual void add_impulse_2d(EntityRef, Double2, std::optional<Double2> world_point = {}) = 0;
    virtual void add_torque_2d(EntityRef, double) = 0;
    virtual void add_angular_impulse_2d(EntityRef, double) = 0;
    virtual void set_linear_velocity_2d(EntityRef, Double2) = 0;
    virtual void set_angular_velocity_2d(EntityRef, double) = 0;
    virtual std::optional<PhysicsBodyState2D> body_state_2d(EntityRef) const = 0;
    virtual std::optional<Double2> point_velocity_2d(EntityRef, Double2 world_point) const = 0;
    virtual std::optional<PhysicsHit2D> raycast_2d(Double2 origin, Double2 displacement,
                                                   PhysicsQueryFilter = {}) const = 0;
    virtual std::vector<EntityRef> overlap_shape_2d(const PhysicsOverlap2D&,
                                                    PhysicsQueryFilter = {}) const = 0;
    virtual std::optional<PhysicsHit2D> shape_cast_2d(const PhysicsSweep2D&,
                                                      PhysicsQueryFilter = {}) const = 0;
    // Stable copied slots for this tick. Sensor first/visitor second; solid pairs
    // use canonical ordering. Availability checks the current epoch on each read.
    virtual std::size_t contact_count_2d() const = 0;
    virtual std::optional<PhysicsContact2D> contact_event_2d(std::size_t) const = 0;
};
} // namespace forge
