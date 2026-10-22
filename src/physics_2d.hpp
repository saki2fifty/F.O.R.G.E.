#pragma once
#include "physics_2d_joint.hpp"
#include "physics_command.hpp"
#include <forge/physics_components.hpp>
#include <forge/world.hpp>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace forge {
struct PhysicsDebugGeometry;
class TileMapData;
} // namespace forge
namespace forge::detail {
struct RuntimeTileMapSnapshot;
}
namespace forge::physics_detail {
struct BoxPart2D {
    std::array<float, 2> center{}, dimensions{}; // Scaled body-local metres; full dimensions.
    bool operator==(const BoxPart2D&) const = default;
};
struct Body2DConfiguration {
    PhysicsBody2D body;
    unsigned shape = 0;                // Box, circle, capsule, static compound boxes.
    std::array<float, 2> dimensions{}; // Effective world geometry, metres.
    std::shared_ptr<const std::vector<BoxPart2D>> parts{};
    // Derived controller role, not an authored PhysicsBody2D. The capsule's
    // native/local origin is at its feet; -1 selects floating move/slide.
    std::optional<float> mover_floor_y{};
    bool operator==(const Body2DConfiguration& other) const {
        return body == other.body && shape == other.shape && dimensions == other.dimensions &&
               mover_floor_y == other.mover_floor_y &&
               (parts == other.parts || (parts && other.parts && *parts == *other.parts));
    }
    Json json() const;
};
struct Body2DDesired {
    EntityRef ref;
    Body2DConfiguration config;
    LocalTransform pose;
    // Read-only cache provenance inside the existing realized-body record.
    std::shared_ptr<const detail::RuntimeTileMapSnapshot> tile_snapshot{};
    std::uint64_t tile_layer = 0;
};
using Desired2D = std::map<std::uint64_t, Body2DDesired>;
// Callers admit scalar storage through the existing reflection/schema owner.
// This shared conversion owns effective XY geometry/profile and native mass checks.
Body2DConfiguration inline_configuration_2d(PhysicsBody2D, unsigned shape,
                                            std::array<float, 2> raw_dimensions, LocalScale);
Body2DConfiguration tile_map_configuration_2d(PhysicsBody2D, const TileMapData&,
                                              std::uint64_t layer, LocalScale);
PhysicsDebugGeometry prepare_debug_2d(const Json&, LocalScale, std::size_t line_limit,
                                      const TileMapData* = nullptr);
LocalTransform debug_pose_2d(const AffineTransform&);
struct Native2DImage {
    Json metadata;
    std::string bytes;
};
struct MoverMotion2D {
    // Metres relative to the requested origin; translation includes shallow
    // recovery, while clipped_displacement contains only the requested motion.
    Double2 translation{}, clipped_displacement{};
    unsigned iterations = 0;
};
struct MoverState2D {
    Double2 position{}, intent{}, velocity{};
    std::uint64_t epoch = 0;
};
// Reads the existing effective graph and schema. No native creation or ECS writes.
class Native2D;
struct TileMap2DOverride {
    std::uint64_t entity = 0;
    const TileMapData* cells = nullptr;
};
Desired2D desired_bodies_2d(WorldContext&, const PhysicsConfig&,
                            const std::map<std::uint64_t, TransformNode>&,
                            const Native2D* previous = nullptr,
                            const TileMap2DOverride* candidate = nullptr,
                            bool allow_pending_maps = false);
void validate_shape_capacity_2d(const Desired2D&);
void validate_ray_2d(Double2, Double2);
void validate_overlap_2d(const PhysicsOverlap2D&);
void validate_sweep_2d(const PhysicsSweep2D&);

// Subordinate native state of PhysicsRuntime, never another service/world owner.
// No native identifier or header crosses this private boundary.
class Native2D {
  public:
    explicit Native2D(const PhysicsConfig&);
    ~Native2D();
    void check() const;
    void validate(const Desired2D&, float dt = 0, const std::set<std::uint64_t>& commanded = {},
                  const std::map<std::uint64_t, LocalTransform>& placements = {},
                  const DesiredJoints2D& joints = {}) const;
    void synchronize(const Desired2D&, const Desired2D& initial, float,
                     const std::set<std::uint64_t>& commanded,
                     std::vector<std::uint64_t>& discontinuities,
                     const DesiredJoints2D& joints = {});
    void apply(std::uint64_t, const BodyCommand&, std::vector<std::uint64_t>& discontinuities);
    void finish_targets(float);
    std::optional<PhysicsBodyState2D> state(std::uint64_t, EntityRef) const;
    std::optional<Double2> point_velocity(std::uint64_t, EntityRef, Double2) const;
    std::optional<PhysicsHit2D> raycast(Double2, Double2, PhysicsQueryFilter) const;
    std::vector<EntityRef> overlap(const PhysicsOverlap2D&, PhysicsQueryFilter) const;
    std::optional<PhysicsHit2D> shape_cast(const PhysicsSweep2D&, PhysicsQueryFilter) const;
    // Controller foundation only: capsule cast with native touching-surface
    // encroachment and the existing checked binding/filter/work policy. A miss
    // does not certify a clear starting pose or supply penetration recovery.
    std::optional<PhysicsHit2D> mover_cast(const PhysicsSweep2D&, PhysicsQueryFilter) const;
    // Maximum capsule penetration in metres, including deep contacts whose
    // native normal is zero. Observes the current pose; performs no recovery.
    double mover_penetration(const PhysicsOverlap2D&, PhysicsQueryFilter,
                             std::optional<EntityRef> ignore = {}) const;
    // -1 keeps floating move/slide. A floor-normal threshold also removes
    // unrequested uphill intent on steeper upward faces.
    MoverMotion2D mover_motion(const PhysicsSweep2D&, PhysicsQueryFilter,
                               float minimum_floor_y = -1) const;
    // Private fixed-boundary controller operations. Outer CharacterService owns
    // command admission/FIFO; body APIs cannot control this representation.
    void move_mover(std::uint64_t, EntityRef, Double2 velocity);
    bool place_mover(std::uint64_t, EntityRef, Double2 feet, bool clear_velocity);
    std::optional<MoverState2D> mover_state(std::uint64_t, EntityRef) const;
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
    std::size_t shape_size() const;
    std::size_t joint_size() const;
    bool realized() const;
    // Borrowed only during this owner's detached preflight, before native mutation.
    const Body2DDesired* admitted(std::uint64_t) const;
    Native2DImage checkpoint(const Desired2D&, std::size_t remaining_bytes,
                             const DesiredJoints2D& joints = {}) const;
    void restore(const Json&, const std::string&, const Desired2D&, std::uint64_t tick,
                 const DesiredJoints2D& joints = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge::physics_detail
