#pragma once
#include "animation_material_inputs.hpp"
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
    void tick(double seconds, bool deliver_events);
    void reset_presentation();
    Json presentation_properties(flecs::entity_t, double alpha) const;
    bool ready() const;
    bool checkpoint_ready() const;
    bool pending() const { return pending_; }
    std::vector<std::uint64_t> take_discontinuities();
    Json checkpoint() const;
    void restore(const Json&);
    std::optional<AnimationPlayback> playback(EntityRef) const;
    bool control(EntityRef, AnimationControl);
    bool seek(EntityRef, double seconds);
    Json sequence_checkpoint(bool audio_entries = false) const;
    Json camera_shots_checkpoint() const;
    Json audio_cues_checkpoint() const;
    Json camera_presentation(flecs::entity_t, double alpha) const;
    void restore(const Json&, const Json& sequences, bool audio_entries = false);
    void pose_validator(AnimationRuntime::PoseValidator callback) {
        validator_ = std::move(callback);
    }

  private:
    struct MaterialTarget {
        asset_detail::PreparedModelDraw draw;
        asset_detail::PreparedModelDrawKey identity;
        ResourceIdentity clip;
        std::vector<MaterialParameterOverride> unanimated;
    };
    struct State {
        TransformAnimator config;
        AssetRef<TransformAnimationAsset> requested;
        EntityRef entity;
        ResourceTicket ticket;
        ResourceLease<TransformAnimationAsset> clip;
        double time = 0;
        double previous = 0, advance = 0;
        bool playing = false, discontinuous = true;
        flecs::entity_t sequence = 0;
        std::optional<MaterialTarget> material;
        bool material_pending = false, material_error = false;
        std::optional<std::uint64_t> sprite_frames;
    };
    struct SequenceState {
        AnimationSequence config;
        EntityRef entity;
        double time = 0, previous = 0, advance = 0;
        bool playing = false, discontinuous = true, entry_pending = true;
    };
    struct Shot {
        CameraShot config;
        EntityRef entity;
        flecs::entity_t sequence = 0, camera = 0;
    };
    struct Cue {
        AudioCue config;
        EntityRef entity;
        flecs::entity_t sequence = 0;
        bool ready = false, binding_error = false;
        std::string error;
    };
    WorldContext& context_;
    std::filesystem::path project_;
    ResourcePool<TransformAnimationAsset> pool_;
    flecs::query<const TransformAnimator> players_;
    flecs::query<const AnimationSequence> sequences_;
    flecs::query<const CameraShot> camera_shots_;
    flecs::query<const AudioCue> audio_cues_;
    std::map<flecs::entity_t, SequenceState> sequence_states_;
    std::set<flecs::entity_t> sequence_errors_;
    std::map<flecs::entity_t, std::vector<Shot>> shot_scenes_;
    std::set<flecs::entity_t> shot_errors_;
    std::map<flecs::entity_t, Cue> cue_states_;
    bool cue_over_capacity_ = false;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::map<flecs::entity_t, State> states_;
    std::set<std::uint64_t> discontinuities_;
    std::map<flecs::entity_t, std::string> errors_;
    bool pending_ = false;
    std::set<flecs::entity_t> load_errors_;
    AnimationRuntime::PoseValidator validator_;
    AnimationMaterialInputs material_inputs_;
    ResourceTicket request(AssetRef<TransformAnimationAsset>);
    void apply(const std::map<flecs::entity_t, double>& times, bool commit);
    void error(flecs::entity_t, const std::string&);
    void synchronize_sequences();
    void synchronize_camera_shots();
    void synchronize_audio_cues();
    void dispatch_audio_cues();
    bool audio_cues_checkpoint_ready() const;
    void cue_error(Cue&, const std::string&, bool binding);
    void synchronize_materials();
    double sequence_time(const State&, double) const;
    std::map<flecs::entity_t, double> times_at(flecs::entity_t controller, double time) const;
};
} // namespace forge
