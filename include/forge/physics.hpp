#pragma once
#include <filesystem>
#include <forge/physics_components.hpp>
#include <forge/world.hpp>
namespace forge {
// Host composition API, not exported by the experimental gameplay SDK.
EngineModule physics_module(PhysicsConfig config = {}, std::filesystem::path content_root = {});
// Host-only error retaining identity through failed candidate-world destruction.
class PhysicsConfigurationError : public std::runtime_error {
  public:
    explicit PhysicsConfigurationError(Diagnostic value)
        : std::runtime_error(value.text), diagnostic(std::move(value)) {}
    Diagnostic diagnostic;
};
class PhysicsRuntime : public PhysicsService {
  public:
    explicit PhysicsRuntime(WorldContext&, PhysicsConfig, std::filesystem::path content_root = {});
    ~PhysicsRuntime() override;
    void stop() noexcept;
    void configure(PhysicsConfig);
    // Owned CPU preparation; no simulation tick/body publication. Required before
    // initial realization/recovery. False means native resource work is pending.
    bool prepare_assets();
    void refresh_assets();
    // Read-only host admission of an animation transform candidate. Uses the
    // same shape/spatial/solver ownership rules as realization, before ECS writes.
    void validate_transform_candidate(const std::map<std::uint64_t, TransformNode>&);
    void synchronize(float dt);
    void step(float dt);
    void adopt();
    std::vector<std::uint64_t> take_discontinuities();
    std::optional<PhysicsHit> raycast(Double3, Double3) const override;
    std::optional<PhysicsHit> raycast_filtered(Double3, Double3, PhysicsQueryFilter) const override;
    std::optional<PhysicsHit> shape_cast(const PhysicsSweep&, PhysicsQueryFilter) const override;
    std::uint64_t request_collision_asset(AssetId) override;
    RuntimeResourceStatus inspect_collision_asset(std::uint64_t) const override;
    bool release_collision_asset(std::uint64_t) override;
    void teleport(EntityRef, LocalTranslation, LocalRotation, bool) override;
    void move_kinematic(EntityRef, LocalTranslation, LocalRotation) override;
    const std::vector<PhysicsContact>& contacts() const override;
    CharacterState character(EntityRef) const override;
    void move_character(EntityRef, Double3) override;
    void jump_character(EntityRef, float) override;
    void crouch_character(EntityRef, bool) override;
    void place_character(EntityRef, LocalTranslation, LocalRotation, bool) override;
    Json checkpoint() const;
    // Fresh, unpublished world only. Scene/config reconstruction must precede this call.
    void restore(const Json&);
    Json status() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
