#include "animation_asset.hpp"
#include "animation_resource.hpp"
#include "asset_bytes.hpp"
#include "builtins.hpp"
#include <cmath>
#include <forge/animation.hpp>
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <set>
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
};
struct Playback {
    Animator config;
    EntityRef reference;
    Pair assets;
    std::unique_ptr<Sampler> sampler;
    double time = 0, previous = 0, advance = 0;
    bool playing = false;
};
} // namespace
struct AnimationRuntime::Impl {
    WorldContext& context;
    std::filesystem::path project;
    std::map<AssetId, std::shared_ptr<const Skeleton>> skeletons;
    std::map<AssetId, std::shared_ptr<const Clip>> clips;
    std::map<AssetId, std::string> revisions;
    std::unique_ptr<ModelAnimationResources> model_resources;
    std::shared_ptr<const AssetCatalog> model_catalog;
    std::map<std::pair<AssetId, AssetId>, ModelAnimationRequest> model_requests;
    std::map<flecs::entity_t, Playback> states;
    std::set<flecs::entity_t> pending;
    std::map<flecs::entity_t, std::string> errors;
    std::map<flecs::entity_t, Animator> failed_configurations;
    std::size_t cache_bytes = 0;
    Impl(WorldContext& c, std::filesystem::path p) : context(c), project(std::move(p)) {}
    ~Impl() {
        states.clear();
        if (model_resources)
            model_resources->close();
    }
    std::optional<Pair> acquire_model(const ModelAnimationRequest& request) {
        auto loaded = model_resources->acquire(request);
        if (!loaded)
            return std::nullopt;
        Pair result{loaded.skeleton->native, loaded.clip->native, request.revision,
                    request.revision, std::move(loaded)};
        return result;
    }
    std::optional<Pair> load(const Animator& config) {
        if (!config.skeleton.id || !config.clip.id)
            throw ArchiveError("Choose a Skeleton and Animation clip");
        const auto key = std::pair{config.skeleton.id, config.clip.id};
        if (const auto existing = model_requests.find(key); existing != model_requests.end())
            return acquire_model(existing->second);
        if (model_catalog && model_catalog->records().contains(config.skeleton.id) &&
            model_catalog->records().contains(config.clip.id) &&
            (model_catalog->records().at(config.skeleton.id).metadata.contains("forge.model") ||
             model_catalog->records().at(config.clip.id).metadata.contains("forge.model"))) {
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
        for (const auto* m : {&sm, &cm}) {
            if (m->at("version") != 1 || m->at("ozz_revision") != ozz_revision ||
                m->at("ozz_version") != "0.17.0" || m->at("converter") != "gltf2ozz" ||
                m->at("converter_revision") != ozz_revision)
                throw ArchiveError("Unsupported animation provenance");
            auto source = m->at("source_asset").get<AssetId>();
            auto found = catalog.records().find(source);
            if (found == catalog.records().end() || found->second.type != "animation_source")
                throw ArchiveError("Animation source identity is missing");
            for (const auto& entry : m->at("settings").at("animations"))
                if (entry.at("iframe_interval") != 0 || entry.at("raw") != false ||
                    entry.at("additive") != false)
                    throw ArchiveError("Unsupported animation conversion settings");
        }
        if (sm.at("skeleton_asset") != Json(config.skeleton.id) ||
            cm.at("skeleton_asset") != Json(config.skeleton.id) ||
            sm.at("artifact_sha256") != cm.at("skeleton_sha256") ||
            sm.at("skeleton_sha256") != sm.at("artifact_sha256") ||
            sm.at("source_asset") != cm.at("source_asset") ||
            sm.at("source_sha256") != cm.at("source_sha256") ||
            sm.at("settings") != cm.at("settings"))
            throw ArchiveError("Animation clip is incompatible with this skeleton revision");
        ProjectPaths paths(project);
        auto admit = [&](const AssetRecord& record) {
            auto data = read_bytes(paths.resolve(record.source), max_archive_bytes);
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
    void synchronize() {
        if (model_resources)
            model_resources->pump();
        pending.clear();
        std::set<flecs::entity_t> alive;
        std::set<std::pair<AssetId, AssetId>> used;
        std::set<AssetId> used_skeletons, used_clips;
        context.world().each([&](flecs::entity e, const Animator& config) {
            if (e.has(flecs::Prefab) || !context.reference(e.id()))
                return;
            alive.insert(e.id());
            used.insert({config.skeleton.id, config.clip.id});
            used_skeletons.insert(config.skeleton.id);
            used_clips.insert(config.clip.id);
            auto old = states.find(e.id());
            if (old != states.end() && old->second.config == config) {
                errors.erase(e.id());
                failed_configurations.erase(e.id());
                return;
            }
            if (failed_configurations.contains(e.id()) &&
                failed_configurations.at(e.id()) == config)
                return;
            try {
                detail::validate_reflected_value(e, config);
                if (old != states.end() && old->second.config.skeleton == config.skeleton &&
                    old->second.config.clip == config.clip) {
                    old->second.config = config;
                    old->second.previous = old->second.time;
                    old->second.advance = 0;
                } else {
                    Playback next;
                    next.config = config;
                    next.reference = *context.reference(e.id());
                    auto assets = load(config);
                    if (!assets) {
                        pending.insert(e.id());
                        return;
                    }
                    next.assets = std::move(*assets);
                    std::size_t joints = next.assets.skeleton->info().tracks;
                    for (const auto& [other, state] : states)
                        if (other != e.id())
                            joints += state.assets.skeleton->info().tracks;
                    if (joints > 8192 || (old == states.end() && states.size() >= 256))
                        throw ArchiveError(
                            "Animation world exceeds 256 players / 8192 evaluated joints");
                    next.sampler =
                        std::make_unique<Sampler>(next.assets.skeleton, next.assets.clip);
                    next.sampler->sample(0);
                    next.playing = config.play_on_start;
                    states.insert_or_assign(e.id(), std::move(next));
                }
                errors.erase(e.id());
                failed_configurations.erase(e.id());
            } catch (const std::exception& ex) {
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
        std::erase_if(failed_configurations,
                      [&](const auto& entry) { return !alive.contains(entry.first); });
    }
};
AnimationRuntime::AnimationRuntime(WorldContext& c, std::filesystem::path p)
    : impl_(std::make_unique<Impl>(c, std::move(p))) {}
AnimationRuntime::~AnimationRuntime() = default;
void AnimationRuntime::shutdown() noexcept { impl_.reset(); }
void AnimationRuntime::synchronize() {
    if (impl_)
        impl_->synchronize();
}
void AnimationRuntime::tick(float dt) {
    if (!impl_)
        return;
    if (!std::isfinite(dt) || dt <= 0)
        throw ArchiveError("Animation requires a positive fixed delta");
    auto profile = impl_->context.services().profile("animation", "FixedPlayback");
    impl_->synchronize();
    for (auto& [id, p] : impl_->states) {
        p.previous = p.time;
        p.advance = 0;
        if (!p.config.enabled || !p.playing || impl_->errors.contains(id) ||
            impl_->pending.contains(id))
            continue;
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
}
void AnimationRuntime::reset_presentation() {
    if (!impl_)
        return;
    impl_->synchronize();
    for (auto& [id, p] : impl_->states) {
        (void)id;
        p.previous = p.time;
        p.advance = 0;
        p.sampler->reset();
    }
}
Json AnimationRuntime::presentation(flecs::entity_t id, double alpha) {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw ArchiveError("Invalid animation presentation alpha");
    if (!impl_ || !impl_->states.contains(id) || impl_->errors.contains(id) ||
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
    auto pose = p.sampler->sample(static_cast<float>(std::clamp(time / duration, 0.0, 1.0)));
    auto locals = Json::array();
    for (const auto& local : p.sampler->local_pose())
        locals.push_back({{"translation", local.translation},
                          {"rotation", local.rotation},
                          {"scale", local.scale}});
    auto morphs = Json::array();
    if (p.assets.model)
        for (const auto& morph : p.assets.model.clip->morphs->sample(time))
            morphs.push_back({{"node", morph.node}, {"weights", morph.weights}});
    Json result = {{"parents", p.assets.skeleton->info().parents},
                   {"model", pose},
                   {"time", time},
                   {"duration", duration},
                   {"playing", p.playing},
                   {"clip", p.config.clip.id},
                   {"local", std::move(locals)},
                   {"morphs", std::move(morphs)}};
    if (p.assets.model) {
        result["model_asset"] = p.assets.model.skeleton->model;
        result["model_revision"] = p.assets.skeleton_revision;
        result["joint_nodes"] = p.assets.model.skeleton->joint_nodes;
        if (p.assets.model.clip->has_transform_channels) {
            auto channels = Json::array();
            for (const auto& channel : p.assets.model.clip->transform_channels)
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
bool AnimationRuntime::checkpoint_ready() const {
    return !impl_ || (impl_->pending.empty() && impl_->errors.empty());
}
Json AnimationRuntime::checkpoint() const {
    // A partial binding is not a recoverable playback state. Runtime IPC preserves
    // the null marker until all selected model resources have been adopted.
    if (impl_ && !impl_->pending.empty())
        return nullptr;
    if (impl_ && !impl_->errors.empty())
        throw ArchiveError("Cannot checkpoint an Animator with invalid asset configuration");
    auto entries = Json::array();
    if (impl_)
        for (const auto& [id, p] : impl_->states) {
            if (impl_->errors.contains(id))
                throw ArchiveError(
                    "Cannot checkpoint an Animator with invalid asset configuration");
            entries.push_back({{"entity", p.reference},
                               {"skeleton", p.config.skeleton.id},
                               {"clip", p.config.clip.id},
                               {"skeleton_revision", p.assets.skeleton_revision},
                               {"clip_revision", p.assets.clip_revision},
                               {"time", p.time},
                               {"playing", p.playing}});
        }
    return {{"version", 1}, {"entries", entries}};
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
        std::unique_ptr<Sampler> sampler;
    };
    std::vector<RestoreValue> values;
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
        auto sampler = std::make_unique<Sampler>(p.assets.skeleton, p.assets.clip);
        sampler->sample(static_cast<float>(time / p.assets.clip->info().duration));
        values.push_back({&p, time, entry.at("playing").get<bool>(), std::move(sampler)});
    }
    for (auto& value : values) {
        auto& p = *value.playback;
        const auto time = value.time;
        p.time = time;
        p.playing = value.playing;
        p.previous = time;
        p.advance = 0;
        p.sampler = std::move(value.sampler);
    }
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
