#pragma once
#include "animation_material_inputs.hpp"
#include "sequence_clip_span.hpp"
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
    bool has_next_sequence_clips() const;
    bool has_nested_sequences() const;
    bool has_sequence_rates() const;
    void prepare_sequence_preview_root(EntityRef);
    bool pending() const { return pending_; }
    std::vector<std::uint64_t> take_discontinuities();
    Json checkpoint() const;
    void restore(const Json&);
    std::optional<AnimationPlayback> playback(EntityRef) const;
    Json sequence_clip_status(EntityRef) const;
    EntityRef parent_sequence(EntityRef) const;
    bool control(EntityRef, AnimationControl);
    bool seek(EntityRef, double seconds);
    Json sequence_checkpoint(unsigned profile = 3, bool rates = false) const;
    Json camera_shots_checkpoint(bool transitions = false) const;
    bool has_camera_blends() const;
    Json tune_camera_shot(EntityRef, const Json& value);
    void project_camera_shots(Json&, double alpha) const;
    Json audio_cues_checkpoint() const;
    Json camera_presentation(flecs::entity_t, double alpha) const;
    void restore(const Json&, const Json& sequences, unsigned profile = 3, bool rates = false);
    void pose_validator(AnimationRuntime::PoseValidator callback) {
        validator_ = std::move(callback);
    }
    // Private composition boundary: prepare with the complete candidate, then
    // publish only after every sequence consumer and the validator admitted it.
    using SequenceTimes = std::map<flecs::entity_t, double>;
    using SkeletalPose = std::function<std::function<void()>(
        std::map<std::uint64_t, TransformNode>&, const SequenceTimes&)>;
    void skeletal_pose(SkeletalPose prepare) { skeletal_pose_ = std::move(prepare); }
    flecs::entity_t resolve_sequence(flecs::entity, EntityRef) const;
    double sequence_duration(flecs::entity_t id) const;
    double sequence_sample_time(flecs::entity_t id, double alpha = 1,
                                const SequenceTimes& seek = {}) const;
    struct SequenceInterval {
        double begin = 0, advance = 0, duration = 0, offset = 0, end = 0;
        bool loop = false, playing = false, discontinuous = false;
        flecs::entity_t root = 0;
        double rate = 1, local_duration = 0;
    };
    SequenceInterval sequence_interval(flecs::entity_t id) const;
    SequenceTimes next_sequence_times(double seconds) const;
    void validate_sequence_sample(const SequenceTimes&);

  private:
    struct MaterialTarget {
        asset_detail::PreparedModelDraw draw;
        asset_detail::PreparedModelDrawKey identity;
        ResourceIdentity clip, next_clip;
        std::vector<MaterialParameterOverride> unanimated;
    };
    struct State {
        TransformAnimator config;
        AssetRef<TransformAnimationAsset> requested;
        EntityRef entity;
        ResourceTicket ticket;
        ResourceLease<TransformAnimationAsset> clip;
        AssetRef<TransformAnimationAsset> requested_next;
        ResourceTicket next_ticket;
        ResourceLease<TransformAnimationAsset> next_clip;
        std::optional<detail::SequenceClipSpan> span;
        std::vector<std::size_t> material_order, component_order;
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
        flecs::entity_t parent = 0, root = 0;
        double root_offset = 0, root_end = 0;
        double root_rate = 1;
        double time = 0, previous = 0, advance = 0;
        bool playing = false, discontinuous = true, entry_pending = true;
    };
    struct SequenceStep {
        double time = 0, advance = 0;
        bool playing = false, discontinuous = false;
    };
    struct Shot {
        CameraShot config;
        EntityRef entity;
        flecs::entity_t sequence = 0, camera = 0;
    };
    struct ShotSample {
        const Shot* current = nullptr;
        const Shot* previous = nullptr;
        double weight = 1;
    };
    ShotSample sample_camera_shot(flecs::entity_t membership, double alpha) const;
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
    std::set<flecs::entity_t> inactive_sequences_;
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
    SkeletalPose skeletal_pose_;
    AnimationMaterialInputs material_inputs_;
    ResourceTicket request(AssetRef<TransformAnimationAsset>);
    void apply(const std::map<flecs::entity_t, double>& times, bool commit,
               const SequenceTimes& seek = {});
    void error(flecs::entity_t, const std::string&);
    static flecs::entity_t scene_membership(flecs::entity);
    double controller_time(const SequenceState&, double root_time) const;
    SequenceStep sequence_step(const SequenceState&, double seconds) const;
    double controller_presentation_time(flecs::entity_t, double alpha) const;
    void synchronize_sequences();
    void synchronize_camera_shots();
    void synchronize_audio_cues();
    void dispatch_audio_cues();
    bool audio_cues_checkpoint_ready() const;
    void cue_error(Cue&, const std::string&, bool binding);
    void synchronize_materials();
    double sequence_time(const State&, double) const;
    double clip_span_duration(const State&) const;
    LocalTransform sample_pose(const State&, double) const;
    AnimationPropertyValues sample_properties(const State&, double) const;
    void sample_materials(const State&, double, std::span<AnimationMaterialValues>) const;
    void sample_components(const State&, double, std::span<nlohmann::json>) const;
    std::map<flecs::entity_t, double> times_at(const SequenceTimes&) const;
};
} // namespace forge
