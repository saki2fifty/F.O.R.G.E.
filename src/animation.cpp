#include "animation_asset.hpp"
#include "animation_resource.hpp"
#include "asset_bytes.hpp"
#include "builtins.hpp"
#include "legacy_animation_provenance.hpp"
#include "transform_animation_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <forge/runtime_content_access.hpp>
#include <future>
#include <set>
#include <thread>
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
    Animator config;
    EntityRef reference;
    Pair assets, blend_assets;
    std::vector<AnimatedTransformChannel> transform_channels;
    std::unique_ptr<Sampler> sampler, blend_sampler;
    double blend = 0, previous_blend = 0, blend_from = 0, blend_elapsed = 0;
    double time = 0, previous = 0, advance = 0;
    bool playing = false;
    bool model_pose_applied = false, discontinuous = true;
};
struct SampledPose {
    std::vector<Matrix> model;
    std::vector<LocalPose> local;
};
SampledPose sample_pose(Playback& p, double time, double blend, bool temporary = false) {
    std::unique_ptr<Sampler> first, second;
    auto* sampler = p.sampler.get();
    auto* blend_sampler = p.blend_sampler.get();
    if (temporary) {
        first = std::make_unique<Sampler>(p.assets.skeleton, p.assets.clip);
        sampler = first.get();
        if (p.blend_assets.clip) {
            second = std::make_unique<Sampler>(p.assets.skeleton, p.blend_assets.clip);
            blend_sampler = second.get();
        }
    }
    const auto ratio = float(std::clamp(time / p.assets.clip->info().duration, 0., 1.));
    auto model = blend_sampler ? sampler->sample_blended(ratio, *blend_sampler, float(blend))
                               : sampler->sample(ratio);
    return {std::move(model), sampler->local_pose()};
}
const std::vector<AnimatedTransformChannel>& animated_channels(const Playback& p) {
    return p.transform_channels;
}
} // namespace
struct AnimationRuntime::Impl {
    WorldContext& context;
    std::filesystem::path project;
    TransformAnimationPlayback transforms;
    bool initial_catalog_requested = false;
    std::string catalog_error;
    std::map<AssetId, std::shared_ptr<const Skeleton>> skeletons;
    std::map<AssetId, std::shared_ptr<const Clip>> clips;
    std::map<AssetId, std::string> revisions;
    std::unique_ptr<ModelAnimationResources> model_resources;
    std::shared_ptr<const AssetCatalog> model_catalog;
    std::map<std::pair<AssetId, AssetId>, ModelAnimationRequest> model_requests;
    std::map<flecs::entity_t, Playback> states;
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
                           const std::map<flecs::entity_t, double>& weights = {}) {
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
                const auto time = times.contains(id) ? times.at(id) : playback.time;
                const auto weight = weights.contains(id) ? weights.at(id) : playback.blend;
                const auto pose = sample_pose(playback, time, weight, !commit);
                const auto& locals = pose.local;
                struct Write {
                    flecs::entity entity;
                    LocalTransform value;
                    unsigned channels = 0;
                };
                std::map<flecs::entity_t, Write> writes;
                auto candidate = context.transform_nodes();
                for (const auto& channel : animated_channels(playback)) {
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
                if (!commit)
                    continue;
                const bool snap = playback.discontinuous || binding_errors.contains(id);
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
                playback.discontinuous = false;
                binding_errors.erase(id);
            } catch (const std::exception& ex) {
                if (!commit)
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
        if (p.assets.model)
            p.transform_channels = p.assets.model.clip->transform_channels;
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
        if (p.assets.model && (!p.assets.model.clip->morphs->sample(0).empty() ||
                               !loaded->model.clip->morphs->sample(0).empty()))
            throw ArchiveError("Two-clip skeletal blending does not support morph-weight curves");
        if (loaded->model) {
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
    void synchronize(float fixed_dt = 0) {
        if (!model_catalog && transforms.has_players() && !initial_catalog_requested) {
            initial_catalog_requested = true;
            refresh_assets();
        }
        poll_catalog();
        if (model_resources)
            model_resources->pump();
        pending.clear();
        std::set<flecs::entity_t> alive;
        std::set<std::pair<AssetId, AssetId>> used;
        std::set<AssetId> used_skeletons, used_clips;
        auto retain = [&](const Animator& config) {
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
            retain(config);
            auto old = states.find(e.id());
            if (old != states.end())
                retain(old->second.config);
            const bool reload = old != states.end() && old->second.config == config &&
                                reload_pending.contains(e.id());
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
                if (reload) {
                    auto assets = load(config);
                    if (!assets)
                        return; // Continue the previous good playback while loading.
                    if (fixed_dt == 0)
                        return; // Pause may prepare resources, but cannot apply a new pose.
                    Playback next;
                    next.config = config;
                    next.reference = old->second.reference;
                    next.assets = std::move(*assets);
                    if (!prepare_blend(next))
                        return;
                    const auto same = [](const Pair& a, const Pair& b) {
                        return a.skeleton == b.skeleton && a.clip == b.clip &&
                               a.model_generation == b.model_generation;
                    };
                    if (same(next.assets, old->second.assets) &&
                        same(next.blend_assets, old->second.blend_assets)) {
                        reload_pending.erase(e.id());
                        return;
                    }
                    next.blend = old->second.blend;
                    next.previous_blend = next.blend;
                    next.blend_from = old->second.blend_from;
                    next.blend_elapsed = old->second.blend_elapsed;
                    std::size_t joints =
                        next.assets.skeleton->info().tracks * (next.blend_sampler ? 2 : 1);
                    for (const auto& [other, state] : states)
                        if (other != e.id())
                            joints += state.assets.skeleton->info().tracks *
                                      (state.blend_sampler ? 2 : 1);
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
                        apply_model_poses(false, {{e.id(), target_time}}, e.id());
                    } catch (...) {
                        std::swap(old->second, next);
                        throw;
                    }
                    reload_pending.erase(e.id());
                    reload_errors.erase(e.id());
                } else if (old != states.end() && old->second.config.skeleton == config.skeleton &&
                           old->second.config.clip == config.clip &&
                           old->second.config.blend_clip == config.blend_clip) {
                    if (old->second.config.enabled != config.enabled) {
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
                    std::size_t joints =
                        next.assets.skeleton->info().tracks * (next.blend_sampler ? 2 : 1);
                    for (const auto& [other, state] : states)
                        if (other != e.id())
                            joints += state.assets.skeleton->info().tracks *
                                      (state.blend_sampler ? 2 : 1);
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
        throw ArchiveError("Scene preparation: invalid Animator asset configuration");
    if (!impl_->pending.empty())
        return false;
    impl_->apply_model_poses();
    if (!impl_->binding_errors.empty())
        throw ArchiveError("Scene preparation: invalid Animator model binding");
    return impl_->transforms.initial_pose();
}
void AnimationRuntime::refresh_assets() {
    if (impl_)
        impl_->refresh_assets();
}
void AnimationRuntime::catalog(std::shared_ptr<const AssetCatalog> selected) {
    if (impl_)
        impl_->catalog(std::move(selected));
}
void AnimationRuntime::tick(float dt) {
    if (!impl_)
        return;
    if (!std::isfinite(dt) || dt <= 0)
        throw ArchiveError("Animation requires a positive fixed delta");
    auto profile = impl_->context.services().profile("animation", "FixedPlayback");
    impl_->synchronize(dt);
    for (auto& [id, p] : impl_->states) {
        p.previous = p.time;
        p.previous_blend = p.blend;
        p.advance = 0;
        if (!p.config.enabled || !p.playing || impl_->pending.contains(id))
            continue;
        if (p.config.playback_speed > 0 && p.blend != p.config.blend_weight) {
            p.blend_elapsed = std::min(double(p.config.transition_seconds), p.blend_elapsed + dt);
            const double ratio =
                p.config.transition_seconds > 0 ? p.blend_elapsed / p.config.transition_seconds : 1;
            p.blend = p.blend_from + (p.config.blend_weight - p.blend_from) * ratio;
        }
        auto duration = p.assets.clip->info().duration;
        p.advance = double(dt) * p.config.playback_speed;
        p.time = p.previous + p.advance;
        if (p.config.loop)
            p.time = std::fmod(p.time, duration);
        else if (p.time >= duration) {
            p.time = duration;
            p.advance = p.time - p.previous;
            p.playing = false;
        }
    }
    impl_->apply_model_poses();
    impl_->transforms.tick(dt);
}
void AnimationRuntime::reset_presentation() {
    if (!impl_)
        return;
    impl_->synchronize();
    impl_->discontinuities.clear();
    for (auto& [id, p] : impl_->states) {
        (void)id;
        p.previous = p.time;
        p.advance = 0;
        p.previous_blend = p.blend;
        p.sampler->reset();
        if (p.blend_sampler)
            p.blend_sampler->reset();
    }
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
    const auto duration = p.assets.clip->info().duration;
    auto time = p.previous + p.advance * alpha;
    if (p.config.loop)
        time = std::fmod(time, duration);
    const double weight = p.previous_blend + (p.blend - p.previous_blend) * alpha;
    auto pose = sample_pose(p, time, weight);
    auto locals = Json::array();
    for (const auto& local : pose.local)
        locals.push_back({{"translation", local.translation},
                          {"rotation", local.rotation},
                          {"scale", local.scale}});
    auto morphs = Json::array();
    if (p.assets.model)
        for (const auto& morph : p.assets.model.clip->morphs->sample(time))
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
    return !impl_ || (impl_->pending.empty() && impl_->errors.empty() &&
                      impl_->binding_errors.empty() && impl_->transforms.checkpoint_ready());
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
                   {"weight", p.blend},
                   {"from", p.blend_from},
                   {"elapsed", p.blend_elapsed},
                   {"target", p.config.blend_weight},
                   {"seconds", p.config.transition_seconds}}}});
        }
    return {{"version", 1},
            {"entries", entries},
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
        for (const auto& [key, request] : impl_->model_requests) {
            (void)key;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (!impl_->model_resources->prepare_recovery(request, remaining))
                throw ArchiveError("Model animation recovery resources failed or timed out");
        }
        impl_->synchronize();
    }
    if (data.at("version") != 1 || !data.at("entries").is_array() ||
        data.at("entries").size() != impl_->states.size() || !impl_->errors.empty())
        throw ArchiveError("Animation recovery configuration differs");
    std::set<flecs::entity_t> seen;
    struct RestoreValue {
        Playback* playback;
        double time;
        bool playing;
        double blend, from, elapsed;
    };
    std::vector<RestoreValue> values;
    std::map<flecs::entity_t, double> times, weights;
    for (const auto& entry : data.at("entries")) {
        auto ref = entry.at("entity").get<EntityRef>();
        auto entity = impl_->context.resolve(ref);
        if (entity.state != WorldContext::ResolveState::Available ||
            !seen.insert(entity.entity).second || !impl_->states.contains(entity.entity))
            throw ArchiveError("Animation recovery entity is unresolved");
        auto& p = impl_->states.at(entity.entity);
        double time = entry.at("time");
        if (entry.at("skeleton") != Json(p.config.skeleton.id) ||
            entry.at("clip") != Json(p.config.clip.id) ||
            entry.at("skeleton_revision") != p.assets.skeleton_revision ||
            entry.at("clip_revision") != p.assets.clip_revision || !std::isfinite(time) ||
            time < 0 || time > p.assets.clip->info().duration || !entry.at("playing").is_boolean())
            throw ArchiveError("Animation recovery asset revision or playback state differs");
        double blend = 0, from = 0, elapsed = 0;
        if (entry.contains("blend")) {
            const auto& b = entry.at("blend");
            blend = b.at("weight");
            from = b.at("from");
            elapsed = b.at("elapsed");
            if (b.at("clip") != (p.config.blend_clip.id ? Json(p.config.blend_clip.id) : Json()) ||
                b.at("revision") != p.blend_assets.clip_revision ||
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
        (void)sample_pose(p, time, blend, true);
        times.emplace(entity.entity, time);
        weights.emplace(entity.entity, blend);
        values.push_back({&p, time, entry.at("playing").get<bool>(), blend, from, elapsed});
    }
    const auto transform_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (impl_->transforms.pending() && std::chrono::steady_clock::now() < transform_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        impl_->synchronize();
    }
    // Validate recovered channel values and instance scopes with temporary
    // samplers before replacing any playback state or mutating the world.
    impl_->apply_model_poses(false, times, 0, weights);
    impl_->transforms.restore(data.value("transform_entries", Json::array()));
    for (auto& value : values) {
        auto& p = *value.playback;
        const auto time = value.time;
        p.time = time;
        p.playing = value.playing;
        p.previous = time;
        p.advance = 0;
        p.blend = p.previous_blend = value.blend;
        p.blend_from = value.from;
        p.blend_elapsed = value.elapsed;
        p.sampler->reset();
        if (p.blend_sampler)
            p.blend_sampler->reset();
        // The recovery envelope does not contain renderer-local history. Until
        // the next successful fixed application, keep any prior good draw.
        p.model_pose_applied = false;
        p.discontinuous = true;
    }
    impl_->binding_errors.clear();
    impl_->discontinuities.clear();
}
EngineModule animation_module(std::filesystem::path project) {
    auto module = animation_schema_module();
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.allowed_services =
        capability(Capability::Diagnostics) | capability(Capability::Profiling);
    module.start = [project = std::move(project)](ModuleContext& c) {
        auto state = std::make_shared<AnimationRuntime>(*c.owner, project);
        c.world.component<AnimationBridge>("forge.animation.private_bridge")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        c.world.set<AnimationBridge>({state});
        c.state = state;
    };
    module.stop = [](ModuleContext& c) {
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
