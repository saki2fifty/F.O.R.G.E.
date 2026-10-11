#pragma once
#include <forge/animation.hpp>
#include <forge/resource.hpp>
#include <forge/transform_animation.hpp>
#include <set>
namespace forge {
template <> struct ResourceTraits<TransformAnimationAsset> {
    using Data = TransformAnimationClip;
};
// Private provider owned by AnimationRuntime. Uses its catalog, clock and validator.
class TransformAnimationPlayback {
  public:
    TransformAnimationPlayback(WorldContext&, std::filesystem::path);
    void catalog(std::shared_ptr<const AssetCatalog>);
    bool has_players() const;
    void synchronize();
    bool initial_pose();
    void tick(double seconds);
    bool ready() const;
    bool checkpoint_ready() const;
    bool pending() const { return pending_; }
    std::vector<std::uint64_t> take_discontinuities();
    Json checkpoint() const;
    void restore(const Json&);
    void pose_validator(AnimationRuntime::PoseValidator callback) {
        validator_ = std::move(callback);
    }

  private:
    struct State {
        TransformAnimator config;
        AssetRef<TransformAnimationAsset> requested;
        EntityRef entity;
        ResourceTicket ticket;
        ResourceLease<TransformAnimationAsset> clip;
        double time = 0;
        bool playing = false, discontinuous = true;
    };
    WorldContext& context_;
    std::filesystem::path project_;
    ResourcePool<TransformAnimationAsset> pool_;
    flecs::query<const TransformAnimator> players_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::map<flecs::entity_t, State> states_;
    std::set<std::uint64_t> discontinuities_;
    std::map<flecs::entity_t, std::string> errors_;
    bool pending_ = false;
    std::set<flecs::entity_t> load_errors_;
    AnimationRuntime::PoseValidator validator_;
    ResourceTicket request(AssetRef<TransformAnimationAsset>);
    void apply(const std::map<flecs::entity_t, double>& times, bool commit);
    void error(flecs::entity_t, const std::string&);
};
} // namespace forge
