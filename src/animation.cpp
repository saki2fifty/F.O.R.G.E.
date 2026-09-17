#include "animation_asset.hpp"
#include "animation_bytes.hpp"
#include <cmath>
#include <forge/animation.hpp>
#include <forge/project_paths.hpp>
#include <set>
namespace forge {
namespace {
using namespace animation_detail;
struct AnimationBridge {
    std::weak_ptr<AnimationRuntime> runtime;
};
struct Pair {
    std::shared_ptr<const Skeleton> skeleton;
    std::shared_ptr<const Clip> clip;
    std::string skeleton_revision, clip_revision;
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
    std::map<flecs::entity_t, Playback> states;
    std::map<flecs::entity_t, std::string> errors;
    std::map<flecs::entity_t, Animator> failed_configurations;
    std::size_t cache_bytes = 0;
    Impl(WorldContext& c, std::filesystem::path p) : context(c), project(std::move(p)) {}
    Pair load(const Animator& config) {
        if (!config.skeleton.id || !config.clip.id)
            throw ArchiveError("Choose a Skeleton and Animation clip");
        auto catalog = AssetCatalog::open_project(project);
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
        return {skeletons.at(config.skeleton.id), clips.at(config.clip.id),
                sm.at("artifact_sha256"), cm.at("artifact_sha256")};
    }
    void synchronize() {
        std::set<flecs::entity_t> alive;
        context.world().each([&](flecs::entity e, const Animator& config) {
            if (e.has(flecs::Prefab) || !context.reference(e.id()))
                return;
            alive.insert(e.id());
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
                if (!std::isfinite(config.playback_speed) || config.playback_speed < 0 ||
                    config.playback_speed > 4)
                    throw ArchiveError("Animator playback speed must be between 0 and 4");
                if (old != states.end() && old->second.config.skeleton == config.skeleton &&
                    old->second.config.clip == config.clip) {
                    old->second.config = config;
                    old->second.previous = old->second.time;
                    old->second.advance = 0;
                } else {
                    Playback next;
                    next.config = config;
                    next.reference = *context.reference(e.id());
                    next.assets = load(config);
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
        if (!p.config.enabled || !p.playing || impl_->errors.contains(id))
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
    if (!impl_ || !impl_->states.contains(id) || impl_->errors.contains(id))
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
    return {{"parents", p.assets.skeleton->info().parents},
            {"model", pose},
            {"time", time},
            {"duration", duration},
            {"playing", p.playing},
            {"clip", p.config.clip.id}};
}
Json AnimationRuntime::checkpoint() const {
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
    if (data.at("version") != 1 || !data.at("entries").is_array() ||
        data.at("entries").size() != impl_->states.size() || !impl_->errors.empty())
        throw ArchiveError("Animation recovery configuration differs");
    std::set<flecs::entity_t> seen;
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
        p.time = time;
        p.playing = entry.at("playing");
        p.previous = time;
        p.advance = 0;
        p.sampler->reset();
        p.sampler->sample(static_cast<float>(time / p.assets.clip->info().duration));
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
