#include "animation_asset.hpp"
#include "animation_blend_space_resource.hpp"
#include "animation_mask_resource.hpp"
#include "animation_morph_blend.hpp"
#include "animation_playback_access.hpp"
#include "animation_resource.hpp"
#include "animation_rig_binding.hpp"
#include "animation_root_motion.hpp"
#include "animation_states_resource.hpp"
#include "asset_bytes.hpp"
#include "builtins.hpp"
#include "legacy_animation_provenance.hpp"
#include "transform_animation_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/assets.hpp>
#include <forge/navigation_components.hpp>
#include <forge/project_paths.hpp>
#include <forge/runtime_content_access.hpp>
#include <future>
#include <limits>
#include <set>
#include <thread>
#include <tuple>
namespace forge {
namespace {
using namespace animation_detail;
using namespace asset_detail;
struct AnimationBridge {
    std::weak_ptr<AnimationRuntime> runtime;
};
struct Pair {
    std::shared_ptr<const Skeleton> skeleton;
    std::shared_ptr<const Clip> clip;
    std::string skeleton_revision, clip_revision;
    ModelAnimationLease model;
    std::uint64_t model_generation = 0;
};
struct Playback {
    Animator config, requested;
    ResourceLease<AnimationStatesAsset> definition;
    ResourceLease<AnimationMaskAsset> mask;
    ResourceLease<AnimationBlendSpaceAsset> space;
    AnimationBlendSelection space_selection;
    std::map<AssetId, Pair> clips;
    std::unique_ptr<Sampler> outgoing_sampler;
    Pair outgoing_assets;
    Animator outgoing_config;
    double outgoing_time = 0, outgoing_previous = 0, outgoing_advance = 0;
    std::vector<LocalPose> frozen;
    MorphPose frozen_morphs, morph_defaults;
    EntityRef reference;
    Pair assets, blend_assets, third_assets;
    std::unique_ptr<Sampler> third_sampler;
    std::vector<AnimatedTransformChannel> transform_channels;
    RigInputs rig_inputs;
    RigConstraints rig, previous_rig;
    std::vector<AnimatedTransformChannel> rig_channels, pose_channels;
    std::vector<LocalPoseOverride> rig_base;
    std::vector<unsigned> rig_base_mask;
    bool rig_admitted = false;
    std::optional<std::size_t> rig_motion_root;
    std::vector<float> blend_mask;
    std::vector<float> morph_mask;
    std::unique_ptr<Sampler> sampler, blend_sampler;
    double blend = 0, previous_blend = 0, blend_from = 0, blend_elapsed = 0;
    double time = 0, previous = 0, advance = 0;
    // Snapshot of this tick's semantic interval; presentation snaps may reset advance.
    double tick_begin = 0, tick_advance = 0;
    bool playing = false, entry_pending = true;
    // Automatic admission failures belong to the held player, not a new ECS request.
    bool automatic_failure = false;
    bool model_pose_applied = false, discontinuous = true;
    struct RootSamples {
        std::shared_ptr<const Clip> clip;
        std::size_t joint = 0;
        RootRigid start, end, previous;
        double phase = -1;
    };
    std::map<const Clip*, RootSamples> root_samples;
};
double playback_duration(const Playback& p) {
    return p.space ? p.space->space.cycle_seconds : p.assets.clip->info().duration;
}
// Detached recovery preparation; these slots are adopted only after every
// candidate clock/pose succeeds. No persistent binding cache is serialized.
struct PreparedRig {
    RigInputs inputs;
    RigConstraints constraints;
    std::vector<AnimatedTransformChannel> affected, channels;
    std::vector<LocalPoseOverride> base;
    std::vector<unsigned> mask;
    std::optional<std::size_t> motion_root;
};
RootRigid root_rigid(const LocalPose& value) {
    RootRigid result;
    std::copy(value.translation.begin(), value.translation.end(), result.translation.begin());
    std::copy(value.rotation.begin(), value.rotation.end(), result.rotation.begin());
    return root_checked(result);
}
std::optional<std::size_t> root_joint(const Playback& p) {
    if (p.config.root_motion > 2)
        throw ArchiveError("Unknown Animator root motion mode");
    if (!p.config.root_motion)
        return {};
    if (!p.assets.model)
        throw ArchiveError("Root motion requires an imported model with durable skeleton nodes");
    const auto& skeleton = p.assets.model.skeleton.get();
    const auto& parents = p.assets.skeleton->info().parents;
    if (skeleton.joint_assets.size() != parents.size())
        throw ArchiveError("Reimport model to obtain durable root joint identities");
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < parents.size(); ++i) {
        if (parents[i] != -1 ||
            (p.config.root_joint.id && skeleton.joint_assets[i] != p.config.root_joint.id))
            continue;
        if (selected)
            throw ArchiveError("Choose a root joint for this multi-root skeleton");
        selected = i;
    }
    if (!selected)
        throw ArchiveError("Root joint must identify a root of this imported skeleton");
    return selected;
}
std::size_t evaluated_joints(const Playback& p) {
    if (p.space && p.space->space.dimensions == 2)
        return p.assets.skeleton->info().tracks * p.space_selection.count;
    const bool two_layers = p.blend_sampler || (p.definition && p.blend < 1);
    return p.assets.skeleton->info().tracks * (two_layers ? 2 : 1);
}
Json space_selection_json(const Playback& p) {
    auto result = Json::array();
    for (std::size_t i = 0; i < p.space_selection.count; ++i)
        result.push_back({{"sample", p.space->space.samples[p.space_selection.samples[i]].id},
                          {"weight", p.space_selection.weights[i]}});
    return result;
}
void append_markers(const Playback& p, std::uint64_t tick, std::vector<AnimationEvent>& events) {
    const auto& state = p.definition->state(p.config.state);
    const double duration = p.assets.clip->info().duration;
    const double begin = p.tick_begin, end = begin + p.tick_advance;
    const auto emit = [&](const AnimationMarker& marker, double time) {
        if (events.size() == 4096)
            throw ArchiveError("Animation marker delivery exceeds 4096 occurrences per fixed tick");
        events.push_back({p.reference, p.requested.states.id, p.config.clip.id, p.config.state,
                          marker.id, tick, (time - begin) / p.config.playback_speed, marker.phase,
                          marker.value, marker.name});
    };
    for (const auto& marker : state.markers) {
        const double time = marker.phase * duration;
        if (p.entry_pending && marker.phase == 0)
            emit(marker, begin);
        if (!p.config.loop) {
            if (time > begin && time <= end)
                emit(marker, time);
            continue;
        }
        const double first = std::floor((begin - time) / duration) + 1;
        const double last = std::floor((end - time) / duration);
        const double count = std::max(0., last - first + 1);
        if (count > 4096 - events.size())
            throw ArchiveError("Animation marker delivery exceeds 4096 occurrences per fixed tick");
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
            emit(marker, time + (first + i) * duration);
    }
}
struct SampledPose {
    std::vector<Matrix> model;
    std::vector<LocalPose> local;
    std::optional<LocalPose> incoming, second, outgoing;
    MorphPose morphs;
    std::optional<LocalPose> third;
};
std::vector<float> subtree_mask(const Pair& assets, AssetId selected) {
    if (!selected)
        return {};
    if (!assets.model)
        throw ArchiveError("Blend subtree requires an imported model with durable skeleton nodes");
    const auto& joints = assets.model.skeleton->joint_assets;
    const auto& parents = assets.skeleton->info().parents;
    if (joints.size() != parents.size())
        throw ArchiveError("Reimport model to obtain durable blend joint identities");
    const auto found = std::find(joints.begin(), joints.end(), selected);
    if (found == joints.end())
        throw ArchiveError("Blend subtree must identify a joint of this imported skeleton");
    const auto root = std::size_t(found - joints.begin());
    std::vector<float> result(parents.size(), 0);
    // Admitted skeletons have parents before children, so one pass includes descendants.
    for (std::size_t i = 0; i < parents.size(); ++i)
        result[i] = i == root || (parents[i] >= 0 && result[std::size_t(parents[i])] != 0) ? 1 : 0;
    return result;
}
std::vector<float> morph_subtree_mask(const Playback& p, std::span<const float> joints) {
    if (joints.empty() || p.morph_defaults.empty())
        return {};
    std::vector<float> result;
    const auto& nodes = p.assets.model.skeleton->joint_nodes;
    for (const auto& value : p.morph_defaults) {
        const auto found = std::find(nodes.begin(), nodes.end(), value.node);
        if (found == nodes.end())
            throw ArchiveError("Morph curve node has no admitted skeleton mapping");
        result.push_back(joints[std::size_t(found - nodes.begin())]);
    }
    return result;
}
MorphPose sample_morphs(const Playback& p, double time, double blend, double outgoing_time) {
    if (!p.assets.model || p.morph_defaults.empty())
        return {};
    const auto incoming = p.assets.model.clip->morphs->sample(
        time * p.assets.clip->info().duration / playback_duration(p));
    if (p.definition) {
        MorphPose outgoing;
        if (!p.frozen.empty())
            outgoing = p.frozen_morphs;
        else if (p.outgoing_sampler)
            outgoing = p.outgoing_assets.model.clip->morphs->sample(
                outgoing_time < 0 ? p.outgoing_time : outgoing_time);
        return blend_morph_samples(p.morph_defaults, outgoing, incoming, blend);
    }
    MorphPose second;
    if (p.blend_assets.model)
        second = p.blend_assets.model.clip->morphs->sample(
            time * p.blend_assets.clip->info().duration / playback_duration(p));
    if (p.space && p.space->space.dimensions == 2) {
        if (!p.blend_sampler)
            return blend_morph_samples(p.morph_defaults, incoming, {}, 0);
        auto result = incoming;
        double total = p.space_selection.weights[0];
        if (p.blend_sampler) {
            total += p.space_selection.weights[1];
            result = blend_morph_samples(p.morph_defaults, result, second,
                                         p.space_selection.weights[1] / total);
        }
        if (p.third_sampler) {
            total += p.space_selection.weights[2];
            const auto third = p.third_assets.model.clip->morphs->sample(
                time * p.third_assets.clip->info().duration / playback_duration(p));
            result = blend_morph_samples(p.morph_defaults, result, third,
                                         p.space_selection.weights[2] / total);
        }
        return result;
    }
    return blend_morph_samples(p.morph_defaults, incoming, second, p.blend_sampler ? blend : 0,
                               p.config.blend_mode == 1, p.morph_mask);
}
SampledPose sample_pose(Playback& p, double time, double blend, bool temporary = false,
                        double outgoing_time = -1, const RigConstraints* constraints = nullptr,
                        bool apply_constraints = true,
                        const std::vector<LocalPoseOverride>* base = nullptr) {
    std::unique_ptr<Sampler> first, second, third;
    auto* sampler = p.sampler.get();
    auto* blend_sampler = p.blend_sampler.get();
    auto* third_sampler = p.third_sampler.get();
    const auto joint = root_joint(p);
    if (temporary) {
        first = std::make_unique<Sampler>(p.assets.skeleton, p.assets.clip);
        sampler = first.get();
        if (p.blend_assets.clip) {
            second = std::make_unique<Sampler>(p.assets.skeleton, p.blend_assets.clip);
            blend_sampler = second.get();
        }
        if (p.third_assets.clip) {
            third = std::make_unique<Sampler>(p.assets.skeleton, p.third_assets.clip);
            third_sampler = third.get();
        }
    }
    sampler->root_motion_joint(joint);
    if (blend_sampler)
        blend_sampler->root_motion_joint(joint);
    if (third_sampler)
        third_sampler->root_motion_joint(joint);
    const auto ratio = float(std::clamp(time / playback_duration(p), 0., 1.));
    std::vector<Matrix> model;
    std::optional<LocalPose> outgoing_root;
    if (p.space && p.space->space.dimensions == 2) {
        const std::array<Sampler*, 3> samplers{sampler, blend_sampler, third_sampler};
        std::array<WeightedAnimationSample, 3> layers;
        for (std::size_t i = 0; i < p.space_selection.count; ++i)
            layers[i] = {samplers[i], ratio, float(p.space_selection.weights[i])};
        model = sampler->sample_weighted({layers.data(), p.space_selection.count});
    } else if (p.definition && blend < 1 && (!p.frozen.empty() || p.outgoing_sampler)) {
        if (!p.frozen.empty())
            model = sampler->sample_transition(ratio, p.frozen, float(blend));
        else {
            std::unique_ptr<Sampler> outgoing;
            auto* previous = p.outgoing_sampler.get();
            if (temporary) {
                outgoing = std::make_unique<Sampler>(p.assets.skeleton, p.outgoing_assets.clip);
                previous = outgoing.get();
            }
            previous->root_motion_joint(joint);
            model = sampler->sample_transition(
                ratio, *previous,
                float(std::clamp((outgoing_time < 0 ? p.outgoing_time : outgoing_time) /
                                     p.outgoing_assets.clip->info().duration,
                                 0., 1.)),
                float(blend));
            outgoing_root = previous->sampled_root();
        }
    } else
        model = blend_sampler ? sampler->sample_blended(ratio, *blend_sampler, float(blend),
                                                        p.blend_mask, p.config.blend_mode == 1)
                              : sampler->sample(ratio);
    const auto& rig = constraints ? *constraints : p.rig;
    const auto& rig_base = base ? *base : p.rig_base;
    if (apply_constraints && (rig.reach || rig.aim || !rig_base.empty()))
        model = sampler->constrain(rig, rig_base);
    return {std::move(model),
            sampler->local_pose(),
            sampler->sampled_root(),
            blend_sampler ? blend_sampler->sampled_root() : std::nullopt,
            outgoing_root,
            sample_morphs(p, time, blend, outgoing_time),
            third_sampler ? third_sampler->sampled_root() : std::nullopt};
}
RootRigid layer_motion(Playback& p, const Pair& assets, std::size_t joint, double begin,
                       double advance, bool loop, double time,
                       const std::optional<LocalPose>& sampled) {
    auto found = p.root_samples.find(assets.clip.get());
    if (found == p.root_samples.end() || found->second.joint != joint) {
        Sampler sampler(assets.skeleton, assets.clip);
        sampler.root_motion_joint(joint);
        Playback::RootSamples value;
        value.clip = assets.clip;
        value.joint = joint;
        (void)sampler.sample(0);
        value.start = root_rigid(*sampler.sampled_root());
        (void)sampler.sample(1);
        value.end = root_rigid(*sampler.sampled_root());
        found = p.root_samples.insert_or_assign(assets.clip.get(), std::move(value)).first;
    }
    auto& cache = found->second;
    const auto sample = [&](double phase) {
        Sampler sampler(assets.skeleton, assets.clip);
        sampler.root_motion_joint(joint);
        (void)sampler.sample(float(std::clamp(phase / assets.clip->info().duration, 0., 1.)));
        return root_rigid(*sampler.sampled_root());
    };
    const auto previous = cache.phase == begin ? cache.previous : sample(begin);
    const auto current = sampled ? root_rigid(*sampled) : sample(time);
    const auto delta = root_interval(previous, current, cache.start, cache.end, loop, begin,
                                     advance, assets.clip->info().duration);
    cache.previous = current;
    cache.phase = time;
    return delta;
}
RootRigid root_motion_delta(Playback& p, const SampledPose& pose, std::size_t joint) {
    const double incoming_ratio = p.assets.clip->info().duration / playback_duration(p);
    auto incoming = layer_motion(p, p.assets, joint, p.tick_begin * incoming_ratio,
                                 p.tick_advance * incoming_ratio, p.config.loop,
                                 p.time * incoming_ratio, pose.incoming);
    const double weight = (p.previous_blend + p.blend) * .5;
    if (p.space && p.space->space.dimensions == 2) {
        double total = p.space_selection.weights[0];
        if (p.blend_sampler) {
            const double ratio = p.blend_assets.clip->info().duration / playback_duration(p);
            const auto second =
                layer_motion(p, p.blend_assets, joint, p.tick_begin * ratio, p.tick_advance * ratio,
                             p.config.loop, p.time * ratio, pose.second);
            total += p.space_selection.weights[1];
            incoming = root_blend(incoming, second, p.space_selection.weights[1] / total);
        }
        if (p.third_sampler) {
            const double ratio = p.third_assets.clip->info().duration / playback_duration(p);
            const auto third =
                layer_motion(p, p.third_assets, joint, p.tick_begin * ratio, p.tick_advance * ratio,
                             p.config.loop, p.time * ratio, pose.third);
            total += p.space_selection.weights[2];
            incoming = root_blend(incoming, third, p.space_selection.weights[2] / total);
        }
    } else if (p.definition && p.previous_blend < 1) {
        RootRigid outgoing;
        if (p.outgoing_sampler)
            outgoing =
                layer_motion(p, p.outgoing_assets, joint, p.outgoing_previous, p.outgoing_advance,
                             p.outgoing_config.loop, p.outgoing_time, pose.outgoing);
        incoming = root_blend(outgoing, incoming, weight);
    } else if (p.blend_sampler) {
        const double ratio = p.blend_assets.clip->info().duration / playback_duration(p);
        const auto second =
            layer_motion(p, p.blend_assets, joint, p.tick_begin * ratio, p.tick_advance * ratio,
                         p.config.loop, p.time * ratio, pose.second);
        const auto root_weight = p.blend_mask.empty() ? 1. : double(p.blend_mask.at(joint));
        incoming = p.config.blend_mode == 1
                       ? root_compose(incoming, root_blend({}, second, weight * root_weight))
                       : root_blend(incoming, second, weight * root_weight);
    }
    const auto rest = root_rigid(p.assets.skeleton->joint_rest_pose(joint));
    return root_compose(root_compose(rest, incoming), root_inverse(rest));
}
const std::vector<AnimatedTransformChannel>& animated_channels(const Playback& p) {
    return p.rig_admitted ? p.pose_channels : p.transform_channels;
}
std::vector<AnimatedTransformChannel> rig_channels(const Playback& p, const RigConstraints& rig) {
    std::vector<AnimatedTransformChannel> result;
    const auto add = [&](std::size_t joint) {
        const auto node = p.assets.model.skeleton->joint_nodes.at(joint);
        if (std::none_of(result.begin(), result.end(),
                         [&](const auto& c) { return c.node == node; }))
            result.push_back({node, AnimatedTransformPath::Rotation});
    };
    if (rig.reach) {
        add(rig.reach->start);
        add(rig.reach->middle);
    }
    if (rig.aim)
        add(rig.aim->joint);
    return result;
}
std::vector<AnimatedTransformChannel>
channel_union(std::span<const AnimatedTransformChannel> first,
              std::span<const AnimatedTransformChannel> second) {
    std::vector<AnimatedTransformChannel> result(first.begin(), first.end());
    for (const auto& value : second)
        if (std::none_of(result.begin(), result.end(), [&](const auto& c) {
                return c.node == value.node && c.path == value.path;
            }))
            result.push_back(value);
    return result;
}
std::vector<unsigned> rig_base_mask(std::span<const std::size_t> nodes,
                                    std::span<const AnimatedTransformChannel> channels) {
    std::vector<unsigned> result(nodes.size(), 7);
    for (const auto& channel : channels) {
        const auto found = std::find(nodes.begin(), nodes.end(), channel.node);
        if (found == nodes.end())
            throw ArchiveError("IK channel has no admitted joint mapping");
        result[std::size_t(found - nodes.begin())] &=
            ~(channel.path == AnimatedTransformPath::Translation ? 1u
              : channel.path == AnimatedTransformPath::Rotation  ? 2u
                                                                 : 4u);
    }
    return result;
}
std::vector<AnimatedTransformChannel> former_rig_channels(const Playback& old,
                                                          const Playback& next) {
    if (old.rig_channels.empty())
        return {};
    if (old.assets.skeleton == next.assets.skeleton)
        return old.rig_channels;
    if (!old.assets.model || !next.assets.model ||
        old.assets.model.skeleton->model != next.assets.model.skeleton->model)
        return {};
    const auto& before = old.assets.model.skeleton.get();
    const auto& after = next.assets.model.skeleton.get();
    std::vector<AnimatedTransformChannel> result;
    for (const auto& channel : old.rig_channels) {
        const auto source =
            std::find(before.joint_nodes.begin(), before.joint_nodes.end(), channel.node);
        if (source == before.joint_nodes.end())
            continue;
        const auto id = before.joint_assets.at(std::size_t(source - before.joint_nodes.begin()));
        const auto target = std::find(after.joint_assets.begin(), after.joint_assets.end(), id);
        if (target != after.joint_assets.end())
            result.push_back(
                {after.joint_nodes.at(std::size_t(target - after.joint_assets.begin())),
                 channel.path});
    }
    return result;
}
} // namespace
struct AnimationRuntime::Impl {
    WorldContext& context;
    std::filesystem::path project;
    TransformAnimationPlayback transforms;
    bool initial_catalog_requested = false, deliver_events = true;
    std::vector<AnimationEvent> events, event_candidates;
    // Phase guard only, copied from RuntimeSimulation's authoritative fixed tick.
    std::optional<std::uint64_t> last_animation_tick;
    std::string catalog_error;
    std::map<AssetId, std::shared_ptr<const Skeleton>> skeletons;
    std::map<AssetId, std::shared_ptr<const Clip>> clips;
    std::map<AssetId, std::string> revisions;
    std::unique_ptr<ModelAnimationResources> model_resources;
    std::shared_ptr<const AssetCatalog> model_catalog;
    std::map<std::pair<AssetId, AssetId>, ModelAnimationRequest> model_requests;
    ResourcePool<AnimationStatesAsset> state_pool{{1, 64, 64, 4 * 1024 * 1024}};
    std::map<AssetId, ResourceTicket> state_requests;
    ResourcePool<AnimationMaskAsset> mask_pool{{1, 64, 64, 4 * 1024 * 1024}};
    std::map<AssetId, ResourceTicket> mask_requests;
    ResourcePool<AnimationBlendSpaceAsset> space_pool{{1, 64, 64, 4 * 1024 * 1024}};
    std::map<AssetId, ResourceTicket> space_requests;
    std::map<flecs::entity_t, Playback> states;
    std::map<flecs::entity_t, AnimationRootMotion> root_motions;
    std::set<flecs::entity_t> pending;
    std::map<flecs::entity_t, std::string> errors;
    std::map<flecs::entity_t, std::string> binding_errors;
    std::set<flecs::entity_t> orphan_nodes;
    std::map<flecs::entity_t, Animator> failed_configurations;
    std::set<flecs::entity_t> reload_pending;
    std::map<flecs::entity_t, std::string> reload_errors;
    std::future<std::shared_ptr<const AssetCatalog>> catalog_job;
    bool catalog_again = false;
    std::size_t cache_bytes = 0;
    AnimationRuntime::PoseValidator pose_validator;
    std::set<std::uint64_t> discontinuities;
    Impl(WorldContext& c, std::filesystem::path p)
        : context(c), project(std::move(p)), transforms(c, project) {}
    ~Impl() {
        states.clear();
        if (model_resources)
            model_resources->close();
    }
    void apply_model_poses(bool commit = true, const std::map<flecs::entity_t, double>& times = {},
                           flecs::entity_t only = 0,
                           const std::map<flecs::entity_t, double>& weights = {},
                           std::optional<std::uint64_t> motion_tick = {},
                           std::map<flecs::entity_t, PreparedRig>* recovered_rigs = nullptr,
                           const RigInputs* tuned_rig = nullptr, bool transactional = false) {
        // One derived instance index per fixed tick. Native ChildOf lookup also
        // resolves Flecs Parent storage at the pinned revision. Nested roots are
        // boundaries, including roots belonging to another model.
        std::map<flecs::entity_t, std::map<AssetId, flecs::entity_t>> instances;
        std::set<flecs::entity_t> ambiguous;
        std::set<flecs::entity_t> orphans;
        context.world().each([&](flecs::entity node, const ModelSource& source) {
            if (node.has(flecs::Prefab) || !source.node.id || !context.reference(node.id()))
                return;
            auto parent = node.target(flecs::ChildOf);
            bool associated = false;
            std::set<flecs::entity_t> visited{node.id()};
            while (parent && visited.insert(parent.id()).second) {
                if (parent.has<ModelSource>()) {
                    const auto& owner = parent.get<ModelSource>();
                    if (!owner.node.id) {
                        if (owner.model == source.model && context.reference(parent.id())) {
                            associated = true;
                            if (!instances[parent.id()].emplace(source.node.id, node.id()).second)
                                ambiguous.insert(parent.id());
                        }
                        break;
                    }
                }
                parent = parent.target(flecs::ChildOf);
            }
            if (!associated)
                orphans.insert(node.id());
        });
        if (commit) {
            for (const auto id : orphans)
                if (!orphan_nodes.contains(id)) {
                    Diagnostic diagnostic{Severity::Warning,
                                          "animation.node_scope",
                                          "Model node is outside its matching instance root; "
                                          "animation does not retarget it",
                                          {}};
                    diagnostic.context.entity = context.reference(id)->entity;
                    diagnostic.context.module = "forge.animation";
                    context.services().emit(std::move(diagnostic));
                }
            orphan_nodes = std::move(orphans);
        }
        for (auto& [id, playback] : states) {
            if (only && id != only)
                continue;
            if (!playback.config.enabled || (!only && pending.contains(id))) {
                if (commit)
                    binding_errors.erase(id);
                continue;
            }
            const auto root = context.world().entity(id);
            if (!root.has<ModelSource>()) {
                // Legacy standalone Animator remains a pose-inspection consumer.
                if (commit)
                    binding_errors.erase(id);
                continue;
            }
            try {
                const auto& source = root.get<ModelSource>();
                if (source.node.id || !playback.assets.model ||
                    source.model.id != playback.assets.model.skeleton->model)
                    throw ArchiveError(
                        "Model Animator must be on its matching model instance root");
                const auto& skeleton = playback.assets.model.skeleton.get();
                const auto& clip = playback.assets.model.clip.get();
                if (skeleton.joint_assets.size() != skeleton.joint_nodes.size() ||
                    !clip.has_transform_channels)
                    throw ArchiveError(
                        "Reimport model animation to obtain durable nodes and channel intent");
                if (ambiguous.contains(id))
                    throw ArchiveError("Model instance has duplicate source node identities");
                RigInputs inputs;
                if (root.has<TwoBoneIK>())
                    inputs.reach = root.get<TwoBoneIK>();
                if (root.has<AimIK>())
                    inputs.aim = root.get<AimIK>();
                if (tuned_rig)
                    inputs = *tuned_rig;
                struct RigCandidate {
                    RigConstraints rig;
                    std::vector<AnimatedTransformChannel> affected, channels, next_channels;
                    bool layout_changed = true;
                    std::vector<unsigned> mask, next_mask;
                };
                std::optional<RigCandidate> new_rig;
                const auto motion_root = root_joint(playback);
                if (!playback.rig_admitted || inputs != playback.rig_inputs ||
                    playback.rig_motion_root != motion_root) {
                    if (inputs.reach)
                        detail::validate_reflected_value(root, *inputs.reach);
                    if (inputs.aim)
                        detail::validate_reflected_value(root, *inputs.aim);
                    auto rig = bind_rig(*playback.assets.skeleton, skeleton.joint_assets, inputs,
                                        motion_root, &playback.rig);
                    new_rig.emplace();
                    new_rig->layout_changed =
                        !playback.rig_admitted || !same_rig_layout(playback.rig, rig);
                    if (new_rig->layout_changed) {
                        new_rig->affected = rig_channels(playback, rig);
                        new_rig->next_channels =
                            channel_union(playback.transform_channels, new_rig->affected);
                        new_rig->channels =
                            channel_union(new_rig->next_channels, playback.rig_channels);
                        if (rig.reach || rig.aim || !playback.rig_channels.empty())
                            new_rig->mask = rig_base_mask(skeleton.joint_nodes, new_rig->channels);
                        if (rig.reach || rig.aim)
                            new_rig->next_mask =
                                rig_base_mask(skeleton.joint_nodes, new_rig->next_channels);
                    }
                    new_rig->rig = std::move(rig);
                }
                const auto& constraints = new_rig ? new_rig->rig : playback.rig;
                const auto mandatory_joint = [&](std::size_t index) {
                    if (!instances[id].contains(skeleton.joint_assets.at(index)))
                        throw ArchiveError("IK chain joint is missing from this model instance");
                };
                if (constraints.reach) {
                    mandatory_joint(constraints.reach->start);
                    mandatory_joint(constraints.reach->middle);
                    mandatory_joint(constraints.reach->end);
                }
                if (constraints.aim)
                    mandatory_joint(constraints.aim->joint);
                const auto& channels = new_rig && new_rig->layout_changed
                                           ? new_rig->channels
                                           : animated_channels(playback);
                auto candidate = context.transform_nodes();
                std::vector<LocalPoseOverride> base;
                if (constraints.reach || constraints.aim || !playback.rig_channels.empty()) {
                    base.resize(skeleton.joint_nodes.size());
                    const auto& masks =
                        new_rig && new_rig->layout_changed ? new_rig->mask : playback.rig_base_mask;
                    const auto& parents = playback.assets.skeleton->info().parents;
                    for (std::size_t j = 0; j < base.size(); ++j) {
                        const auto target = instances[id].find(skeleton.joint_assets.at(j));
                        if (target == instances[id].end())
                            continue;
                        const auto entity = context.world().entity(target->second);
                        if (!candidate.contains(entity.id()))
                            throw ArchiveError("IK model node has no local transform");
                        const auto parent = parents.at(j);
                        const auto expected =
                            parent < 0
                                ? id
                                : instances[id].at(skeleton.joint_assets.at(std::size_t(parent)));
                        if ((constraints.reach || constraints.aim) &&
                            candidate.at(entity.id()).parent != expected)
                            throw ArchiveError(
                                "IK needs the admitted model's effective joint hierarchy");
                        const auto local = context.get_local_transform(entity);
                        base[j].value = {{float(local.translation.x), float(local.translation.y),
                                          float(local.translation.z)},
                                         {local.scale.x, local.scale.y, local.scale.z},
                                         {local.rotation.x, local.rotation.y, local.rotation.z,
                                          local.rotation.w}};
                        base[j].channels = masks.at(j);
                    }
                }
                const auto time = times.contains(id) ? times.at(id) : playback.time;
                const auto weight = weights.contains(id) ? weights.at(id) : playback.blend;
                const auto pose =
                    sample_pose(playback, time, weight, !commit, -1, &constraints, true, &base);
                const auto& locals = pose.local;
                struct Write {
                    flecs::entity entity;
                    LocalTransform value;
                    unsigned channels = 0;
                };
                std::map<flecs::entity_t, Write> writes;
                std::optional<AnimationRootMotion> motion;
                if (const auto joint = root_joint(playback)) {
                    const auto target = instances[id].find(skeleton.joint_assets.at(*joint));
                    if (target == instances[id].end() || !candidate.contains(target->second))
                        throw ArchiveError("Root motion joint is missing from the model instance");
                    const auto entity = context.world().entity(target->second);
                    auto value = context.get_local_transform(entity);
                    const auto& local = locals.at(*joint);
                    value.translation = {local.translation[0], local.translation[1],
                                         local.translation[2]};
                    value.rotation = normalized({local.rotation[0], local.rotation[1],
                                                 local.rotation[2], local.rotation[3]});
                    writes.emplace(entity.id(), Write{entity, value,
                                                      unsigned(TransformChannel::Translation) |
                                                          unsigned(TransformChannel::Rotation)});
                    candidate.at(entity.id()).local = value;
                    if (motion_tick) {
                        const auto delta = root_motion_delta(playback, pose, *joint);
                        motion = AnimationRootMotion{playback.reference,
                                                     skeleton.joint_assets.at(*joint),
                                                     playback.config.clip.id, *motion_tick};
                        motion->translation = delta.translation;
                        std::copy(delta.rotation.begin(), delta.rotation.end(),
                                  motion->rotation.begin());
                        motion->applied = playback.config.root_motion == 1;
                        if (motion->applied) {
                            if (root.has<NavigationAgent>() &&
                                root.get<NavigationAgent>().enabled &&
                                root.get<NavigationAgent>().has_destination)
                                throw ArchiveError("Root motion Apply conflicts with an active "
                                                   "Navigation Agent; use Extract");
                            if (!candidate.contains(id))
                                throw ArchiveError("Root motion entity has no local transform");
                            const auto previous = context.get_local_transform(root);
                            LocalTransform rigid;
                            rigid.translation = {delta.translation[0], delta.translation[1],
                                                 delta.translation[2]};
                            rigid.rotation = normalized({motion->rotation[0], motion->rotation[1],
                                                         motion->rotation[2], motion->rotation[3]});
                            auto value = decompose(
                                affine_transform(previous) * affine_transform(rigid), &previous);
                            if (!equivalent(value.scale, previous.scale))
                                throw ArchiveError("Root motion rotation cannot preserve this "
                                                   "entity scale; use Extract");
                            value.scale = previous.scale;
                            detail::validate_reflected_value(root, value.translation);
                            detail::validate_reflected_value(root, value.rotation);
                            writes.emplace(id, Write{root, value,
                                                     unsigned(TransformChannel::Translation) |
                                                         unsigned(TransformChannel::Rotation)});
                            candidate.at(id).local = value;
                        }
                    }
                }
                for (const auto& channel : channels) {
                    const auto found = std::find(skeleton.joint_nodes.begin(),
                                                 skeleton.joint_nodes.end(), channel.node);
                    if (found == skeleton.joint_nodes.end())
                        throw ArchiveError("Animation channel has no joint mapping");
                    const auto index = std::size_t(found - skeleton.joint_nodes.begin());
                    const auto target = instances[id].find(skeleton.joint_assets.at(index));
                    // A selected glTF scene can omit nodes from the shared rig.
                    // Missing optional targets never bind another instance.
                    if (target == instances[id].end())
                        continue;
                    const auto entity = context.world().entity(target->second);
                    if (!candidate.contains(entity.id()))
                        throw ArchiveError("Animated model node has no local transform");
                    auto [entry, inserted] = writes.try_emplace(
                        entity.id(), Write{entity, context.get_local_transform(entity)});
                    (void)inserted;
                    auto& value = entry->second.value;
                    const auto& local = locals.at(index);
                    if (channel.path == AnimatedTransformPath::Translation) {
                        value.translation = {local.translation[0], local.translation[1],
                                             local.translation[2]};
                        detail::validate_reflected_value(entity, value.translation);
                        entry->second.channels |= unsigned(TransformChannel::Translation);
                    } else if (channel.path == AnimatedTransformPath::Rotation) {
                        value.rotation = normalized({local.rotation[0], local.rotation[1],
                                                     local.rotation[2], local.rotation[3]});
                        detail::validate_reflected_value(entity, value.rotation);
                        entry->second.channels |= unsigned(TransformChannel::Rotation);
                    } else {
                        value.scale =
                            checked_local_scale({local.scale[0], local.scale[1], local.scale[2]});
                        detail::validate_reflected_value(entity, value.scale);
                        entry->second.channels |= unsigned(TransformChannel::Scale);
                    }
                    candidate.at(entity.id()).local = value;
                }
                // Validate the complete prepared pose before any ECS mutation.
                (void)evaluate_transforms(candidate);
                if (pose_validator)
                    pose_validator(candidate);
                if (!commit) {
                    if (recovered_rigs &&
                        (inputs.reach || inputs.aim || !playback.rig_channels.empty())) {
                        const bool changed = new_rig && new_rig->layout_changed;
                        PreparedRig prepared{
                            inputs,
                            constraints,
                            changed ? new_rig->affected : playback.rig_channels,
                            changed ? new_rig->next_channels : playback.pose_channels,
                            constraints.reach || constraints.aim ? std::move(base)
                                                                 : std::vector<LocalPoseOverride>{},
                            changed ? new_rig->next_mask : playback.rig_base_mask,
                            motion_root};
                        recovered_rigs->emplace(id, std::move(prepared));
                    }
                    continue;
                }
                // Allocate one observation node before any ECS write. Publishing
                // an extracted node does not allocate or copy earlier actors.
                decltype(root_motions)::node_type prepared_motion;
                if (motion) {
                    if (root_motions.contains(id))
                        throw ArchiveError(
                            "Root motion observation duplicated in one fixed interval");
                    decltype(root_motions) single;
                    single.emplace(id, *motion);
                    prepared_motion = single.extract(id);
                }
                const bool snap =
                    tuned_rig || playback.discontinuous || binding_errors.contains(id) ||
                    (new_rig && new_rig->layout_changed) || !same_rig_base(base, playback.rig_base);
                std::optional<std::set<std::uint64_t>> prepared_discontinuities;
                if (snap) {
                    // Prepare allocating bookkeeping before the first ECS write.
                    // Publication below is a non-allocating owner swap.
                    prepared_discontinuities = discontinuities;
                    for (const auto& [target, write] : writes) {
                        (void)write;
                        prepared_discontinuities->insert(target);
                    }
                }
                // Live tuning shares this complete-pose transaction. The typed
                // request is adopted only after binding, solving, physics and
                // allocating bookkeeping all succeeded. No clock is advanced.
                if (tuned_rig) {
                    if (inputs.reach && root.get<TwoBoneIK>() != *inputs.reach)
                        root.set(*inputs.reach);
                    if (inputs.aim && root.get<AimIK>() != *inputs.aim)
                        root.set(*inputs.aim);
                }
                for (auto& [target, write] : writes) {
                    (void)target;
                    if (write.channels & unsigned(TransformChannel::Translation))
                        write.entity.set(write.value.translation);
                    if (write.channels & unsigned(TransformChannel::Rotation))
                        write.entity.set(write.value.rotation);
                    if (write.channels & unsigned(TransformChannel::Scale))
                        write.entity.set(write.value.scale);
                }
                if (snap) {
                    discontinuities.swap(*prepared_discontinuities);
                    // A new/repaired binding has no compatible previous sample.
                    // Morph sampling and transform interpolation must snap to
                    // the same successful fixed-boundary time.
                    playback.previous = playback.time;
                    playback.advance = 0;
                    playback.previous_blend = playback.blend;
                }
                playback.model_pose_applied = true;
                playback.rig_base = constraints.reach || constraints.aim
                                        ? std::move(base)
                                        : std::vector<LocalPoseOverride>{};
                playback.previous_rig = motion_tick && !snap ? playback.rig : constraints;
                if (new_rig) {
                    playback.rig_inputs = std::move(inputs);
                    playback.rig = std::move(new_rig->rig);
                    if (new_rig->layout_changed) {
                        playback.rig_channels = std::move(new_rig->affected);
                        playback.pose_channels = std::move(new_rig->next_channels);
                        playback.rig_base_mask = std::move(new_rig->next_mask);
                    }
                    playback.rig_admitted = true;
                    playback.rig_motion_root = motion_root;
                }
                playback.discontinuous = false;
                binding_errors.erase(id);
                if (motion)
                    root_motions.insert(std::move(prepared_motion));
            } catch (const std::exception& ex) {
                if (!commit || tuned_rig || transactional)
                    throw;
                if (binding_errors[id] != ex.what()) {
                    Diagnostic diagnostic{Severity::Error, "animation.binding", ex.what(), {}};
                    diagnostic.context.entity = playback.reference.entity;
                    diagnostic.context.asset = playback.config.clip.id;
                    diagnostic.context.module = "forge.animation";
                    context.services().emit(std::move(diagnostic));
                    binding_errors[id] = ex.what();
                }
            }
        }
    }
    std::optional<Pair> acquire_model(const ModelAnimationRequest& request) {
        auto loaded = model_resources->acquire(request);
        if (!loaded)
            return std::nullopt;
        Pair result{loaded.skeleton->native, loaded.clip->native, request.revision,
                    request.revision,        std::move(loaded),   request.generation};
        return result;
    }
    std::optional<Pair> load(const Animator& config) {
        if (!config.skeleton.id || !config.clip.id)
            throw ArchiveError("Choose a Skeleton and Animation clip");
        const auto key = std::pair{config.skeleton.id, config.clip.id};
        if (const auto existing = model_requests.find(key); existing != model_requests.end()) {
            // Native resource requests coalesce unchanged revisions, after typed
            // member/owner validation. Removed members must not reuse an old ticket.
            auto request = model_resources->request(model_catalog, config.skeleton, config.clip);
            existing->second = std::move(request);
            return acquire_model(existing->second);
        }
        if (model_catalog && model_catalog->records().contains(config.skeleton.id) &&
            model_catalog->records().contains(config.clip.id) &&
            (model_catalog->records().at(config.skeleton.id).metadata.contains("forge.model") ||
             model_catalog->records().at(config.clip.id).metadata.contains("forge.model"))) {
            if (!model_resources)
                model_resources = std::make_unique<ModelAnimationResources>(project);
            auto request = model_resources->request(model_catalog, config.skeleton, config.clip);
            return acquire_model(model_requests.emplace(key, std::move(request)).first->second);
        }
        auto catalog = AssetCatalog::open_project(project);
        const auto model_member = [&](AssetId id) {
            const auto found = catalog.records().find(id);
            return found != catalog.records().end() && found->second.subasset &&
                   found->second.metadata.contains("forge.model");
        };
        if (model_member(config.skeleton.id) || model_member(config.clip.id)) {
            if (!model_resources)
                model_resources = std::make_unique<ModelAnimationResources>(project);
            model_catalog = std::make_shared<const AssetCatalog>(std::move(catalog));
            auto request = model_resources->request(model_catalog, config.skeleton, config.clip);
            return acquire_model(model_requests.emplace(key, std::move(request)).first->second);
        }
        auto s = catalog.resolve(config.skeleton), c = catalog.resolve(config.clip);
        if (s.state != AssetState::Available)
            throw ArchiveError("Skeleton unavailable: " + s.diagnostic);
        if (c.state != AssetState::Available)
            throw ArchiveError("Animation clip unavailable: " + c.diagnostic);
        const auto& sm = s.record->metadata;
        const auto& cm = c.record->metadata;
        validate_legacy_pair(config.skeleton.id, sm, cm);
        const RuntimeContentAccess access(project);
        if (!access.packaged())
            for (const auto* m : {&sm, &cm}) {
                const auto source = m->at("source_asset").get<AssetId>();
                const auto found = catalog.records().find(source);
                if (found == catalog.records().end() || found->second.type != "animation_source")
                    throw ArchiveError("Animation source identity is missing");
            }
        auto admit = [&](const AssetRecord& record) {
            auto data = access.read(record.source, max_archive_bytes);
            auto digest = content_digest(data);
            if (digest != record.metadata.at("artifact_sha256").get<std::string>())
                throw ArchiveError("Animation artifact digest mismatch");
            if (data.size() > 64 * 1024 * 1024 - cache_bytes || revisions.size() >= 64)
                throw ArchiveError("Animation world cache exceeds 64 MiB / 64 assets");
            return data;
        };
        auto check_revision = [&](AssetId id, const Json& metadata) {
            if (revisions.contains(id) &&
                revisions.at(id) != metadata.at("artifact_sha256").get<std::string>())
                throw ArchiveError(
                    "Animation asset changed during Play; restart Play to use the new revision");
        };
        check_revision(config.skeleton.id, sm);
        check_revision(config.clip.id, cm);
        if (!skeletons.contains(config.skeleton.id)) {
            auto data = admit(*s.record);
            auto value = std::make_shared<Skeleton>(data);
            skeletons.emplace(config.skeleton.id, std::move(value));
            revisions[config.skeleton.id] = sm.at("artifact_sha256");
            cache_bytes += data.size();
        }
        if (!clips.contains(config.clip.id)) {
            auto data = admit(*c.record);
            auto value = std::make_shared<Clip>(data);
            clips.emplace(config.clip.id, std::move(value));
            revisions[config.clip.id] = cm.at("artifact_sha256");
            cache_bytes += data.size();
        }
        return Pair{skeletons.at(config.skeleton.id), clips.at(config.clip.id),
                    sm.at("artifact_sha256"), cm.at("artifact_sha256")};
    }
    bool prepare_blend(Playback& p) {
        if ((p.config.blend_root.id || p.config.blend_mask.id) && !p.config.blend_clip.id)
            throw ArchiveError("Animator blend subtree or bone mask needs a second clip");
        p.blend_mask = subtree_mask(p.assets, p.config.blend_root.id);
        if (p.config.blend_mask.id) {
            if (!p.assets.model)
                throw ArchiveError(
                    "Animation Mask requires an imported skeleton with durable bones");
            if (!model_catalog) {
                if (!catalog_error.empty())
                    throw ArchiveError(catalog_error);
                return false;
            }
            const auto resolved = model_catalog->resolve(p.config.blend_mask);
            if (resolved.state != AssetState::Available)
                throw ArchiveError("Animation Mask unavailable: " + resolved.diagnostic);
            auto ticket = mask_requests.find(p.config.blend_mask.id);
            if (ticket == mask_requests.end())
                ticket = mask_requests
                             .emplace(p.config.blend_mask.id,
                                      request_animation_mask(mask_pool, project, *resolved.record))
                             .first;
            const auto status = ticket->second.inspect();
            if (status.state == ResourceState::Failed ||
                status.state == ResourceState::DependencyFailed)
                throw ArchiveError(status.diagnostic);
            p.mask = mask_pool.acquire(ticket->second);
            if (!p.mask)
                return false;
            if (p.mask->skeleton_revision != p.assets.skeleton_revision)
                throw ArchiveError(
                    "Animation Mask and skeleton use different model revisions; rebuild the mask");
            auto weights =
                p.mask->mask.weights(p.config.skeleton.id, p.assets.model.skeleton->joint_assets,
                                     p.assets.skeleton->info().parents);
            if (p.blend_mask.empty())
                p.blend_mask = std::move(weights);
            else
                for (std::size_t i = 0; i < weights.size(); ++i)
                    p.blend_mask[i] *= weights[i];
        }
        if (p.assets.model) {
            p.transform_channels = p.assets.model.clip->transform_channels;
            p.morph_defaults = merge_morph_defaults(p.assets.model.clip->morph_defaults, {});
        }
        if (!p.config.blend_clip.id)
            return true;
        auto second = p.config;
        second.clip = second.blend_clip;
        second.blend_clip = {};
        second.blend_weight = 0;
        auto loaded = load(second);
        if (!loaded)
            return false;
        if (loaded->skeleton != p.assets.skeleton ||
            loaded->skeleton_revision != p.assets.skeleton_revision ||
            bool(loaded->model) != bool(p.assets.model) ||
            loaded->model_generation != p.assets.model_generation)
            throw ArchiveError("Blend clips must use the same exact skeleton and model revision");
        if (loaded->model) {
            p.morph_defaults =
                merge_morph_defaults(p.morph_defaults, loaded->model.clip->morph_defaults);
            p.morph_mask = morph_subtree_mask(p, p.blend_mask);
            std::set<std::pair<std::size_t, AnimatedTransformPath>> seen;
            for (const auto& c : p.transform_channels)
                seen.emplace(c.node, c.path);
            for (const auto& c : loaded->model.clip->transform_channels)
                if (seen.emplace(c.node, c.path).second)
                    p.transform_channels.push_back(c);
        }
        p.blend_assets = std::move(*loaded);
        p.blend_sampler = std::make_unique<Sampler>(p.assets.skeleton, p.blend_assets.clip);
        return true;
    }
    void catalog(std::shared_ptr<const AssetCatalog> selected) {
        if (!selected)
            throw ArchiveError("Animation catalog notification is empty");
        std::set<flecs::entity_t> changed;
        for (const auto& [id, state] : states)
            if (state.assets.model)
                changed.insert(id);
        transforms.catalog(selected);
        catalog_error.clear();
        model_catalog = std::move(selected);
        state_requests.clear();
        mask_requests.clear();
        space_requests.clear();
        reload_pending.swap(changed);
        reload_errors.clear();
        failed_configurations.clear();
    }
    void refresh_assets() {
        if (catalog_job.valid()) {
            catalog_again = true;
            return;
        }
        catalog_job = std::async(std::launch::async, [root = project] {
            return std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        });
    }
    void poll_catalog() {
        if (!catalog_job.valid() ||
            catalog_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            catalog(catalog_job.get());
        } catch (const std::exception& e) {
            catalog_error = e.what();
            Diagnostic diagnostic{Severity::Error, "animation.catalog", e.what(), {}};
            diagnostic.context.module = "forge.animation";
            context.services().emit(std::move(diagnostic));
        }
        if (std::exchange(catalog_again, false))
            refresh_assets();
    }
    template <class Retain>
    void state_player(flecs::entity e, const Animator& requested, float fixed_dt,
                      bool preview_command, Retain retain, bool automatic = false,
                      std::optional<Playback>* prepared = nullptr) {
        auto old = states.find(e.id());
        const bool reload = reload_pending.contains(e.id());
        if (!prepared && old != states.end() && old->second.definition &&
            old->second.requested == requested && !reload) {
            if (!old->second.automatic_failure) {
                errors.erase(e.id());
                failed_configurations.erase(e.id());
            }
            return;
        }
        if (!prepared && !reload && failed_configurations.contains(e.id()) &&
            failed_configurations.at(e.id()) == requested &&
            (automatic || old == states.end() || !old->second.automatic_failure))
            return;
        try {
            detail::validate_reflected_value(e, requested);
            if (requested.blend_space.id || requested.blend_clip.id ||
                requested.blend_weight != 0 || requested.blend_root.id || requested.blend_mask.id ||
                requested.blend_mode != 0)
                throw ArchiveError(
                    "Named animation states cannot also use manual two-clip blending");
            if (!model_catalog) {
                if (!catalog_error.empty())
                    throw ArchiveError(catalog_error);
                if (old == states.end())
                    pending.insert(e.id());
                return;
            }
            const auto resolved = model_catalog->resolve(requested.states);
            if (resolved.state != AssetState::Available)
                throw ArchiveError("Animation States unavailable: " + resolved.diagnostic);
            auto ticket = state_requests.find(requested.states.id);
            if (ticket == state_requests.end())
                ticket =
                    state_requests
                        .emplace(requested.states.id,
                                 request_animation_states(state_pool, project, *resolved.record))
                        .first;
            const auto status = ticket->second.inspect();
            if (status.state == ResourceState::Failed ||
                status.state == ResourceState::DependencyFailed)
                throw ArchiveError(status.diagnostic);
            auto definition = state_pool.acquire(ticket->second);
            if (!definition) {
                if (old == states.end())
                    pending.insert(e.id());
                return;
            }
            const auto& target = definition->state(requested.state);
            if (requested.skeleton.id && requested.skeleton.id != definition->skeleton)
                throw ArchiveError("Animation States does not match the Animator skeleton");
            Animator config = requested;
            config.skeleton.id = definition->skeleton;
            config.clip = target.clip;
            config.blend_clip = {};
            config.blend_weight = 1;
            config.transition_seconds = target.transition_seconds;
            config.loop = target.loop;
            config.playback_speed *= target.speed;
            config.state = target.id;
            std::map<AssetId, Pair> clips;
            std::vector<AnimatedTransformChannel> channels;
            MorphPose morph_defaults;
            std::set<std::pair<std::size_t, AnimatedTransformPath>> seen;
            std::optional<Pair> primary;
            for (const auto& item : definition->states) {
                auto c = config;
                c.clip = item.clip;
                c.blend_weight = 0;
                retain(c);
                auto pair = load(c);
                if (!pair) {
                    if (old == states.end())
                        pending.insert(e.id());
                    return;
                }
                if (!primary)
                    primary = *pair;
                if (pair->skeleton != primary->skeleton ||
                    pair->skeleton_revision != primary->skeleton_revision ||
                    bool(pair->model) != bool(primary->model) ||
                    pair->model_generation != primary->model_generation)
                    throw ArchiveError(
                        "All animation states must use the same exact skeleton/model revision");
                if (pair->model) {
                    morph_defaults =
                        merge_morph_defaults(morph_defaults, pair->model.clip->morph_defaults);
                    for (const auto& channel : pair->model.clip->transform_channels)
                        if (seen.emplace(channel.node, channel.path).second)
                            channels.push_back(channel);
                }
                clips.emplace(item.clip.id, std::move(*pair));
            }
            const auto& selected = clips.at(config.clip.id);
            if (!prepared && old != states.end() && old->second.definition &&
                old->second.requested == requested && reload &&
                old->second.definition.identity().revision == definition.identity().revision &&
                old->second.assets.skeleton_revision == selected.skeleton_revision &&
                old->second.assets.clip_revision == selected.clip_revision) {
                old->second.automatic_failure = false;
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                reload_pending.erase(e.id());
                return;
            }
            if (old != states.end() && fixed_dt == 0 && !preview_command)
                return;
            if (!prepared && old != states.end() && old->second.definition &&
                old->second.definition.identity() == definition.identity() &&
                old->second.config.state == target.id && old->second.assets.clip == selected.clip) {
                if (old->second.config.enabled != config.enabled ||
                    old->second.config.root_motion != config.root_motion ||
                    old->second.config.root_joint != config.root_joint) {
                    old->second.discontinuous = true;
                    old->second.model_pose_applied = false;
                }
                old->second.requested = requested;
                old->second.config = config;
                if (old->second.outgoing_sampler)
                    old->second.outgoing_config.playback_speed =
                        requested.playback_speed *
                        definition->state(old->second.outgoing_config.state).speed;
                old->second.previous = old->second.time;
                old->second.advance = 0;
                old->second.outgoing_previous = old->second.outgoing_time;
                old->second.outgoing_advance = 0;
                old->second.previous_blend = old->second.blend;
                old->second.automatic_failure = false;
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                reload_pending.erase(e.id());
                return;
            }
            Playback next;
            next.config = config;
            next.requested = requested;
            next.definition = std::move(definition);
            next.clips = std::move(clips);
            next.assets = selected;
            next.transform_channels = std::move(channels);
            next.morph_defaults = std::move(morph_defaults);
            next.reference = *context.reference(e.id());
            if (old != states.end())
                next.rig_channels = former_rig_channels(old->second, next);
            next.sampler = std::make_unique<Sampler>(next.assets.skeleton, next.assets.clip);
            next.blend = next.previous_blend = next.blend_from = 1;
            next.playing = automatic || requested.play_on_start;
            if (!prepared && old != states.end() && old->second.definition &&
                old->second.definition.identity() == next.definition.identity() &&
                old->second.assets.skeleton == next.assets.skeleton &&
                old->second.assets.model_generation == next.assets.model_generation &&
                config.transition_seconds > 0) {
                next.blend = next.previous_blend = next.blend_from = 0;
                if ((old->second.definition && old->second.blend < 1) ||
                    old->second.blend_sampler) {
                    auto pose = sample_pose(old->second, old->second.time, old->second.blend, true,
                                            -1, nullptr, false);
                    next.frozen = std::move(pose.local);
                    next.frozen_morphs = std::move(pose.morphs);
                } else {
                    next.outgoing_assets = old->second.assets;
                    next.outgoing_config = old->second.config;
                    next.outgoing_config.playback_speed =
                        requested.playback_speed *
                        next.definition->state(next.outgoing_config.state).speed;
                    next.outgoing_time = next.outgoing_previous = old->second.time;
                    next.outgoing_sampler =
                        std::make_unique<Sampler>(next.assets.skeleton, next.outgoing_assets.clip);
                }
            }
            std::size_t joints = evaluated_joints(next);
            for (const auto& [other, p] : states)
                if (other != e.id())
                    joints += evaluated_joints(p);
            if (!prepared && (joints > 8192 || (old == states.end() && states.size() >= 256)))
                throw ArchiveError("Animation state transition exceeds player/joint budget");
            (void)sample_pose(next, 0, next.blend);
            if (prepared) {
                if (old == states.end())
                    throw ArchiveError("Recovery state has no admitted player");
                std::swap(old->second, next);
                try {
                    apply_model_poses(false, {{e.id(), 0}}, e.id());
                } catch (...) {
                    std::swap(old->second, next);
                    throw;
                }
                std::swap(old->second, next);
                *prepared = std::move(next);
                return;
            }
            if (old != states.end()) {
                std::swap(old->second, next);
                try {
                    apply_model_poses(false, {{e.id(), 0}}, e.id());
                } catch (...) {
                    std::swap(old->second, next);
                    throw;
                }
            } else
                states.emplace(e.id(), std::move(next));
            errors.erase(e.id());
            failed_configurations.erase(e.id());
            reload_pending.erase(e.id());
            reload_errors.erase(e.id());
        } catch (const std::exception& ex) {
            if (prepared)
                throw;
            if (errors[e.id()] != ex.what()) {
                Diagnostic d{Severity::Error, "animation.state", ex.what(), {}};
                d.context.entity = context.reference(e.id())->entity;
                d.context.module = "forge.animation";
                d.context.asset = requested.states.id;
                context.services().emit(std::move(d));
                errors[e.id()] = ex.what();
            }
            failed_configurations[e.id()] = requested;
            if (old != states.end())
                old->second.automatic_failure = automatic;
            reload_pending.erase(e.id());
        }
    }
    template <class Retain>
    void space_player(flecs::entity e, const Animator& requested, float fixed_dt, Retain retain) {
        auto old = states.find(e.id());
        const bool reload = reload_pending.contains(e.id());
        if (old != states.end() && old->second.space && old->second.requested == requested &&
            !reload) {
            errors.erase(e.id());
            failed_configurations.erase(e.id());
            return;
        }
        if (!reload && failed_configurations.contains(e.id()) &&
            failed_configurations.at(e.id()) == requested)
            return;
        try {
            detail::validate_reflected_value(e, requested);
            if (requested.states.id || requested.blend_clip.id || requested.blend_mask.id ||
                requested.blend_root.id || requested.blend_mode != 0 || requested.blend_weight != 0)
                throw ArchiveError(
                    "Animation Blend Space cannot also use named states or manual blending");
            const bool cached = !reload && old != states.end() && old->second.space &&
                                old->second.requested.blend_space == requested.blend_space;
            if (cached) {
                auto unchanged = requested;
                unchanged.blend_position = old->second.requested.blend_position;
                unchanged.blend_position_y = old->second.requested.blend_position_y;
                const auto pair = old->second.space->space.select(requested.blend_position,
                                                                  requested.blend_position_y);
                if (unchanged == old->second.requested &&
                    pair.count == old->second.space_selection.count &&
                    pair.samples == old->second.space_selection.samples) {
                    // Movement commonly changes only the weight within an admitted
                    // pair. Keep its leases, channels, samplers and cycle clock.
                    auto& p = old->second;
                    const auto previous_request = p.requested;
                    const auto previous_config = p.config;
                    const auto previous_pair = p.space_selection;
                    const auto previous_blend = p.blend, previous_previous_blend = p.previous_blend;
                    const auto previous_from = p.blend_from;
                    const auto previous_discontinuous = p.discontinuous;
                    p.requested = requested;
                    p.config.blend_position = requested.blend_position;
                    p.config.blend_position_y = requested.blend_position_y;
                    p.space_selection = pair;
                    p.config.blend_weight = pair.count > 1 ? float(pair.weights[1]) : 0;
                    p.blend = p.previous_blend = p.blend_from = p.config.blend_weight;
                    p.discontinuous = true;
                    try {
                        apply_model_poses(fixed_dt == 0, {{e.id(), p.time}}, e.id(), {}, {},
                                          nullptr, nullptr, true);
                    } catch (...) {
                        p.requested = previous_request;
                        p.config = previous_config;
                        p.space_selection = previous_pair;
                        p.blend = previous_blend;
                        p.previous_blend = previous_previous_blend;
                        p.blend_from = previous_from;
                        p.discontinuous = previous_discontinuous;
                        throw;
                    }
                    errors.erase(e.id());
                    failed_configurations.erase(e.id());
                    return;
                }
            }
            if (!model_catalog) {
                if (!catalog_error.empty())
                    throw ArchiveError(catalog_error);
                if (old == states.end())
                    pending.insert(e.id());
                return;
            }
            const auto resolved = model_catalog->resolve(requested.blend_space);
            if (resolved.state != AssetState::Available)
                throw ArchiveError("Animation Blend Space unavailable: " + resolved.diagnostic);
            auto space = cached ? old->second.space : ResourceLease<AnimationBlendSpaceAsset>{};
            if (!cached) {
                auto ticket = space_requests.find(requested.blend_space.id);
                if (ticket == space_requests.end())
                    ticket = space_requests
                                 .emplace(requested.blend_space.id,
                                          request_animation_blend_space(space_pool, project,
                                                                        *resolved.record))
                                 .first;
                const auto status = ticket->second.inspect();
                if (status.state == ResourceState::Failed ||
                    status.state == ResourceState::DependencyFailed)
                    throw ArchiveError(status.diagnostic);
                space = space_pool.acquire(ticket->second);
            }
            if (!space) {
                if (old == states.end())
                    pending.insert(e.id());
                return;
            }
            if (requested.skeleton.id && requested.skeleton.id != space->space.skeleton)
                throw ArchiveError("Animation Blend Space does not match the Animator skeleton");
            const auto selection =
                space->space.select(requested.blend_position, requested.blend_position_y);
            Playback next;
            next.requested = requested;
            next.config = requested;
            next.config.skeleton.id = space->space.skeleton;
            next.config.clip = space->space.samples[selection.samples[0]].clip;
            next.config.blend_clip = selection.count == 1
                                         ? AssetRef<AnimationClipAsset>{}
                                         : space->space.samples[selection.samples[1]].clip;
            next.config.blend_weight = selection.count > 1 ? float(selection.weights[1]) : 0;
            next.config.transition_seconds = 0;
            next.space = std::move(space);
            next.space_selection = selection;
            if (cached) {
                next.clips = old->second.clips;
                next.transform_channels = old->second.transform_channels;
                next.morph_defaults = old->second.morph_defaults;
            }
            std::optional<Pair> primary;
            for (const auto& sample : next.space->space.samples) {
                auto c = next.config;
                c.clip = sample.clip;
                c.blend_clip = {};
                c.blend_weight = 0;
                retain(c);
                if (next.clips.contains(sample.clip.id))
                    continue;
                auto pair = load(c);
                if (!pair) {
                    if (old == states.end())
                        pending.insert(e.id());
                    return;
                }
                if (!pair->model || pair->skeleton_revision != next.space->skeleton_revision)
                    throw ArchiveError(
                        "Blend Space clip and skeleton selections use different model revisions");
                if (!primary)
                    primary = *pair;
                if (pair->skeleton != primary->skeleton ||
                    pair->model_generation != primary->model_generation)
                    throw ArchiveError(
                        "All Blend Space clips must share one exact skeleton/model revision");
                next.transform_channels =
                    channel_union(next.transform_channels, pair->model.clip->transform_channels);
                next.morph_defaults =
                    merge_morph_defaults(next.morph_defaults, pair->model.clip->morph_defaults);
                next.clips.emplace(sample.clip.id, std::move(*pair));
            }
            next.assets = next.clips.at(next.config.clip.id);
            if (next.config.blend_clip.id) {
                next.blend_assets = next.clips.at(next.config.blend_clip.id);
                next.blend_sampler =
                    std::make_unique<Sampler>(next.assets.skeleton, next.blend_assets.clip);
            }
            if (selection.count > 2) {
                next.third_assets =
                    next.clips.at(next.space->space.samples[selection.samples[2]].clip.id);
                next.third_sampler =
                    std::make_unique<Sampler>(next.assets.skeleton, next.third_assets.clip);
            }
            if (reload && old != states.end() && old->second.space &&
                old->second.requested == requested &&
                old->second.space.identity() == next.space.identity() &&
                next.clips.size() == old->second.clips.size() &&
                std::all_of(next.clips.begin(), next.clips.end(), [&](const auto& entry) {
                    const auto found = old->second.clips.find(entry.first);
                    return found != old->second.clips.end() &&
                           found->second.skeleton == entry.second.skeleton &&
                           found->second.clip == entry.second.clip &&
                           found->second.model_generation == entry.second.model_generation;
                })) {
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                reload_pending.erase(e.id());
                return;
            }
            // Published assets adopt at a fixed boundary. Explicit position edits
            // may commit a complete paused pose without consuming a motion interval.
            if (reload && old != states.end() && old->second.requested == requested &&
                fixed_dt == 0)
                return;
            next.reference = *context.reference(e.id());
            next.sampler = std::make_unique<Sampler>(next.assets.skeleton, next.assets.clip);
            next.blend = next.previous_blend = next.blend_from = next.config.blend_weight;
            next.playing = requested.play_on_start;
            if (old != states.end()) {
                next.rig_channels = former_rig_channels(old->second, next);
                if (old->second.assets.skeleton_revision == next.assets.skeleton_revision &&
                    old->second.assets.model_generation == next.assets.model_generation) {
                    next.time =
                        old->second.time / playback_duration(old->second) * playback_duration(next);
                    next.previous = next.time;
                    next.playing = old->second.playing;
                }
            }
            std::size_t joints = evaluated_joints(next);
            for (const auto& [other, p] : states)
                if (other != e.id())
                    joints += evaluated_joints(p);
            if (joints > 8192 || (old == states.end() && states.size() >= 256))
                throw ArchiveError("Animation Blend Space exceeds player/joint budget");
            (void)sample_pose(next, next.time, next.blend);
            if (old != states.end()) {
                const auto target_time = next.time;
                std::swap(old->second, next);
                try {
                    apply_model_poses(fixed_dt == 0, {{e.id(), target_time}}, e.id(), {}, {},
                                      nullptr, nullptr, true);
                } catch (...) {
                    std::swap(old->second, next);
                    throw;
                }
            } else
                states.emplace(e.id(), std::move(next));
            errors.erase(e.id());
            failed_configurations.erase(e.id());
            reload_pending.erase(e.id());
            reload_errors.erase(e.id());
        } catch (const std::exception& ex) {
            if (errors[e.id()] != ex.what()) {
                Diagnostic d{Severity::Error, "animation.blend_space", ex.what(), {}};
                d.context.entity = context.reference(e.id())->entity;
                d.context.module = "forge.animation";
                d.context.asset = requested.blend_space.id;
                context.services().emit(std::move(d));
                errors[e.id()] = ex.what();
            }
            failed_configurations[e.id()] = requested;
            reload_pending.erase(e.id());
        }
    }
    void synchronize(float fixed_dt = 0, bool preview_command = false) {
        bool needs_catalog = transforms.has_players();
        if (!model_catalog && !initial_catalog_requested)
            context.world().each([&](const Animator& c) {
                needs_catalog |=
                    bool(c.states.id) || bool(c.blend_mask.id) || bool(c.blend_space.id);
            });
        if (!model_catalog && needs_catalog && !initial_catalog_requested) {
            initial_catalog_requested = true;
            refresh_assets();
        }
        poll_catalog();
        state_pool.pump();
        mask_pool.pump();
        space_pool.pump();
        if (model_resources)
            model_resources->pump();
        pending.clear();
        std::set<flecs::entity_t> alive;
        std::set<std::pair<AssetId, AssetId>> used;
        std::set<AssetId> used_skeletons, used_clips, used_states, used_masks, used_spaces;
        auto retain = [&](const Animator& config) {
            if (config.blend_mask.id)
                used_masks.insert(config.blend_mask.id);
            if (config.blend_space.id)
                used_spaces.insert(config.blend_space.id);
            used.insert({config.skeleton.id, config.clip.id});
            used_skeletons.insert(config.skeleton.id);
            used_clips.insert(config.clip.id);
            if (config.blend_clip.id) {
                used.insert({config.skeleton.id, config.blend_clip.id});
                used_clips.insert(config.blend_clip.id);
            }
        };
        context.world().each([&](flecs::entity e, const Animator& config) {
            if (e.has(flecs::Prefab) || !context.reference(e.id()))
                return;
            alive.insert(e.id());
            if (auto old = states.find(e.id()); old != states.end()) {
                retain(old->second.config);
                for (const auto& [clip, pair] : old->second.clips) {
                    auto c = old->second.config;
                    c.clip.id = clip;
                    retain(c);
                }
            }
            if (config.blend_space.id) {
                used_spaces.insert(config.blend_space.id);
                space_player(e, config, fixed_dt, retain);
                if (auto current = states.find(e.id()); current != states.end())
                    for (const auto& [clip, pair] : current->second.clips) {
                        auto c = current->second.config;
                        c.clip.id = clip;
                        retain(c);
                    }
                return;
            }
            if (config.states.id)
                used_states.insert(config.states.id);
            if (auto previous = states.find(e.id());
                previous != states.end() && previous->second.definition)
                used_states.insert(previous->second.requested.states.id);
            if (config.states.id) {
                auto old = states.find(e.id());
                if (old != states.end()) {
                    retain(old->second.config);
                    for (const auto& [clip, pair] : old->second.clips) {
                        auto c = old->second.config;
                        c.clip.id = clip;
                        retain(c);
                    }
                }
                state_player(e, config, fixed_dt, preview_command, retain);
                auto current = states.find(e.id());
                if (current != states.end())
                    for (const auto& [clip, pair] : current->second.clips) {
                        auto c = current->second.config;
                        c.clip.id = clip;
                        retain(c);
                    }
                return;
            }
            retain(config);
            auto old = states.find(e.id());
            if (old != states.end())
                retain(old->second.config);
            const bool reload = old != states.end() && old->second.config == config &&
                                reload_pending.contains(e.id());
            const bool replace_mask = old != states.end() && !old->second.definition &&
                                      !old->second.space &&
                                      old->second.config.skeleton == config.skeleton &&
                                      old->second.config.clip == config.clip &&
                                      old->second.config.blend_clip == config.blend_clip &&
                                      (old->second.config.blend_mask != config.blend_mask ||
                                       old->second.config.blend_root != config.blend_root);
            if (old != states.end() && old->second.config == config && !reload) {
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                reload_pending.erase(e.id());
                return;
            }
            if (failed_configurations.contains(e.id()) &&
                failed_configurations.at(e.id()) == config)
                return;
            try {
                detail::validate_reflected_value(e, config);
                if (!config.blend_clip.id && config.blend_weight != 0)
                    throw ArchiveError("Animator blend target needs a second clip");
                if (config.blend_root.id && !config.blend_clip.id)
                    throw ArchiveError("Animator blend subtree needs a second clip");
                if (config.blend_mode > 1 || (config.blend_mode != 0 && !config.blend_clip.id))
                    throw ArchiveError("Animator blend mode is invalid or needs a second clip");
                if (reload || replace_mask) {
                    auto assets = load(config);
                    if (!assets)
                        return; // Continue the previous good playback while loading.
                    if (fixed_dt == 0 && !replace_mask)
                        return; // Pause may prepare resources, but cannot apply a new pose.
                    Playback next;
                    next.config = config;
                    next.reference = old->second.reference;
                    next.assets = std::move(*assets);
                    next.rig_channels = former_rig_channels(old->second, next);
                    if (!prepare_blend(next))
                        return;
                    const auto same = [](const Pair& a, const Pair& b) {
                        return a.skeleton == b.skeleton && a.clip == b.clip &&
                               a.model_generation == b.model_generation;
                    };
                    const bool same_mask =
                        bool(next.mask) == bool(old->second.mask) &&
                        (!next.mask || next.mask.identity() == old->second.mask.identity());
                    if (same(next.assets, old->second.assets) &&
                        same(next.blend_assets, old->second.blend_assets) && same_mask &&
                        next.config == old->second.config) {
                        reload_pending.erase(e.id());
                        return;
                    }
                    next.blend = old->second.blend;
                    next.previous_blend = next.blend;
                    next.blend_from = old->second.blend_from;
                    next.blend_elapsed = old->second.blend_elapsed;
                    if (next.config.blend_weight != old->second.config.blend_weight ||
                        next.config.transition_seconds != old->second.config.transition_seconds) {
                        next.blend_from = next.blend;
                        next.blend_elapsed = 0;
                        if (next.config.transition_seconds == 0)
                            next.blend = next.previous_blend = next.config.blend_weight;
                    }
                    std::size_t joints = evaluated_joints(next);
                    for (const auto& [other, state] : states)
                        if (other != e.id())
                            joints += evaluated_joints(state);
                    if (joints > 8192)
                        throw ArchiveError("Animation replacement exceeds evaluated-joint budget");
                    next.sampler =
                        std::make_unique<Sampler>(next.assets.skeleton, next.assets.clip);
                    const auto duration = next.assets.clip->info().duration;
                    next.time = config.loop ? std::fmod(old->second.time, duration)
                                            : std::min(old->second.time, double(duration));
                    next.previous = next.time;
                    next.playing = old->second.playing && (config.loop || next.time < duration);
                    (void)sample_pose(next, next.time, next.blend);
                    double target_time = next.time;
                    if (next.config.enabled && next.playing) {
                        target_time += double(fixed_dt) * config.playback_speed;
                        target_time = config.loop ? std::fmod(target_time, duration)
                                                  : std::min(target_time, double(duration));
                    }
                    // Temporary owner-local candidate, never published or sampled externally.
                    // Validate transform/physics compatibility before adopting either resource.
                    std::swap(old->second, next);
                    try {
                        // Between ticks an explicit edit must publish its complete pose now.
                        // During a tick this remains preflight; the Animation stage commits it.
                        apply_model_poses(fixed_dt == 0, {{e.id(), target_time}}, e.id(), {}, {},
                                          nullptr, nullptr, true);
                    } catch (...) {
                        std::swap(old->second, next);
                        throw;
                    }
                    reload_pending.erase(e.id());
                    reload_errors.erase(e.id());
                } else if (old != states.end() && old->second.config.skeleton == config.skeleton &&
                           old->second.config.clip == config.clip &&
                           old->second.config.blend_clip == config.blend_clip &&
                           !old->second.definition && !old->second.space) {
                    if (old->second.config.enabled != config.enabled ||
                        old->second.config.root_motion != config.root_motion ||
                        old->second.config.root_joint != config.root_joint ||
                        old->second.config.blend_mode != config.blend_mode) {
                        old->second.discontinuous = true;
                        old->second.model_pose_applied = false;
                    }
                    if (old->second.config.blend_weight != config.blend_weight ||
                        old->second.config.transition_seconds != config.transition_seconds) {
                        old->second.blend_from = old->second.blend;
                        old->second.blend_elapsed = 0;
                        if (config.transition_seconds == 0) {
                            old->second.blend = config.blend_weight;
                            old->second.discontinuous = true;
                            old->second.model_pose_applied = false;
                        }
                    }
                    old->second.previous_blend = old->second.blend;
                    old->second.config = config;
                    old->second.previous = old->second.time;
                    old->second.advance = 0;
                } else {
                    Playback next;
                    next.config = config;
                    next.reference = *context.reference(e.id());
                    auto assets = load(config);
                    if (!assets) {
                        if (old == states.end())
                            pending.insert(e.id());
                        return;
                    }
                    next.assets = std::move(*assets);
                    if (!prepare_blend(next)) {
                        if (old == states.end())
                            pending.insert(e.id());
                        return;
                    }
                    next.blend = next.previous_blend = next.blend_from = config.blend_weight;
                    std::size_t joints = evaluated_joints(next);
                    for (const auto& [other, state] : states)
                        if (other != e.id())
                            joints += evaluated_joints(state);
                    if (joints > 8192 || (old == states.end() && states.size() >= 256))
                        throw ArchiveError(
                            "Animation world exceeds 256 players / 8192 evaluated joints");
                    next.sampler =
                        std::make_unique<Sampler>(next.assets.skeleton, next.assets.clip);
                    (void)sample_pose(next, 0, next.blend);
                    next.playing = config.play_on_start;
                    states.insert_or_assign(e.id(), std::move(next));
                }
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                reload_pending.erase(e.id());
            } catch (const std::exception& ex) {
                if (reload) {
                    reload_pending.erase(e.id());
                    reload_errors[e.id()] = ex.what();
                    Diagnostic d{Severity::Error, "animation.reload", ex.what(), {}};
                    d.context.entity = context.reference(e.id())->entity;
                    d.context.module = "forge.animation";
                    d.context.asset = config.clip.id;
                    context.services().emit(std::move(d));
                    return; // Last-good playback remains active; retry on a new notification.
                }
                if (errors[e.id()] != ex.what()) {
                    Diagnostic d{Severity::Error, "animation.asset", ex.what(), {}};
                    d.context.entity = context.reference(e.id())->entity;
                    d.context.module = "forge.animation";
                    if (config.clip.id)
                        d.context.asset = config.clip.id;
                    context.services().emit(std::move(d));
                    errors[e.id()] = ex.what();
                }
                failed_configurations[e.id()] = config;
                // No replacement assets are adopted. Previous valid object remains owned,
                // but a mismatching authored configuration is not sampled as if it succeeded.
            }
        });
        for (auto it = model_requests.begin(); it != model_requests.end();) {
            if (used.contains(it->first)) {
                ++it;
                continue;
            }
            if (!used_clips.contains(it->first.second))
                model_resources->unload_clip(it->second.clip);
            if (!used_skeletons.contains(it->first.first))
                model_resources->unload_skeleton(it->second.skeleton);
            it = model_requests.erase(it);
        }
        for (auto it = state_requests.begin(); it != state_requests.end();) {
            if (used_states.contains(it->first)) {
                ++it;
                continue;
            }
            state_pool.unload(AssetRef<AnimationStatesAsset>{it->first});
            it = state_requests.erase(it);
        }
        for (auto it = mask_requests.begin(); it != mask_requests.end();) {
            if (used_masks.contains(it->first)) {
                ++it;
                continue;
            }
            mask_pool.unload(AssetRef<AnimationMaskAsset>{it->first});
            it = mask_requests.erase(it);
        }
        for (auto it = space_requests.begin(); it != space_requests.end();) {
            if (used_spaces.contains(it->first)) {
                ++it;
                continue;
            }
            space_pool.unload(AssetRef<AnimationBlendSpaceAsset>{it->first});
            it = space_requests.erase(it);
        }
        std::erase_if(states, [&](const auto& entry) { return !alive.contains(entry.first); });
        std::erase_if(errors, [&](const auto& entry) { return !alive.contains(entry.first); });
        std::erase_if(binding_errors,
                      [&](const auto& entry) { return !alive.contains(entry.first); });
        std::erase_if(failed_configurations,
                      [&](const auto& entry) { return !alive.contains(entry.first); });
        std::erase_if(reload_pending, [&](auto id) { return !alive.contains(id); });
        std::erase_if(reload_errors,
                      [&](const auto& entry) { return !alive.contains(entry.first); });
        transforms.catalog(model_catalog);
        transforms.synchronize();
    }
};
AnimationRuntime::AnimationRuntime(WorldContext& c, std::filesystem::path p)
    : impl_(std::make_unique<Impl>(c, std::move(p))) {}
