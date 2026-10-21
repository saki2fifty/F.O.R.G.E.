#pragma once
#include "physics_command.hpp"
#include <forge/physics_components.hpp>
#include <forge/world.hpp>
#include <map>
#include <memory>
#include <set>

namespace forge::physics_detail {
struct Body2DConfiguration {
    PhysicsBody2D body;
    unsigned shape = 0;                // Box, circle, capsule.
    std::array<float, 2> dimensions{}; // Effective world geometry, metres.
    bool operator==(const Body2DConfiguration&) const = default;
    Json json() const;
};
struct Body2DDesired {
    EntityRef ref;
    Body2DConfiguration config;
    LocalTransform pose;
};
using Desired2D = std::map<std::uint64_t, Body2DDesired>;
struct Native2DImage {
    Json metadata;
    std::string bytes;
};
// Reads the existing effective graph and schema. No native creation or ECS writes.
Desired2D desired_bodies_2d(WorldContext&, const PhysicsConfig&,
                            const std::map<std::uint64_t, TransformNode>&);
void validate_ray_2d(Double2, Double2);
void validate_overlap_2d(const PhysicsOverlap2D&);

// Subordinate native state of PhysicsRuntime, never another service/world owner.
// No native identifier or header crosses this private boundary.
class Native2D {
  public:
    explicit Native2D(const PhysicsConfig&);
    ~Native2D();
    void check() const;
    void validate(const Desired2D&, float dt = 0, const std::set<std::uint64_t>& commanded = {},
                  const std::map<std::uint64_t, LocalTransform>& placements = {}) const;
    void synchronize(const Desired2D&, const Desired2D& initial, float,
                     const std::set<std::uint64_t>& commanded,
                     std::vector<std::uint64_t>& discontinuities);
    void apply(std::uint64_t, const BodyCommand&, std::vector<std::uint64_t>& discontinuities);
    void finish_targets(float);
    std::optional<PhysicsBodyState2D> state(std::uint64_t, EntityRef) const;
    std::optional<Double2> point_velocity(std::uint64_t, EntityRef, Double2) const;
    std::optional<PhysicsHit2D> raycast(Double2, Double2, PhysicsQueryFilter) const;
    std::vector<EntityRef> overlap(const PhysicsOverlap2D&, PhysicsQueryFilter) const;
    void expire_events();
    void adopt_events(std::uint64_t tick);
    std::size_t event_count() const;
    std::optional<PhysicsContact2D> event(std::size_t) const;
    bool current(std::uint64_t, EntityRef, std::uint64_t epoch) const;
    bool contains(std::uint64_t, EntityRef) const;
    void validate_step() const;
    void step(float, std::uint64_t tick);
    std::map<std::uint64_t, LocalTransform> adopted_poses() const;
    void accept_poses(const std::map<std::uint64_t, LocalTransform>&);
    std::size_t size() const;
    bool realized() const;
    Native2DImage checkpoint(const Desired2D&, std::size_t remaining_bytes) const;
    void restore(const Json&, const std::string&, const Desired2D&, std::uint64_t tick);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::physics_detail
