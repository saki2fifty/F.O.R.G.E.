#pragma once
#include "navigation_behavior_resource.hpp"
#include <forge/navigation_service.hpp>
#include <forge/scene.hpp>
#include <functional>

namespace forge {
// Borrowed by NavigationRuntime. Resources owns catalog/loading; Flecs owns progress.
class NavigationBehaviorPlayback {
  public:
    using PlaneLookup = std::function<NavigationPlane(AssetRef<NavMeshAsset>)>;
    using AgentLookup = std::function<std::optional<NavigationAgentObservation>(EntityRef)>;
    explicit NavigationBehaviorPlayback(WorldContext&);
    ~NavigationBehaviorPlayback();
    void bind(Scene*);
    bool prepare(bool fail_on_error = false, std::span<const AssetId> extra_graphs = {});
    void begin_tick(double dt, std::uint64_t tick, const PlaneLookup&);
    void finish_tick(double dt, const AgentLookup&);
    Json debug(flecs::entity_t) const;
    Json checkpoint();
    void restore(const Json&); // Unpublished candidate only; discard on failure.

  private:
    struct Graph {
        std::uint64_t token = 0;
        ResourceLease<NavigationBehaviorAsset> lease;
        RuntimeResourceStatus status;
    };
    struct Frame {
        NavigationPlane plane;
        Double3 actor;
        std::optional<Double3> target, goal;
        std::optional<AffineTransform> anchor;
        bool anchor_required = false;
        NavigationBehaviorCursor cursor;
    };
    struct Player {
        NavigationBehavior config;
        std::optional<NavigationAgent> issued;
        std::string cooked_digest, revision, preparation_error, step_error, reported_error;
        std::uint64_t tick = 0;
        bool pending = false, sampled = false;
        std::optional<Frame> frame;
    };
    WorldContext& context_;
    Scene* scene_ = nullptr;
    flecs::query<const NavigationBehavior> query_;
    std::weak_ptr<RuntimeResourceService> resources_;
    std::map<AssetId, Graph> graphs_;
    std::map<flecs::entity_t, Player> players_;
    std::string global_error_;
    void check() const;
    std::vector<flecs::entity_t> controllers() const;
    void clear_graphs() noexcept;
    void release_intent(flecs::entity, Player&);
    void stop(flecs::entity, Player&);
    std::optional<Double3> goal(const NavigationBehaviorState&, const Frame&) const;
    void apply_intent(flecs::entity, Player&, const std::optional<Double3>&);
    void report(flecs::entity_t, Player&);
    flecs::entity resolve(EntityRef) const;
};
} // namespace forge
