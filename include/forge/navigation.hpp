#pragma once
#include <forge/world.hpp>
namespace forge {
// The trusted host supplies its matching installed worker. Empty disables rebaking.
EngineModule navigation_module(std::filesystem::path project,
                               std::filesystem::path rebuild_worker = {});
class NavigationRuntime final : public NavigationService {
  public:
    NavigationRuntime(WorldContext&, std::filesystem::path,
                      std::filesystem::path rebuild_worker = {});
    ~NavigationRuntime() override;
    void bind(Scene*);  // RuntimeSimulation owns this bounded borrow.
    void synchronize(); // Capture geometry at a writable fixed-tick boundary.
    void tick(float dt, std::uint64_t tick);
    void physics_adopted(float dt, std::uint64_t tick);
    bool prepare_assets(); // Unpublished-world readiness; never advances behavior clocks.
    // One world-local request, never an authored asset/catalog publication.
    // Requests may queue from an owner-thread gameplay callback. Only an explicit
    // writable synchronization/poll boundary captures geometry or adopts a result.
    std::uint64_t request_rebuild(AssetRef<NavMeshAsset>) override;
    NavigationRebuildObservation rebuild_state(std::uint64_t) const override;
    std::optional<NavigationRebuildObservation> rebuild_state() const;
    bool cancel_rebuild(std::uint64_t) override;
    bool rebuild_pending() const;
    void poll_rebuild(); // May progress while paused; never advances gameplay time.
    NavResult project_point(AssetRef<NavMeshAsset>, Double3) override;
    NavResult find_path(AssetRef<NavMeshAsset>, Double3, Double3) override;
    NavResult project_point(AssetRef<NavMeshAsset>, Double3, const NavigationAreaFilter&) override;
    NavResult find_path(AssetRef<NavMeshAsset>, Double3, Double3,
                        const NavigationAreaFilter&) override;
    std::optional<NavigationAgentObservation> agent_state(EntityRef) const override;
    // Private editor Play transport; no movement or authored history is advanced.
    Json tune(EntityRef, const std::string& type, const std::string& property, const Json& value);
    Json tune_avoidance(EntityRef, const std::string& property, const Json& value);
    Json debug(flecs::entity_t) const;
    Json debug_behavior(flecs::entity_t) const;
    // Optional recovery is null while behavior resources are pending or retained
    // from a superseded selection. Invalid configuration/boundaries still throw.
    Json checkpoint();
    // Restore only into an unpublished candidate world; discard that world on failure.
    void restore(const Json&);
    void shutdown() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