AnimationRuntime::~AnimationRuntime() = default;
void AnimationRuntime::pose_validator(PoseValidator validator) {
    if (impl_) {
        impl_->transforms.pose_validator(validator);
        impl_->pose_validator = std::move(validator);
    }
}
void AnimationRuntime::shutdown() noexcept { impl_.reset(); }
void AnimationRuntime::synchronize() {
    if (impl_)
        impl_->synchronize();
}
bool AnimationRuntime::prepare_initial_pose() {
    if (!impl_)
        throw ArchiveError("Animation runtime is unavailable");
    impl_->synchronize();
    if (impl_->transforms.has_players() && !impl_->catalog_error.empty())
        throw ArchiveError("Transform animation catalog: " + impl_->catalog_error);
    if (!impl_->errors.empty())
        throw ArchiveError("Scene preparation: invalid Animator asset configuration: " +
                           impl_->errors.begin()->second);
    if (!impl_->pending.empty())
        return false;
    impl_->apply_model_poses();
    if (!impl_->binding_errors.empty())
        throw ArchiveError("Scene preparation: invalid Animator model binding: " +
                           impl_->binding_errors.begin()->second);
    return impl_->transforms.initial_pose();
}
void AnimationRuntime::prepare_preview_state() {
    if (!impl_)
        throw ArchiveError("Animation runtime is unavailable");
    impl_->synchronize(0, true);
    (void)prepare_initial_pose();
}
void AnimationRuntime::refresh_assets() {
    if (impl_)
        impl_->refresh_assets();
}
void AnimationRuntime::catalog(std::shared_ptr<const AssetCatalog> selected) {
    if (impl_)
        impl_->catalog(std::move(selected));
}
void AnimationRuntime::tick(float dt, std::uint64_t tick) {
    if (!impl_)
        return;
    if (!std::isfinite(dt) || dt <= 0)
        throw ArchiveError("Animation requires a positive fixed delta");
    impl_->last_animation_tick = tick;
    impl_->root_motions.clear();
    impl_->events.clear();
    impl_->event_candidates.clear();
    auto profile = impl_->context.services().profile("animation", "FixedPlayback");
    impl_->synchronize(dt);
    for (auto& [id, p] : impl_->states) {
        p.tick_begin = p.previous = p.time;
        p.tick_advance = 0;
        p.previous_blend = p.blend;
        p.previous_rig = p.rig;
        p.advance = 0;
        p.outgoing_previous = p.outgoing_time;
        p.outgoing_advance = 0;
        if (!p.config.enabled || !p.playing || impl_->pending.contains(id))
            continue;
        if (p.config.playback_speed > 0 && p.blend != p.config.blend_weight) {
            p.blend_elapsed = std::min(double(p.config.transition_seconds), p.blend_elapsed + dt);
            const double ratio =
                p.config.transition_seconds > 0 ? p.blend_elapsed / p.config.transition_seconds : 1;
            p.blend = p.blend_from + (p.config.blend_weight - p.blend_from) * ratio;
        }
        if (p.definition && p.outgoing_sampler && p.config.playback_speed > 0 &&
            p.previous_blend < 1) {
            p.outgoing_advance = double(dt) * p.outgoing_config.playback_speed;
            p.outgoing_time += p.outgoing_advance;
            const auto end = p.outgoing_assets.clip->info().duration;
            p.outgoing_time = p.outgoing_config.loop ? std::fmod(p.outgoing_time, end)
                                                     : std::min(p.outgoing_time, double(end));
        }
        if (p.definition && p.previous_blend >= 1 && p.blend >= 1) {
            p.outgoing_sampler.reset();
            p.outgoing_assets = {};
            p.outgoing_time = p.outgoing_previous = p.outgoing_advance = 0;
            p.frozen.clear();
            p.frozen_morphs.clear();
        }
        auto duration = playback_duration(p);
        p.advance = double(dt) * p.config.playback_speed;
        p.time = p.previous + p.advance;
        if (p.config.loop)
            p.time = std::fmod(p.time, duration);
        else if (p.time >= duration) {
            p.time = duration;
            p.advance = p.time - p.previous;
            p.playing = p.definition && p.blend < p.config.blend_weight;
        }
        p.tick_advance = p.advance;
    }
    impl_->apply_model_poses(true, {}, 0, {}, tick);
    impl_->transforms.tick(dt);
    if (impl_->deliver_events) {
        try {
            for (auto& [id, p] : impl_->states) {
                if (!p.definition || !p.config.enabled || p.tick_advance <= 0 ||
                    impl_->pending.contains(id) || impl_->binding_errors.contains(id))
                    continue;
                append_markers(p, tick, impl_->event_candidates);
            }
            std::sort(impl_->event_candidates.begin(), impl_->event_candidates.end(),
                      [](const auto& a, const auto& b) {
                          return std::tie(a.offset, a.entity, a.marker) <
                                 std::tie(b.offset, b.entity, b.marker);
                      });
            impl_->events.swap(impl_->event_candidates);
        } catch (const std::exception& ex) {
            impl_->event_candidates.clear();
            Diagnostic d{Severity::Error, "animation.markers", ex.what(), {}};
            d.context.tick = tick;
            d.context.module = "forge.animation";
            impl_->context.services().emit(std::move(d));
            throw;
        }
    }
    for (auto& [id, p] : impl_->states)
        if (p.config.enabled && p.tick_advance > 0 && !impl_->pending.contains(id) &&
            !impl_->binding_errors.contains(id))
            p.entry_pending = false;
    // Publish this tick's source markers before adopting a target at time zero.
    // All state clips were retained/admitted by synchronize; one rule per tick.
    for (auto& [id, p] : impl_->states) {
        if (!p.definition || !p.config.enabled || p.tick_advance <= 0 ||
            impl_->pending.contains(id) || impl_->binding_errors.contains(id))
            continue;
        const auto& state = p.definition->state(p.config.state);
        if (!state.exit_state || p.tick_begin + p.tick_advance <
                                     double(state.exit_phase) * p.assets.clip->info().duration)
            continue;
        auto requested = p.requested;
        requested.state = state.exit_state;
        auto entity = impl_->context.world().entity(id);
        impl_->state_player(entity, requested, dt, false, [](const Animator&) {}, true);
        if (p.requested == requested && p.config.state == requested.state)
            entity.set<Animator>(requested);
    }
}
std::span<const AnimationEvent> AnimationRuntime::events() const {
    return impl_ ? std::span<const AnimationEvent>(impl_->events)
                 : std::span<const AnimationEvent>();
}
std::optional<AnimationPlayback> AnimationRuntime::playback(EntityRef reference) const {
    if (!impl_ || !impl_->context.on_owner_thread())
        return {};
    if (const auto transform = impl_->transforms.playback(reference))
        return transform;
    const auto resolved = impl_->context.resolve(reference);
    if (resolved.state != WorldContext::ResolveState::Available ||
        !impl_->context.world().entity(resolved.entity).has<Animator>() ||
        impl_->pending.contains(resolved.entity) || impl_->binding_errors.contains(resolved.entity))
        return {};
    const auto found = impl_->states.find(resolved.entity);
    if (found == impl_->states.end())
        return {};
    const auto& p = found->second;
    AnimationPlayback result;
    result.entity = reference;
    result.states = p.requested.states.id;
    result.clip = p.config.clip.id;
    result.blend_clip = p.config.blend_clip.id;
    result.time = p.time;
    result.duration = playback_duration(p);
    result.progress = std::clamp(result.time / result.duration, 0., 1.);
    result.speed = p.config.playback_speed;
    result.enabled = p.config.enabled;
    result.loop = p.config.loop;
    result.playing = p.config.enabled && p.playing && p.config.playback_speed > 0;
    result.finished = !p.config.loop && p.time >= result.duration;
    if (p.definition) {
        result.state = p.config.state;
        result.state_name = p.definition->state(p.config.state).name;
        result.transition_weight = p.blend;
        result.transitioning = p.blend < 1 && (p.outgoing_sampler || !p.frozen.empty());
    } else {
        result.blend_weight = p.blend;
    }
    return result;
}
std::optional<AnimationRootMotion> AnimationRuntime::root_motion(EntityRef reference) const {
    if (!impl_ || !impl_->context.on_owner_thread())
        return {};
    const auto resolved = impl_->context.resolve(reference);
    if (resolved.state != WorldContext::ResolveState::Available ||
        !impl_->context.world().entity(resolved.entity).has<Animator>() ||
        impl_->pending.contains(resolved.entity) || impl_->binding_errors.contains(resolved.entity))
        return {};
    const auto found = impl_->root_motions.find(resolved.entity);
    if (found == impl_->root_motions.end())
        return {};
    return found->second;
}
bool AnimationRuntime::control(EntityRef reference, AnimationControl command,
                               std::uint64_t current_tick) {
    if (command != AnimationControl::Play && command != AnimationControl::Pause &&
        command != AnimationControl::Restart)
        throw ArchiveError("Unknown animation playback control");
    if (!impl_ || !impl_->context.on_owner_thread() ||
        (impl_->last_animation_tick && current_tick <= *impl_->last_animation_tick))
        return false;
    if (impl_->transforms.playback(reference))
        return impl_->transforms.control(reference, command);
    if (!playback(reference))
        return false;
    const auto entity = impl_->context.resolve(reference).entity;
    auto& p = impl_->states.at(entity);
    if (command == AnimationControl::Pause) {
        p.playing = false;
        return true;
    }
    if (command == AnimationControl::Play) {
        if (!p.config.loop && p.time >= playback_duration(p) &&
            !(p.definition && p.blend < p.config.blend_weight))
            return false;
        p.playing = true;
        return true;
    }
    p.time = p.previous = p.advance = p.tick_begin = p.tick_advance = 0;
    p.blend = p.previous_blend = p.blend_from = p.config.blend_weight;
    p.blend_elapsed = 0;
    p.outgoing_sampler.reset();
    p.outgoing_assets = {};
    p.outgoing_config = {};
    p.outgoing_time = p.outgoing_previous = p.outgoing_advance = 0;
    p.frozen.clear();
    p.frozen_morphs.clear();
    p.sampler->reset();
    if (p.blend_sampler)
        p.blend_sampler->reset();
    p.playing = p.entry_pending = p.discontinuous = true;
    p.model_pose_applied = false;
    return true;
}
void AnimationRuntime::event_delivery(bool enabled) {
    if (impl_) {
        impl_->deliver_events = enabled;
        impl_->events.clear();
    }
}
bool AnimationRuntime::seek(EntityRef reference, double seconds, std::uint64_t current_tick) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw ArchiveError("Animation seek requires finite nonnegative seconds");
    if (!impl_ || !impl_->context.on_owner_thread() ||
        (impl_->last_animation_tick && current_tick <= *impl_->last_animation_tick))
        return false;
    if (impl_->transforms.playback(reference))
        return impl_->transforms.seek(reference, seconds);
    const auto status = playback(reference);
    if (!status)
        return false;
    if (seconds > status->duration)
        throw ArchiveError("Animation seek exceeds the admitted incoming duration");
    const auto entity = impl_->context.resolve(reference).entity;
    auto& p = impl_->states.at(entity);
    const auto time = p.config.loop && seconds == status->duration ? 0. : seconds;
    p.time = p.previous = p.tick_begin = time;
    p.advance = p.tick_advance = 0;
    p.previous_blend = p.blend;
    p.entry_pending = false;
    p.model_pose_applied = false;
    p.discontinuous = true;
    p.sampler->reset();
    if (p.blend_sampler)
        p.blend_sampler->reset();
    p.root_samples.clear();
    impl_->root_motions.erase(entity);
    return true;
}
Json AnimationPlaybackAccess::status(AnimationRuntime& runtime, EntityRef ref) {
    const auto value = runtime.playback(ref);
    Json result{{"source", ref}, {"available", bool(value)}};
    if (value) {
        result.update({{"time", value->time},
                       {"sequence_controller", !value->clip},
                       {"duration", value->duration},
                       {"progress", value->progress},
                       {"speed", value->speed},
                       {"enabled", value->enabled},
                       {"loop", value->loop},
                       {"playing", value->playing},
                       {"finished", value->finished},
                       {"state_name", value->state_name},
                       {"transitioning", value->transitioning},
                       {"transition_weight", value->transition_weight},
                       {"blend_weight", value->blend_weight}});
    }
    return result;
}
void AnimationPlaybackAccess::control(AnimationRuntime& runtime, EntityRef ref,
                                      const Json& command) {
    if (!runtime.impl_ || !runtime.impl_->context.on_owner_thread() || !runtime.playback(ref))
        throw ArchiveError("Animation playback requires an admitted owner-thread player");
    const auto operation = command.at("operation").get<std::string>();
    const auto last = runtime.impl_->last_animation_tick.value_or(0);
    if (last == std::numeric_limits<std::uint64_t>::max())
        throw ArchiveError("Animation playback tick space is exhausted");
    const auto id = runtime.impl_->context.resolve(ref).entity;
    if (runtime.impl_->transforms.playback(ref)) {
        bool accepted = false;
        if (operation == "seek") {
            if (!command.at("position_seconds").is_number())
                throw ArchiveError("Animation seek requires numeric seconds");
            accepted = runtime.seek(ref, command.at("position_seconds").get<double>(), last + 1);
        } else if (operation == "play" || operation == "pause" || operation == "restart") {
            accepted = runtime.control(ref,
                                       operation == "play"    ? AnimationControl::Play
                                       : operation == "pause" ? AnimationControl::Pause
                                                              : AnimationControl::Restart,
                                       last + 1);
        } else
            throw ArchiveError("Unknown animation playback operation");
        if (!accepted)
            throw ArchiveError("Animation playback request is unavailable");
        if (operation == "seek" || operation == "restart")
            (void)runtime.impl_->transforms.initial_pose();
        return;
    }
    bool accepted = false;
    if (operation == "seek") {
        if (!command.at("position_seconds").is_number())
            throw ArchiveError("Animation seek requires numeric seconds");
        const auto seconds = command.at("position_seconds").get<double>();
        const auto status = *runtime.playback(ref);
        if (!std::isfinite(seconds) || seconds < 0 || seconds > status.duration)
            throw ArchiveError("Animation seek requires seconds within its incoming duration");
        const auto time = status.loop && seconds == status.duration ? 0. : seconds;
        // Validate a temporary sample before changing the authoritative clock.
        runtime.impl_->apply_model_poses(false, {{id, time}}, id);
        accepted = runtime.seek(ref, seconds, last + 1);
    } else {
        AnimationControl op;
        if (operation == "play")
            op = AnimationControl::Play;
        else if (operation == "pause")
            op = AnimationControl::Pause;
        else if (operation == "restart") {
            op = AnimationControl::Restart;
            const auto& player = runtime.impl_->states.at(id);
            runtime.impl_->apply_model_poses(false, {{id, 0}}, id,
                                             {{id, player.config.blend_weight}});
        } else
            throw ArchiveError("Unknown animation playback operation");
        accepted = runtime.control(ref, op, last + 1);
    }
    if (!accepted)
        throw ArchiveError("Animation playback request is unavailable");
    if (operation == "seek" || operation == "restart") {
        runtime.impl_->apply_model_poses(true, {}, id);
    }
}
Json AnimationPlaybackAccess::tune_rig(AnimationRuntime& runtime, EntityRef ref,
                                       const std::string& type, const std::string& property,
                                       const Json& value) {
    if (!rig_type(type) || !runtime.impl_ || runtime.impl_->context.role() != WorldRole::Runtime ||
        !runtime.impl_->context.on_owner_thread())
        throw ArchiveError("Live IK tuning requires the runtime animation owner");
    const auto resolved = runtime.impl_->context.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        throw ArchiveError("Live IK target is unavailable");
    auto root = runtime.impl_->context.world().entity(resolved.entity);
    const auto player = runtime.impl_->states.find(root.id());
    if (root.has(flecs::Prefab) || !root.has<ModelSource>() || root.get<ModelSource>().node.id ||
        player == runtime.impl_->states.end() || runtime.impl_->pending.contains(root.id()) ||
        !player->second.config.enabled)
        throw ArchiveError("Live IK tuning needs an admitted model Animator holder");
    const auto& catalog = detail::builtins();
    const auto codec = std::find_if(catalog.begin(), catalog.end(),
                                    [&](const auto& entry) { return type == entry.name; });
    auto candidate = codec->read(root, true);
    if (!candidate.is_object() || !candidate.contains(property))
        throw ArchiveError("Live IK component or property is unavailable");
    candidate[property] = value;
    detail::validate_components(Json{{type, candidate}});
    const auto typed = codec->decode(candidate);
    RigInputs inputs;
    if (root.has<TwoBoneIK>())
        inputs.reach = root.get<TwoBoneIK>();
    if (root.has<AimIK>())
        inputs.aim = root.get<AimIK>();
    if (type == "forge.two_bone_ik") {
        inputs.reach = std::get<TwoBoneIK>(typed);
        detail::validate_reflected_value(root, *inputs.reach);
    } else {
        inputs.aim = std::get<AimIK>(typed);
        detail::validate_reflected_value(root, *inputs.aim);
    }
    runtime.impl_->apply_model_poses(true, {}, root.id(), {}, {}, nullptr, &inputs);
    return codec->read(root, true).at(property);
}
void AnimationRuntime::reset_presentation() {
    if (!impl_)
        return;
    impl_->synchronize();
    impl_->discontinuities.clear();
    impl_->transforms.reset_presentation();
    for (auto& [id, p] : impl_->states) {
        (void)id;
        p.previous = p.time;
        p.advance = 0;
        p.previous_blend = p.blend;
        p.previous_rig = p.rig;
        p.outgoing_previous = p.outgoing_time;
        p.outgoing_advance = 0;
        p.sampler->reset();
        if (p.blend_sampler)
            p.blend_sampler->reset();
    }
}
Json AnimationRuntime::presentation_properties(flecs::entity_t id, double alpha) const {
    return impl_ ? impl_->transforms.presentation_properties(id, alpha) : Json(nullptr);
}
Json AnimationRuntime::presentation(flecs::entity_t id, double alpha) {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw ArchiveError("Invalid animation presentation alpha");
    if (!impl_ || !impl_->states.contains(id) || impl_->binding_errors.contains(id) ||
        impl_->pending.contains(id))
        return nullptr;
    auto profile = impl_->context.services().profile("animation", "SampleAndLocalToModel");
    auto& p = impl_->states.at(id);
    if (!p.config.enabled)
        return nullptr;
    const auto duration = playback_duration(p);
    auto time = p.previous + p.advance * alpha;
    if (p.config.loop)
        time = std::fmod(time, duration);
    double weight = p.previous_blend + (p.blend - p.previous_blend) * alpha;
    auto outgoing_time = p.outgoing_previous + p.outgoing_advance * alpha;
    if (p.outgoing_sampler) {
        const auto end = p.outgoing_assets.clip->info().duration;
        outgoing_time = p.outgoing_config.loop ? std::fmod(outgoing_time, end)
                                               : std::min(outgoing_time, double(end));
    }
    const auto rig = interpolate_rig(p.previous_rig, p.rig, alpha);
    auto pose = [&] {
        try {
            return sample_pose(p, time, weight, false, outgoing_time, &rig);
        } catch (const ArchiveError&) {
            if (alpha <= 0 || alpha >= 1 || (!p.rig.reach && !p.rig.aim))
                throw;
            // Valid fixed endpoints can bracket an unavailable intermediate
            // inverse or bend plane. Present the admitted current fixed sample;
            // never turn a smoothing failure into an invalid gameplay write.
            time = p.time;
            weight = p.blend;
            outgoing_time = p.outgoing_time;
            return sample_pose(p, time, weight, false, outgoing_time, &p.rig);
        }
    }();
    auto locals = Json::array();
    for (const auto& local : pose.local)
        locals.push_back({{"translation", local.translation},
                          {"rotation", local.rotation},
                          {"scale", local.scale}});
    auto morphs = Json::array();
    if (p.assets.model)
        for (const auto& morph : pose.morphs)
            morphs.push_back({{"node", morph.node}, {"weights", morph.weights}});
    Json result = {{"parents", p.assets.skeleton->info().parents},
                   {"model", pose.model},
                   {"time", time},
                   {"duration", duration},
                   {"playing", p.playing},
                   {"clip", p.config.clip.id},
                   {"blend_clip", p.config.blend_clip.id ? Json(p.config.blend_clip.id) : Json()},
                   {"blend_weight", weight},
                   {"local", std::move(locals)},
                   {"morphs", std::move(morphs)}};
    if (p.definition) {
        result["state"] = p.config.state;
        result["state_name"] = p.definition->state(p.config.state).name;
        result["states"] = p.requested.states.id;
    }
    if (p.space) {
        const auto pair = p.space_selection;
        result["blend_space"] = p.requested.blend_space.id;
        result["blend_position"] = p.requested.blend_position;
        result["first_sample"] = p.space->space.samples[pair.samples[0]].id;
        result["second_sample"] = p.space->space.samples[pair.samples[pair.count > 1 ? 1 : 0]].id;
        if (p.space->space.dimensions == 2) {
            result["blend_dimensions"] = 2;
            result["blend_position_y"] = p.requested.blend_position_y;
            result["blend_samples"] = space_selection_json(p);
        }
    }
    if (p.assets.model) {
        result["model_asset"] = p.assets.model.skeleton->model;
        result["model_revision"] = p.assets.skeleton_revision;
        result["model_generation"] = p.assets.model_generation;
        result["joint_nodes"] = p.assets.model.skeleton->joint_nodes;
        result["joint_assets"] = p.assets.model.skeleton->joint_assets;
        if (p.assets.model.clip->has_transform_channels) {
            auto channels = Json::array();
            for (const auto& channel : animated_channels(p))
                channels.push_back(
                    {{"node", channel.node},
                     {"path", channel.path == AnimatedTransformPath::Translation ? "translation"
                              : channel.path == AnimatedTransformPath::Rotation  ? "rotation"
                                                                                 : "scale"}});
            result["transform_channels"] = std::move(channels);
        }
    }
    return result;
}
bool AnimationRuntime::model_pose_ready(flecs::entity_t id) const {
    if (!impl_ || impl_->pending.contains(id) || impl_->binding_errors.contains(id))
        return false;
    const auto found = impl_->states.find(id);
    return found != impl_->states.end() && found->second.config.enabled &&
           found->second.model_pose_applied;
}
std::vector<std::uint64_t> AnimationRuntime::take_discontinuities() {
    if (!impl_)
        return {};
    std::vector<std::uint64_t> result(impl_->discontinuities.begin(), impl_->discontinuities.end());
    impl_->discontinuities.clear();
    const auto transforms = impl_->transforms.take_discontinuities();
    result.insert(result.end(), transforms.begin(), transforms.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
bool AnimationRuntime::checkpoint_ready() const {
    if (!impl_)
        return true;
    if (!impl_->pending.empty() || !impl_->errors.empty() || !impl_->binding_errors.empty() ||
        !impl_->transforms.checkpoint_ready())
        return false;
    for (const auto& [id, p] : impl_->states) {
        auto e = impl_->context.world().entity(id);
        if (!e.has<Animator>() ||
            e.get<Animator>() != ((p.definition || p.space) ? p.requested : p.config))
            return false;
    }
    return true;
}
Json AnimationRuntime::checkpoint() const {
    // A partial binding is not a recoverable playback state. Runtime IPC preserves
    // the null marker until all selected model resources have been adopted.
    if (impl_ && (!impl_->pending.empty() || !impl_->transforms.checkpoint_ready()))
        return nullptr;
    if (impl_ && !impl_->errors.empty())
        throw ArchiveError("Cannot checkpoint an Animator with invalid asset configuration");
    if (impl_ && !impl_->binding_errors.empty())
        throw ArchiveError("Cannot checkpoint an Animator with invalid model instance binding");
    auto entries = Json::array();
    if (impl_)
        for (const auto& [id, p] : impl_->states) {
            if (impl_->errors.contains(id))
                throw ArchiveError(
                    "Cannot checkpoint an Animator with invalid asset configuration");
            auto actor = impl_->context.world().entity(id);
            if (!actor.has<Animator>() ||
                actor.get<Animator>() != ((p.definition || p.space) ? p.requested : p.config))
                return nullptr;
            entries.push_back(
                {{"entity", p.reference},
                 {"skeleton", p.config.skeleton.id},
                 {"clip", p.config.clip.id},
                 {"skeleton_revision", p.assets.skeleton_revision},
                 {"clip_revision", p.assets.clip_revision},
                 {"time", p.time},
                 {"playing", p.playing},
                 {"blend",
                  {{"clip", p.config.blend_clip.id ? Json(p.config.blend_clip.id) : Json()},
                   {"revision", p.blend_assets.clip_revision},
                   {"mask", p.config.blend_mask.id ? Json(p.config.blend_mask.id) : Json()},
                   {"mask_revision", p.mask ? p.mask.identity().revision : ""},
                   {"weight", p.blend},
                   {"from", p.blend_from},
                   {"elapsed", p.blend_elapsed},
                   {"target", p.config.blend_weight},
                   {"seconds", p.config.transition_seconds}}}});
            if (p.space) {
                const auto pair = p.space_selection;
                entries.back()["blend_space"] = {
                    {"asset", p.requested.blend_space.id},
                    {"revision", p.space.identity().revision},
                    {"position", p.requested.blend_position},
                    {"cycle_seconds", playback_duration(p)},
                    {"first_sample", p.space->space.samples[pair.samples[0]].id},
                    {"second_sample",
                     p.space->space.samples[pair.samples[pair.count > 1 ? 1 : 0]].id}};
                if (p.space->space.dimensions == 2) {
                    auto& saved = entries.back()["blend_space"];
                    saved["dimensions"] = 2;
                    saved["position_y"] = p.requested.blend_position_y;
                    saved["samples"] = space_selection_json(p);
                }
            }
            if (p.definition) {
                auto frozen = Json::array();
                auto frozen_morphs = Json::array();
                for (const auto& value : p.frozen_morphs)
                    frozen_morphs.push_back({{"node", value.node}, {"weights", value.weights}});
                for (const auto& pose : p.frozen)
                    frozen.push_back({{"translation", pose.translation},
                                      {"rotation", pose.rotation},
                                      {"scale", pose.scale}});
                entries.back()["states"] = {
                    {"asset", p.requested.states.id},
                    {"revision", p.definition.identity().revision},
                    {"state", p.config.state},
                    {"requested_state", p.requested.state},
                    {"entry_pending", p.entry_pending},
                    {"outgoing_state", p.outgoing_sampler ? p.outgoing_config.state : 0},
                    {"outgoing_time", p.outgoing_time},
                    {"outgoing_speed", p.outgoing_sampler ? p.outgoing_config.playback_speed : 0},
                    {"frozen", frozen},
                    {"frozen_morphs", frozen_morphs}};
            }
        }
    return {{"version", 3},
            {"entries", entries},
            {"sequence_entries", impl_ ? impl_->transforms.sequence_checkpoint() : Json::array()},
            {"transform_entries", impl_ ? impl_->transforms.checkpoint() : Json::array()}};
}
void AnimationRuntime::restore(const Json& data) {
    if (!impl_)
        throw ArchiveError("Animation runtime is unavailable");
    impl_->synchronize();
    if (!impl_->pending.empty()) {
        // Candidate recovery world is not published or ticking. Share one bounded
        // deadline across all model dependencies, not a timeout per entity.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!impl_->pending.empty() && std::chrono::steady_clock::now() < deadline) {
            for (const auto& [asset, request] : impl_->space_requests) {
                (void)asset;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (!impl_->space_pool.wait(request, remaining))
                    throw ArchiveError(
                        "Animation Blend Space recovery resources failed or timed out");
            }
            for (const auto& [asset, request] : impl_->mask_requests) {
                (void)asset;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (!impl_->mask_pool.wait(request, remaining))
                    throw ArchiveError("Animation Mask recovery resources failed or timed out");
            }
            for (const auto& [asset, request] : impl_->state_requests) {
                (void)asset;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (!impl_->state_pool.wait(request, remaining))
                    throw ArchiveError("Animation state recovery resources failed or timed out");
            }
            for (const auto& [key, request] : impl_->model_requests) {
                (void)key;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (!impl_->model_resources->prepare_recovery(request, remaining))
                    throw ArchiveError("Model animation recovery resources failed or timed out");
            }
            impl_->synchronize();
            if (!impl_->pending.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!impl_->pending.empty())
            throw ArchiveError("Animation recovery admission timed out");
    }
    if ((data.at("version") != 1 && data.at("version") != 2 && data.at("version") != 3) ||
        !data.at("entries").is_array() || data.at("entries").size() != impl_->states.size() ||
        !impl_->errors.empty())
        throw ArchiveError("Animation recovery configuration differs");
    std::set<flecs::entity_t> seen;
    struct RestoreValue {
        Playback* playback;
        double time;
        bool playing;
        double blend, from, elapsed;
        std::unique_ptr<Sampler> outgoing;
        Pair outgoing_assets;
        Animator outgoing_config;
        double outgoing_time = 0;
        std::vector<LocalPose> frozen;
        MorphPose frozen_morphs;
        bool entry_pending = false;
    };
    std::vector<RestoreValue> values;
    std::map<flecs::entity_t, Playback> prepared_players;
    const auto swap_players = [&] {
        for (auto& [id, player] : prepared_players)
            std::swap(impl_->states.at(id), player);
    };
    std::map<flecs::entity_t, double> times, weights;
    for (const auto& entry : data.at("entries")) {
        auto ref = entry.at("entity").get<EntityRef>();
        auto entity = impl_->context.resolve(ref);
        if (entity.state != WorldContext::ResolveState::Available ||
            !seen.insert(entity.entity).second || !impl_->states.contains(entity.entity))
            throw ArchiveError("Animation recovery entity is unresolved");
        auto* selected = &impl_->states.at(entity.entity);
        if (data.at("version") >= 2 && selected->definition) {
            const auto& raw = entry.at("states").at("requested_state");
            if (!(raw.is_number_unsigned() ||
                  (raw.is_number_integer() && raw.get<std::int64_t>() >= 0)) ||
                raw.get<std::uint64_t>() > UINT32_MAX)
                throw ArchiveError("Animation recovery requested state is invalid");
            auto requested = selected->requested;
            requested.state = raw.get<std::uint32_t>();
            if (requested.state != selected->requested.state) {
                std::optional<Playback> candidate;
                impl_->state_player(
                    impl_->context.world().entity(entity.entity), requested, 0, true,
                    [](const Animator&) {}, false, &candidate);
                if (!candidate)
                    throw ArchiveError("Animation recovery target admission incomplete");
                selected =
                    &prepared_players.emplace(entity.entity, std::move(*candidate)).first->second;
            }
        }
        auto& p = *selected;
        if (p.space) {
            const auto& space = entry.at("blend_space");
            const auto pair = p.space_selection;
            if (space.at("asset") != Json(p.requested.blend_space.id) ||
                space.at("revision") != p.space.identity().revision ||
                space.at("position") != p.requested.blend_position ||
                space.value("dimensions", 1u) != p.space->space.dimensions ||
                space.at("cycle_seconds") != playback_duration(p) ||
                space.at("first_sample") != Json(p.space->space.samples[pair.samples[0]].id) ||
                space.at("second_sample") !=
                    Json(p.space->space.samples[pair.samples[pair.count > 1 ? 1 : 0]].id))
                throw ArchiveError("Animation Blend Space recovery selection/revision differs");
            if (p.space->space.dimensions == 2 &&
                (space.at("position_y") != p.requested.blend_position_y ||
                 space.at("samples") != space_selection_json(p)))
                throw ArchiveError("2D Blend Space recovery coordinates or layers differ");
        } else if (entry.contains("blend_space"))
            throw ArchiveError("Unexpected Animation Blend Space recovery data");
        double time = entry.at("time");
        if (entry.at("skeleton") != Json(p.config.skeleton.id) ||
            entry.at("clip") != Json(p.config.clip.id) ||
            entry.at("skeleton_revision") != p.assets.skeleton_revision ||
            entry.at("clip_revision") != p.assets.clip_revision || !std::isfinite(time) ||
            time < 0 || time > playback_duration(p) || !entry.at("playing").is_boolean())
            throw ArchiveError("Animation recovery asset revision or playback state differs");
        double blend = 0, from = 0, elapsed = 0;
        if (entry.contains("blend")) {
            const auto& b = entry.at("blend");
            blend = b.at("weight");
            from = b.at("from");
            elapsed = b.at("elapsed");
            if (b.at("clip") != (p.config.blend_clip.id ? Json(p.config.blend_clip.id) : Json()) ||
                b.at("revision") != p.blend_assets.clip_revision ||
                b.value("mask", Json()) !=
                    (p.config.blend_mask.id ? Json(p.config.blend_mask.id) : Json()) ||
                b.value("mask_revision", std::string()) !=
                    (p.mask ? p.mask.identity().revision : "") ||
                b.at("target") != p.config.blend_weight ||
                b.at("seconds") != p.config.transition_seconds || !std::isfinite(blend) ||
                blend < 0 || blend > 1 || !std::isfinite(from) || from < 0 || from > 1 ||
                !std::isfinite(elapsed) || elapsed < 0 || elapsed > p.config.transition_seconds)
                throw ArchiveError("Animation recovery blend configuration or clock differs");
            const double ratio =
                p.config.transition_seconds > 0 ? elapsed / p.config.transition_seconds : 1;
            if (std::abs(blend - (from + (p.config.blend_weight - from) * ratio)) > 1e-6)
                throw ArchiveError("Animation recovery blend progress is inconsistent");
        } else if (p.config.blend_clip.id || p.config.blend_weight != 0)
            throw ArchiveError("Animation recovery is missing its blend state");
        RestoreValue value{&p, time, entry.at("playing").get<bool>(), blend, from, elapsed};
        if (p.definition) {
            const auto& state = entry.at("states");
            if (state.at("asset") != Json(p.requested.states.id) ||
                state.at("revision") != p.definition.identity().revision ||
                state.at("state") != p.config.state)
                throw ArchiveError("Animation state recovery identity/revision differs");
            if (state.contains("entry_pending") && !state.at("entry_pending").is_boolean())
                throw ArchiveError("Animation marker recovery entry flag must be boolean");
            value.entry_pending = state.value("entry_pending", false);
            if (value.entry_pending && time != 0)
                throw ArchiveError(
                    "Animation marker recovery entry flag conflicts with playback time");
            const auto outgoing_id = state.at("outgoing_state").get<std::uint32_t>();
            value.outgoing_time = state.at("outgoing_time");
            const double speed = state.at("outgoing_speed");
            if (!std::isfinite(value.outgoing_time) || value.outgoing_time < 0 ||
                !std::isfinite(speed) || speed < 0 || speed > 16)
                throw ArchiveError("Animation state recovery outgoing clock is invalid");
            const auto& frozen = state.at("frozen");
            if (!frozen.is_array() ||
                (!frozen.empty() && frozen.size() != p.assets.skeleton->info().tracks))
                throw ArchiveError("Animation state recovery frozen pose size differs");
            for (const auto& pose : frozen)
                value.frozen.push_back({pose.at("translation").get<std::array<float, 3>>(),
                                        pose.at("scale").get<std::array<float, 3>>(),
                                        pose.at("rotation").get<std::array<float, 4>>()});
            const auto morphs = state.value("frozen_morphs", Json::array());
            if (!morphs.is_array() ||
                (value.frozen.empty() ? !morphs.empty() : morphs.size() != p.morph_defaults.size()))
                throw ArchiveError("Animation state recovery frozen morph node count differs");
            for (const auto& morph : morphs) {
                const auto& weights = morph.at("weights");
                if (!weights.is_array() || weights.size() > 256)
                    throw ArchiveError(
                        "Animation state recovery morph target count exceeds profile");
                value.frozen_morphs.push_back(
                    {morph.at("node").get<std::size_t>(), weights.get<std::vector<float>>()});
            }
            std::sort(value.frozen_morphs.begin(), value.frozen_morphs.end(),
                      [](const auto& a, const auto& b) { return a.node < b.node; });
            if (!value.frozen.empty())
                (void)blend_morph_samples(p.morph_defaults, {}, value.frozen_morphs, 1);
            if (!value.frozen.empty()) {
                Sampler check(p.assets.skeleton, p.assets.clip);
                (void)check.sample_transition(float(time / p.assets.clip->info().duration),
                                              value.frozen, float(blend));
            }
            if (outgoing_id) {
                const auto& outgoing = p.definition->state(outgoing_id);
                value.outgoing_assets = p.clips.at(outgoing.clip.id);
                if (!value.frozen.empty() ||
                    value.outgoing_time > value.outgoing_assets.clip->info().duration)
                    throw ArchiveError("Animation state recovery source is inconsistent");
                value.outgoing_config = p.config;
                value.outgoing_config.state = outgoing_id;
                value.outgoing_config.clip = outgoing.clip;
                value.outgoing_config.loop = outgoing.loop;
                if (std::abs(speed - p.requested.playback_speed * outgoing.speed) > 1e-6)
                    throw ArchiveError("Animation state recovery outgoing speed differs");
                value.outgoing_config.playback_speed = float(speed);
                value.outgoing =
                    std::make_unique<Sampler>(p.assets.skeleton, value.outgoing_assets.clip);
            } else if (value.outgoing_time != 0 || speed != 0)
                throw ArchiveError("Animation state recovery has an orphan outgoing clock");
            if (blend < 1 && !value.outgoing && value.frozen.empty())
                throw ArchiveError("Animation state recovery is missing its transition source");
            // Temporary candidate: validate frozen quaternions, complete local pose and model
            // binding before any adoption.
            auto old_source = std::move(p.outgoing_sampler);
            auto old_assets = p.outgoing_assets;
            auto old_frozen = std::move(p.frozen);
            auto old_morphs = std::move(p.frozen_morphs);
            const auto old_time = p.outgoing_time;
            p.outgoing_sampler = std::move(value.outgoing);
            p.outgoing_assets = value.outgoing_assets;
            p.frozen = std::move(value.frozen);
            p.frozen_morphs = std::move(value.frozen_morphs);
            p.outgoing_time = value.outgoing_time;
            try {
                (void)sample_pose(p, time, blend, true);
                if (!prepared_players.contains(entity.entity))
                    impl_->apply_model_poses(false, {{entity.entity, time}}, entity.entity,
                                             {{entity.entity, blend}});
            } catch (...) {
                value.outgoing = std::move(p.outgoing_sampler);
                value.frozen = std::move(p.frozen);
                value.frozen_morphs = std::move(p.frozen_morphs);
                p.outgoing_sampler = std::move(old_source);
                p.outgoing_assets = old_assets;
                p.frozen = std::move(old_frozen);
                p.frozen_morphs = std::move(old_morphs);
                p.outgoing_time = old_time;
                throw;
            }
            value.outgoing = std::move(p.outgoing_sampler);
            value.frozen = std::move(p.frozen);
            value.frozen_morphs = std::move(p.frozen_morphs);
            p.outgoing_sampler = std::move(old_source);
            p.outgoing_assets = old_assets;
            p.frozen = std::move(old_frozen);
            p.frozen_morphs = std::move(old_morphs);
            p.outgoing_time = old_time;
        } else {
            if (entry.contains("states"))
                throw ArchiveError("Unexpected named animation state recovery data");
            (void)sample_pose(p, time, blend, true);
        }
        times.emplace(entity.entity, time);
        weights.emplace(entity.entity, blend);
        values.push_back(std::move(value));
    }
    std::size_t recovered_joints = 0;
    for (const auto& value : values) {
        const auto& p = *value.playback;
        const auto layers = p.space && p.space->space.dimensions == 2 ? p.space_selection.count
                            : (p.blend_sampler || (p.definition && value.blend < 1)) ? 2
                                                                                     : 1;
        recovered_joints += p.assets.skeleton->info().tracks * layers;
    }
    if (recovered_joints > 8192)
        throw ArchiveError("Animation recovery exceeds evaluated-joint budget");
    const auto transform_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (impl_->transforms.pending() && std::chrono::steady_clock::now() < transform_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        impl_->synchronize();
    }
    // Validate recovered channel values and instance scopes with temporary
    // samplers before replacing any playback state or mutating the world.
    auto swap_sources = [&] {
        for (auto& value : values)
            if (value.playback->definition) {
                auto& p = *value.playback;
                std::swap(p.outgoing_sampler, value.outgoing);
                std::swap(p.outgoing_assets, value.outgoing_assets);
                std::swap(p.outgoing_config, value.outgoing_config);
                std::swap(p.outgoing_time, value.outgoing_time);
                std::swap(p.frozen, value.frozen);
                std::swap(p.frozen_morphs, value.frozen_morphs);
            }
    };
    swap_sources();
    swap_players();
    std::map<flecs::entity_t, PreparedRig> recovered_rigs;
    try {
        impl_->apply_model_poses(false, times, 0, weights, {}, &recovered_rigs);
    } catch (...) {
        swap_players();
        swap_sources();
        throw;
    }
    swap_players();
    swap_sources();
    impl_->transforms.restore(data.value("transform_entries", Json::array()),
                              data.at("version") >= 3 ? data.at("sequence_entries")
                                                      : Json::array());
    for (auto& value : values) {
        auto& p = *value.playback;
        const auto time = value.time;
        p.time = time;
        p.playing = value.playing;
        p.entry_pending = value.entry_pending;
        p.previous = time;
        p.advance = 0;
        p.blend = p.previous_blend = value.blend;
        p.blend_from = value.from;
        p.blend_elapsed = value.elapsed;
        if (p.definition) {
            p.outgoing_sampler = std::move(value.outgoing);
            p.outgoing_assets = std::move(value.outgoing_assets);
            p.outgoing_config = value.outgoing_config;
            p.outgoing_time = p.outgoing_previous = value.outgoing_time;
            p.outgoing_advance = 0;
            p.frozen = std::move(value.frozen);
            p.frozen_morphs = std::move(value.frozen_morphs);
        }
        p.sampler->reset();
        if (p.blend_sampler)
            p.blend_sampler->reset();
        // The recovery envelope does not contain renderer-local history. Until
        // the next successful fixed application, keep any prior good draw.
        p.model_pose_applied = false;
        p.discontinuous = true;
    }
    // Adopt only after every clock, asset, transform and work-budget check passes.
    // Snapshot prefab masks remain authored intent; this selection is runtime data.
    swap_players();
    for (const auto& [id, old_player] : prepared_players) {
        (void)old_player;
        impl_->context.world().entity(id).set<Animator>(impl_->states.at(id).requested);
    }
    for (auto& [id, rig] : recovered_rigs) {
        auto& player = impl_->states.at(id);
        player.rig_inputs = std::move(rig.inputs);
        player.rig = player.previous_rig = rig.constraints;
        player.rig_channels = std::move(rig.affected);
        player.pose_channels = std::move(rig.channels);
        player.rig_base = std::move(rig.base);
        player.rig_base_mask = std::move(rig.mask);
        player.rig_motion_root = rig.motion_root;
        player.rig_admitted = true;
    }
    impl_->events.clear();
    impl_->event_candidates.clear();
    impl_->last_animation_tick.reset();
    impl_->binding_errors.clear();
    impl_->discontinuities.clear();
}
EngineModule animation_module(std::filesystem::path project) {
    auto module = animation_schema_module();
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.provided_services = capability(Capability::Animation);
    module.allowed_services = capability(Capability::Diagnostics) |
                              capability(Capability::Profiling) | capability(Capability::Animation);
    module.start = [project = std::move(project)](ModuleContext& c) {
        auto state = std::make_shared<AnimationRuntime>(*c.owner, project);
        c.world.component<AnimationBridge>("forge.animation.private_bridge")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        c.world.set<AnimationBridge>({state});
        c.services.publish_animation(state);
        c.state = state;
    };
    module.stop = [](ModuleContext& c) {
        c.services.publish_animation({});
        std::static_pointer_cast<AnimationRuntime>(c.state)->shutdown();
    };
    return module;
}
std::shared_ptr<AnimationRuntime> animation_runtime(WorldContext& c) {
    if (!c.world().has<AnimationBridge>())
        return {};
    return c.world().get<AnimationBridge>().runtime.lock();
}
} // namespace forge
