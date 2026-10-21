#pragma once
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
};
struct Body2DDesired {
    EntityRef ref;
    Body2DConfiguration config;
    LocalTransform pose;
};
using Desired2D = std::map<std::uint64_t, Body2DDesired>;
// Reads the existing effective graph and schema. No native creation or ECS writes.
Desired2D desired_bodies_2d(WorldContext&, const PhysicsConfig&,
                            const std::map<std::uint64_t, TransformNode>&);

// Subordinate native state of PhysicsRuntime, never another service/world owner.
// No native identifier or header crosses this private boundary.
class Native2D {
  public:
    explicit Native2D(const PhysicsConfig&);
    ~Native2D();
    void check() const;
    void validate(const Desired2D&, float dt = 0) const;
    void synchronize(const Desired2D&, float, std::vector<std::uint64_t>& discontinuities);
    void step(float);
    std::map<std::uint64_t, LocalTransform> adopted_poses() const;
    void accept_poses(const std::map<std::uint64_t, LocalTransform>&);
    std::size_t size() const;
    bool realized() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::physics_detail
