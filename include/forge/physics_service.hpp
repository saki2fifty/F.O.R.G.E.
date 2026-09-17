#pragma once
#include <forge/identity.hpp>
#include <forge/transform.hpp>
#include <optional>
#include <vector>
namespace forge {
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
class PhysicsService {
  public:
    virtual ~PhysicsService() = default;
    virtual std::optional<PhysicsHit> raycast(Double3 origin, Double3 displacement) const = 0;
    virtual void teleport(EntityRef, LocalTranslation, LocalRotation, bool clear_velocity) = 0;
    virtual void move_kinematic(EntityRef, LocalTranslation, LocalRotation) = 0;
    virtual const std::vector<PhysicsContact>& contacts() const = 0;
};
} // namespace forge
