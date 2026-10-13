#include "transform_animation_runtime.hpp"
#include "animation_property_state.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/character_components.hpp>
#include <forge/derived_cache.hpp>
#include <forge/model_asset.hpp>
#include <forge/navigation_components.hpp>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>
#include <set>
#include <thread>
namespace forge {
TransformAnimationPlayback::TransformAnimationPlayback(WorldContext& context,
                                                       std::filesystem::path project)
    : context_(context), project_(std::move(project)), pool_({1, 64, 64, 16 * 1024 * 1024}),
      players_(context.world().query<const TransformAnimator>()) {}
void TransformAnimationPlayback::catalog(std::shared_ptr<const AssetCatalog> catalog) {
    catalog_ = std::move(catalog);
}
bool TransformAnimationPlayback::has_players() const { return players_.count() != 0; }
ResourceTicket TransformAnimationPlayback::request(AssetRef<TransformAnimationAsset> asset) {
    const auto at = catalog_->records().find(asset.id);
    if (at == catalog_->records().end() || at->second.type != TransformAnimationAsset::type ||
        at->second.subasset)
        throw std::runtime_error("Transform animation clip is missing or has the wrong type");
    const auto selected = at->second.metadata.at("forge.import");
    const auto metadata = at->second.metadata.at("forge.transform_animation");
    const auto revision = selected.at("key").get<std::string>();
    const auto generation = selected.at("generation");
    if (selected.at("version") != 1 || selected.at("output_format") != "forge.transform-clip" ||
        (selected.at("output_version") != 1 && selected.at("output_version") != 2 &&
         selected.at("output_version") != 3) ||
        !generation.is_number_unsigned() || generation == 0)
        throw std::runtime_error("Unsupported transform animation selection");
    return pool_.request(
        asset, revision, generation.get<std::uint64_t>(),
        [project = project_, asset, selected, metadata, revision](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation load cancelled");
            DerivedDataCache cache(project, {transform_animation_byte_limit,
                                             transform_animation_byte_limit + 65536, 1});
            std::optional<TransformAnimationClip> clip;
            auto artifact = cache.load_selected(revision, [&](const auto& cooked) {
                if (cooked.files.size() != 1 || cooked.files.front().name != "animation.clip")
                    throw std::runtime_error("Invalid transform animation bundle");
                const auto document = asset_detail::parse_bounded_json(
                    cooked.files.front().bytes, transform_animation_byte_limit);
                if (document.at("version") != selected.at("output_version"))
                    throw std::runtime_error("Transform clip/recipe version mismatch");
                clip = decode_transform_animation(cooked.files.front().bytes, asset.id);
            });
            const auto& input = artifact.manifest.at("inputs");
            for (const auto* field : {"importer", "importer_revision", "output_format",
                                      "output_version", "platform", "backend", "profile"})
                if (input.at(field) != selected.at(field))
                    throw std::runtime_error("Animation recipe differs from selected artifact");
            const auto& file = artifact.files.front();
            if (input.at("source") != selected.at("source_digest") ||
                selected.at("artifact_digest") !=
                    asset_build_digest(artifact.manifest.at("files")) ||
                metadata.at("file") != file.name ||
                metadata.at("sha256") != asset_detail::content_digest(file.bytes) ||
                metadata.at("bytes") != file.bytes.size() || !clip || stop.stop_requested())
                throw std::runtime_error("Animation artifact/provenance mismatch");
            auto value = std::make_unique<TransformAnimationClip>(std::move(*clip));
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<TransformAnimationAsset>{std::move(value),
                                                              {0, 0, 0, 0, bytes}};
        });
}
void TransformAnimationPlayback::error(flecs::entity_t id, const std::string& text) {
    if (errors_[id] == text)
        return;
    errors_[id] = text;
    Diagnostic diagnostic{Severity::Error, "animation.transform", text, {}};
    if (const auto ref = context_.reference(id))
        diagnostic.context.entity = ref->entity;
    diagnostic.context.module = "forge.animation";
    context_.services().emit(std::move(diagnostic));
}
void TransformAnimationPlayback::synchronize() {
    pool_.pump();
    pending_ = false;
    std::set<flecs::entity_t> alive;
    std::set<AssetId> used;
    players_.each([&](flecs::entity entity, const TransformAnimator& config) {
        const auto reference = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !reference || !config.enabled || !config.clip.id)
            return;
        alive.insert(entity.id());
        used.insert(config.clip.id);
        if (alive.size() > 256) {
            error(entity.id(), "Transform animation player limit (256) exceeded");
            return;
        }
        if (!catalog_) {
            pending_ = true;
            return;
        }
        try {
            if (!std::isfinite(config.playback_speed) || config.playback_speed < 0 ||
                config.playback_speed > 4)
                throw std::runtime_error("Transform animation speed must be within 0–4");
            auto& state = states_[entity.id()];
            const bool changed = state.config.clip != config.clip || state.entity != *reference;
            state.requested = config.clip;
            auto ticket = request(config.clip);
            state.ticket = ticket;
            auto clip = pool_.acquire(ticket);
            if (!clip) {
                const auto info = ticket.inspect();
                if (resource_detail::terminal(info.state))
                    throw std::runtime_error(info.diagnostic);
                pending_ = true;
                return;
            }
            if (changed || !state.clip) {
                state.time = 0;
                state.playing = config.play_on_start;
            }
            if (changed || !state.clip ||
                state.clip.identity().revision != clip.identity().revision) {
                state.discontinuous = true;
                state.previous = state.time;
                state.advance = 0;
            }
            state.time = std::min(state.time, double(clip->duration));
            state.config = config;
            state.entity = *reference;
            state.ticket = std::move(ticket);
            state.clip = std::move(clip);
            if (load_errors_.erase(entity.id()))
                errors_.erase(entity.id());
        } catch (const std::exception& e) {
            load_errors_.insert(entity.id());
            error(entity.id(), e.what());
        }
    });
    for (const auto& [id, state] : states_) {
        if (alive.contains(id))
            continue;
        if (state.config.clip.id && !used.contains(state.config.clip.id))
            pool_.unload(state.config.clip);
        if (state.requested.id && !used.contains(state.requested.id) &&
            state.requested != state.config.clip)
            pool_.unload(state.requested);
    }
    std::erase_if(states_, [&](const auto& entry) { return !alive.contains(entry.first); });
    std::erase_if(errors_, [&](const auto& entry) { return !alive.contains(entry.first); });
    std::erase_if(load_errors_, [&](auto id) { return !alive.contains(id); });
}
void TransformAnimationPlayback::apply(const std::map<flecs::entity_t, double>& times,
                                       bool commit) {
    if (times.empty())
        return;
    const bool transforms = std::any_of(times.begin(), times.end(), [&](const auto& entry) {
        return states_.at(entry.first).clip->channels != 0;
    });
    auto candidate =
        transforms ? context_.transform_nodes() : decltype(context_.transform_nodes()){};
    std::map<flecs::entity_t, AnimationPropertyState> properties;
    for (const auto& [id, time] : times) {
        const auto& state = states_.at(id);
        auto entity = context_.world().entity(id);
        if (state.clip->properties) {
            auto values = AnimationPropertyState::from(entity, state.clip->properties);
            values.sample(state.clip->properties, state.clip->sample_properties(time));
            properties.emplace(id, std::move(values));
        }
        if (!state.clip->channels)
            continue;
        if ((entity.has<PhysicsBody>() && entity.get<PhysicsBody>().enabled &&
             entity.get<PhysicsBody>().motion == 2) ||
            (entity.has<CharacterController>() && entity.get<CharacterController>().enabled) ||
            (entity.has<NavigationAgent>() && entity.get<NavigationAgent>().enabled &&
             entity.get<NavigationAgent>().has_destination) ||
            (entity.has<Animator>() && entity.get<Animator>().enabled))
            throw std::runtime_error(
                "Transform Animator conflicts with this object's active movement controller");
        if (entity.has<ModelSource>() && entity.get<ModelSource>().node.id) {
            std::set<flecs::entity_t> visited;
            for (auto parent = entity.target(flecs::ChildOf);
                 parent && visited.insert(parent.id()).second;
                 parent = parent.target(flecs::ChildOf))
                if (parent.has<Animator>() && parent.get<Animator>().enabled)
                    throw std::runtime_error(
                        "Transform Animator cannot replace an animated model node pose");
        }
        const auto pose = state.clip->sample(time);
        auto at = candidate.find(id);
        if (at == candidate.end())
            throw std::runtime_error("Transform animation target has no local transform");
        if (state.clip->channels & 1)
            at->second.local.translation = pose.translation;
        if (state.clip->channels & 2)
            at->second.local.rotation = pose.rotation;
        if (state.clip->channels & 4)
            at->second.local.scale = pose.scale;
    }
    if (transforms && validator_)
        validator_(candidate);
    if (!commit)
        return;
    for (const auto& [id, time] : times) {
        (void)time;
        if (!load_errors_.contains(id))
            errors_.erase(id);
    }
    for (const auto& [id, time] : times) {
        (void)time;
        const auto& state = states_.at(id);
        auto entity = context_.world().entity(id);
        if (properties.contains(id))
            properties.at(id).commit(entity);
        const auto pose = state.clip->channels ? candidate.at(id).local : LocalTransform{};
        if (state.clip->channels & 1)
            entity.set<LocalTranslation>(pose.translation);
        if (state.clip->channels & 2)
            entity.set<LocalRotation>(pose.rotation);
        if (state.clip->channels & 4)
            entity.set<LocalScale>(pose.scale);
        auto& playback = states_.at(id);
        if (playback.discontinuous) {
            if (state.clip->channels)
                discontinuities_.insert(id);
            playback.previous = playback.time;
            playback.advance = 0;
        }
        playback.discontinuous = false;
    }
}
bool TransformAnimationPlayback::ready() const { return !pending_ && errors_.empty(); }
bool TransformAnimationPlayback::checkpoint_ready() const {
    return ready() && std::none_of(states_.begin(), states_.end(),
                                   [](const auto& entry) { return entry.second.discontinuous; });
}
bool TransformAnimationPlayback::initial_pose() {
    // AnimationRuntime synchronizes assets before this provider is evaluated.
    if (!errors_.empty())
        throw std::runtime_error("Invalid Transform Animator configuration");
    if (pending_)
        return false;
    std::map<flecs::entity_t, double> times;
    for (const auto& [id, state] : states_)
        if (state.clip)
            times.emplace(id, state.time);
    apply(times, true);
    return true;
}
void TransformAnimationPlayback::tick(double seconds) {
    // Reuse the owner's synchronization; do not query/admit every player twice.
    discontinuities_.clear();
    std::map<flecs::entity_t, double> times;
    for (auto& [id, state] : states_) {
        if (!state.clip)
            continue;
        state.previous = state.time;
        state.advance = 0;
        if (state.playing) {
            const double before = state.time;
            state.time += seconds * animation_timebase * state.config.playback_speed;
            state.advance = state.time - before;
            const auto held_crossed = [&](double from, double to) {
                const auto& keys = state.clip->keys;
                auto next = std::upper_bound(keys.begin(), keys.end(), from,
                                             [](double t, const auto& k) { return t < k.time; });
                for (; next != keys.end() && next->time <= to; ++next)
                    if (next != keys.begin() && (next - 1)->held)
                        return true;
                return false;
            };
            if (held_crossed(before, std::min(state.time, double(state.clip->duration))))
                state.discontinuous = true;
            if (state.config.loop && state.time >= state.clip->duration) {
                const auto first = state.clip->sample(0),
                           last = state.clip->sample(state.clip->duration);
                const auto& a = first.rotation;
                const auto& b = last.rotation;
                const double dot =
                    double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
                if (((state.clip->channels & 1) && first.translation != last.translation) ||
                    ((state.clip->channels & 4) && first.scale != last.scale) ||
                    ((state.clip->channels & 2) && std::abs(dot) < 1 - 1e-6) ||
                    held_crossed(0, std::fmod(state.time, double(state.clip->duration))))
                    state.discontinuous = true;
                if (state.clip->properties &&
                    state.clip->sample_properties(0) !=
                        state.clip->sample_properties(state.clip->duration))
                    state.discontinuous = true;
            }
            if (state.config.loop)
                state.time = std::fmod(state.time, double(state.clip->duration));
            else if (state.time >= state.clip->duration) {
                state.advance = double(state.clip->duration) - before;
                state.time = state.clip->duration;
                state.playing = false;
            }
        }
        times.emplace(id, state.time);
    }
    try {
        apply(times, true);
    } catch (const std::exception& e) {
        for (const auto& [id, time] : times) {
            (void)time;
            error(id, e.what());
        }
    }
}
void TransformAnimationPlayback::reset_presentation() {
    for (auto& [id, state] : states_) {
        (void)id;
        state.previous = state.time;
        state.advance = 0;
    }
}
Json TransformAnimationPlayback::presentation_properties(flecs::entity_t id, double alpha) const {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw std::runtime_error("Invalid property animation presentation alpha");
    const auto at = states_.find(id);
    if (at == states_.end() || !at->second.clip || !at->second.clip->properties ||
        errors_.contains(id))
        return nullptr;
    const auto& state = at->second;
    auto time = state.previous + state.advance * alpha;
    if (state.config.loop)
        time = std::fmod(time, double(state.clip->duration));
    auto values = AnimationPropertyState::from(context_.world().entity(id), state.clip->properties);
    values.sample(state.clip->properties, state.clip->sample_properties(time));
    return values.patch(state.clip->properties);
}
std::vector<std::uint64_t> TransformAnimationPlayback::take_discontinuities() {
    std::vector<std::uint64_t> result(discontinuities_.begin(), discontinuities_.end());
    discontinuities_.clear();
    return result;
}
Json TransformAnimationPlayback::checkpoint() const {
    if (!checkpoint_ready())
        throw std::runtime_error("Transform animation is not ready for recovery");
    auto result = Json::array();
    for (const auto& [id, state] : states_) {
        (void)id;
        if (state.clip)
            result.push_back({{"entity", state.entity},
                              {"clip", state.config.clip.id},
                              {"revision", state.clip.identity().revision},
                              {"time_us", state.time},
                              {"playing", state.playing}});
    }
    return result;
}
void TransformAnimationPlayback::restore(const Json& values) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do {
        synchronize();
        if (ready())
            break;
        if (!errors_.empty())
            throw std::runtime_error("Invalid transform animation recovery assets");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    if (!ready() || !values.is_array() || values.size() != states_.size())
        throw std::runtime_error("Transform animation recovery configuration differs");
    std::map<flecs::entity_t, double> times;
    std::map<flecs::entity_t, bool> playing;
    for (const auto& value : values) {
        const auto resolved = context_.resolve(value.at("entity").get<EntityRef>());
        if (resolved.state != WorldContext::ResolveState::Available ||
            !states_.contains(resolved.entity))
            throw std::runtime_error("Unresolved transform animation recovery target");
        const auto& state = states_.at(resolved.entity);
        const double time = value.at("time_us");
        if (!state.clip || value.at("clip") != Json(state.config.clip.id) ||
            value.at("revision") != state.clip.identity().revision || !std::isfinite(time) ||
            time < 0 || time > state.clip->duration || !value.at("playing").is_boolean() ||
            !times.emplace(resolved.entity, time).second)
            throw std::runtime_error("Transform animation recovery revision/time mismatch");
        playing.emplace(resolved.entity, value.at("playing").get<bool>());
    }
    apply(times, true);
    for (auto& [id, state] : states_) {
        state.time = times.at(id);
        state.playing = playing.at(id);
        state.previous = state.time;
        state.advance = 0;
    }
}
} // namespace forge
