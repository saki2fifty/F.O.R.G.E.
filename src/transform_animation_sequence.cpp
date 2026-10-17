#include "transform_animation_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <forge/render_components.hpp>
#include <stdexcept>
namespace forge {
void TransformAnimationPlayback::synchronize_sequences() {
    std::set<flecs::entity_t> alive;
    sequences_.each([&](flecs::entity entity, const AnimationSequence& config) {
        const auto ref = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !ref || !config.enabled)
            return;
        alive.insert(entity.id());
        try {
            if (alive.size() > 64 || !std::isfinite(config.duration) || config.duration < 1e-6 ||
                config.duration > 3600 || !std::isfinite(config.playback_speed) ||
                config.playback_speed < 0 || config.playback_speed > 4)
                throw std::runtime_error(
                    "Invalid Animation Sequence or controller limit (64) exceeded");
            if ((entity.has<Animator>() && entity.get<Animator>().enabled) ||
                (entity.has<TransformAnimator>() && entity.get<TransformAnimator>().enabled &&
                 entity.get<TransformAnimator>().clip.id))
                throw std::runtime_error(
                    "A sequence controller cannot also be an active clip player");
            auto at = sequence_states_.find(entity.id());
            if (at == sequence_states_.end() || at->second.entity != *ref) {
                SequenceState value;
                value.entity = *ref;
                value.playing = config.play_on_start;
                at = sequence_states_.insert_or_assign(entity.id(), value).first;
            }
            auto& state = at->second;
            const bool changed =
                state.config.duration != config.duration || state.config.loop != config.loop;
            state.discontinuous |= changed;
            state.config = config;
            state.time = std::min(state.time, config.duration * animation_timebase);
            if (changed) {
                state.previous = state.time;
                state.advance = 0;
            }
            if (sequence_errors_.erase(entity.id()) && !load_errors_.contains(entity.id()))
                errors_.erase(entity.id());
        } catch (const std::exception& e) {
            sequence_errors_.insert(entity.id());
            error(entity.id(), e.what());
        }
    });
    std::erase_if(sequence_states_, [&](const auto& pair) { return !alive.contains(pair.first); });
    std::erase_if(sequence_errors_, [&](auto id) { return !alive.contains(id); });
}
double TransformAnimationPlayback::sequence_time(const State& state, double time) const {
    return std::clamp(time - state.config.sequence_start_seconds * animation_timebase, 0.,
                      double(state.clip->duration));
}
std::map<flecs::entity_t, double> TransformAnimationPlayback::times_at(flecs::entity_t controller,
                                                                       double time) const {
    std::map<flecs::entity_t, double> result;
    for (const auto& [id, state] : states_)
        if (state.clip)
            result.emplace(id, controller && state.sequence == controller
                                   ? sequence_time(state, time)
                                   : state.time);
    return result;
}
std::optional<AnimationPlayback> TransformAnimationPlayback::playback(EntityRef ref) const {
    if (!ready())
        return {};
    const auto resolved = context_.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available ||
        errors_.contains(resolved.entity))
        return {};
    AnimationPlayback result;
    result.entity = ref;
    if (const auto at = sequence_states_.find(resolved.entity); at != sequence_states_.end()) {
        const auto& state = at->second;
        result.time = state.time / animation_timebase;
        result.duration = state.config.duration;
        result.speed = state.config.playback_speed;
        result.loop = state.config.loop;
        result.enabled = state.config.enabled;
        result.playing = state.playing && result.speed > 0;
    } else if (const auto at = states_.find(resolved.entity);
               at != states_.end() && at->second.clip) {
        const auto& state = at->second;
        result.clip = state.config.clip.id;
        result.time = state.time / animation_timebase;
        result.duration = double(state.clip->duration) / animation_timebase;
        result.speed = state.config.playback_speed;
        result.loop = state.config.loop;
        result.enabled = state.config.enabled;
        result.playing = state.playing && result.speed > 0;
        if (state.sequence) {
            const auto& sequence = sequence_states_.at(state.sequence);
            result.speed = sequence.config.playback_speed;
            result.loop = false;
            result.playing =
                sequence.playing && result.speed > 0 &&
                sequence.time >= state.config.sequence_start_seconds * animation_timebase &&
                state.time < state.clip->duration;
        }
    } else
        return {};
    result.progress = std::clamp(result.time / result.duration, 0., 1.);
    result.finished = !result.loop && result.time >= result.duration;
    return result;
}
bool TransformAnimationPlayback::seek(EntityRef ref, double seconds) {
    const auto status = playback(ref);
    if (!status)
        return false;
    if (!std::isfinite(seconds) || seconds < 0 || seconds > status->duration)
        throw std::runtime_error("Sequence/clip seek exceeds its duration");
    const auto id = context_.resolve(ref).entity;
    if (states_.contains(id) && states_.at(id).sequence)
        return false; // A track can be observed but only its controller can move its clock.
    const auto time =
        (status->loop && seconds == status->duration ? 0 : seconds) * animation_timebase;
    if (const auto group = sequence_states_.find(id); group != sequence_states_.end()) {
        const auto times = times_at(id, time);
        apply(times, false);
        auto& sequence = group->second;
        sequence.time = sequence.previous = time;
        sequence.advance = 0;
        sequence.discontinuous = true;
        for (auto& [target, state] : states_)
            if (state.sequence == id && state.clip) {
                state.time = state.previous = times.at(target);
                state.advance = 0;
                state.discontinuous = true;
            }
    } else {
        auto times = times_at(0, 0);
        times[id] = time;
        apply(times, false);
        auto& state = states_.at(id);
        state.time = state.previous = time;
        state.advance = 0;
        state.discontinuous = true;
    }
    return true;
}
bool TransformAnimationPlayback::control(EntityRef ref, AnimationControl command) {
    const auto status = playback(ref);
    if (!status)
        return false;
    const auto target = context_.resolve(ref).entity;
    if (states_.contains(target) && states_.at(target).sequence)
        return false;
    if (command == AnimationControl::Restart) {
        if (!seek(ref, 0))
            return false;
    } else if (command != AnimationControl::Play && command != AnimationControl::Pause)
        throw std::runtime_error("Unknown sequence/clip playback control");
    if (command == AnimationControl::Play && status->finished)
        return false;
    const auto id = context_.resolve(ref).entity;
    const bool playing = command != AnimationControl::Pause;
    if (auto group = sequence_states_.find(id); group != sequence_states_.end())
        group->second.playing = playing;
    else
        states_.at(id).playing = playing;
    return true;
}
Json TransformAnimationPlayback::sequence_checkpoint() const {
    auto result = Json::array();
    for (const auto& [id, state] : sequence_states_) {
        (void)id;
        result.push_back({{"entity", state.entity},
                          {"duration", state.config.duration},
                          {"loop", state.config.loop},
                          {"speed", state.config.playback_speed},
                          {"play_on_start", state.config.play_on_start},
                          {"time_us", state.time},
                          {"playing", state.playing}});
    }
    return result;
}
namespace {
flecs::entity_t scene_membership(flecs::entity entity) {
    for (auto parent = entity; parent; parent = parent.target(flecs::ChildOf))
        if (const auto membership = parent.target<SceneMember>())
            return membership.id();
    return 0;
}
} // namespace
void TransformAnimationPlayback::synchronize_camera_shots() {
    const auto previous_errors = std::move(shot_errors_);
    shot_errors_.clear();
    std::map<flecs::entity_t, std::vector<Shot>> candidate;
    std::set<flecs::entity_t> invalid_scenes;
    std::size_t count = 0;
    camera_shots_.each([&](flecs::entity entity, const CameraShot& config) {
        const auto ref = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !ref || !config.enabled || !config.sequence.entity)
            return;
        const auto membership = scene_membership(entity);
        try {
            if (++count > 256)
                throw std::runtime_error("Camera Shot limit (256) exceeded");
            const auto resolved = context_.resolve(config.sequence, membership);
            if (!membership || resolved.state != WorldContext::ResolveState::Available ||
                !context_.world().entity(resolved.entity).has<AnimationSequence>())
                throw std::runtime_error("Camera Shot sequence is unavailable in this Scene");
            const auto controller = context_.world().entity(resolved.entity);
            if (!controller.get<AnimationSequence>().enabled)
                return;
            if (!sequence_states_.contains(resolved.entity) ||
                sequence_errors_.contains(resolved.entity))
                throw std::runtime_error("Camera Shot sequence configuration is invalid");
            const auto camera = context_.resolve(config.camera, membership);
            if (camera.state != WorldContext::ResolveState::Available ||
                !context_.world().entity(camera.entity).has<Camera>())
                throw std::runtime_error("Camera Shot camera is unavailable in this Scene");
            const auto duration = sequence_states_.at(resolved.entity).config.duration;
            if (!std::isfinite(config.start_seconds) || !std::isfinite(config.end_seconds) ||
                config.start_seconds < 0 || config.start_seconds >= config.end_seconds ||
                config.end_seconds > duration)
                throw std::runtime_error(
                    "Camera Shot requires 0 <= start < end <= sequence duration");
            candidate[membership].push_back({config, *ref, resolved.entity, camera.entity});
        } catch (const std::exception& e) {
            invalid_scenes.insert(membership);
            shot_errors_.insert(entity.id());
            error(entity.id(), e.what());
        }
    });
    for (auto& [membership, shots] : candidate) {
        std::sort(shots.begin(), shots.end(), [](const Shot& a, const Shot& b) {
            return a.config.start_seconds < b.config.start_seconds;
        });
        for (std::size_t i = 1; i < shots.size(); ++i) {
            const bool different = shots[i].sequence != shots.front().sequence;
            if (!different && shots[i - 1].config.end_seconds <= shots[i].config.start_seconds)
                continue;
            invalid_scenes.insert(membership);
            for (const auto& shot : shots) {
                const auto id = context_.resolve(shot.entity, membership).entity;
                shot_errors_.insert(id);
                error(id, different ? "Only one camera-shot sequence may direct a loaded Scene"
                                    : "Camera Shot intervals overlap in this Scene sequence");
            }
            break;
        }
    }
    for (auto membership : invalid_scenes)
        candidate.erase(membership);
    shot_scenes_.swap(candidate);
    for (auto id : previous_errors)
        if (!shot_errors_.contains(id) && !load_errors_.contains(id) &&
            !sequence_errors_.contains(id))
            errors_.erase(id);
}
Json TransformAnimationPlayback::camera_presentation(flecs::entity_t id, double alpha) const {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw std::runtime_error("Invalid camera-shot presentation alpha");
    if (shot_scenes_.empty())
        return nullptr;
    const auto entity = context_.world().entity(id);
    if (!entity.has<Camera>())
        return nullptr;
    const auto at = shot_scenes_.find(scene_membership(entity));
    if (at == shot_scenes_.end())
        return nullptr;
    const auto& shots = at->second;
    const auto& group = sequence_states_.at(shots.front().sequence);
    const auto duration = group.config.duration * animation_timebase;
    // The existing sequence clock owns interpolation. A wrap snaps all clip
    // presentation endpoints; camera selection must sample that same wrapped pose.
    const bool wrapped = group.config.loop && group.previous + group.advance >= duration;
    const auto time =
        group.advance == 0 || wrapped ? group.time : group.previous + group.advance * alpha;
    for (const auto& shot : shots) {
        const auto start = shot.config.start_seconds * animation_timebase;
        const auto end = shot.config.end_seconds * animation_timebase;
        if (time >= start && (time < end || (time == duration && end == duration)))
            return id == shot.camera;
    }
    return nullptr; // A gap restores ordinary authored camera composition.
}
Json TransformAnimationPlayback::camera_shots_checkpoint() const {
    auto result = Json::array();
    for (const auto& [membership, shots] : shot_scenes_) {
        (void)membership;
        for (const auto& shot : shots)
            result.push_back({{"entity", shot.entity},
                              {"sequence", shot.config.sequence},
                              {"camera", shot.config.camera},
                              {"start_seconds", shot.config.start_seconds},
                              {"end_seconds", shot.config.end_seconds}});
    }
    // Native entity/membership allocation order is not persistent identity.
    std::sort(result.begin(), result.end(), [](const Json& a, const Json& b) {
        return a.at("entity").dump() < b.at("entity").dump();
    });
    return result;
}
} // namespace forge
