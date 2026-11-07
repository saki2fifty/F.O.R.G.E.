#include "animation_event_range.hpp"
#include "animation_sequence_binding.hpp"
#include "builtins.hpp"
#include "camera_shot_blend.hpp"
#include "json_row_index.hpp"
#include "render_values.hpp"
#include "transform_animation_runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/application_state_activation.hpp>
#include <forge/audio_components.hpp>
#include <forge/render_components.hpp>
#include <functional>
#include <stdexcept>
#include <tuple>
namespace forge {
flecs::entity_t TransformAnimationPlayback::scene_membership(flecs::entity entity) {
    for (auto parent = entity; parent; parent = parent.target(flecs::ChildOf))
        if (const auto membership = parent.target<SceneMember>())
            return membership.id();
    return 0;
}
double TransformAnimationPlayback::controller_time(const SequenceState& state,
                                                   double root_time) const {
    return state.mapping->sample(root_time);
}
TransformAnimationPlayback::SequenceStep
TransformAnimationPlayback::sequence_step(const SequenceState& state, double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Sequence step requires finite nonnegative seconds");
    SequenceStep next{state.time, 0, state.playing, state.discontinuous};
    if (!state.playing)
        return next;
    const auto duration = state.config.duration * animation_timebase;
    next.advance = seconds * animation_timebase * state.config.playback_speed;
    next.time += next.advance;
    if (state.config.loop && next.time >= duration) {
        next.time = std::fmod(next.time, duration);
        next.discontinuous = true;
    } else if (!state.config.loop && next.time >= duration) {
        next.advance = duration - state.time;
        next.time = duration;
        next.playing = false;
    }
    return next;
}
TransformAnimationPlayback::SequenceTimes
TransformAnimationPlayback::next_sequence_times(double seconds) const {
    SequenceTimes times;
    for (const auto& [id, state] : sequence_states_)
        if (!state.parent)
            times.emplace(id, sequence_step(state, seconds).time);
    return times;
}
void TransformAnimationPlayback::validate_sequence_sample(const SequenceTimes& times) {
    if (pending_)
        throw AnimationMaterialPending("Waiting for the complete sequence candidate");
    if (!errors_.empty())
        throw std::runtime_error("Sequence candidate is invalid: " + errors_.begin()->second);
    for (const auto& [id, time] : times) {
        const auto found = sequence_states_.find(id);
        if (found == sequence_states_.end() || found->second.parent || !std::isfinite(time) ||
            time < 0 || time > found->second.config.duration * animation_timebase)
            throw std::runtime_error("Invalid proposed root sequence time");
    }
    apply(times_at(times), false, times);
}
double TransformAnimationPlayback::root_presentation_time(const SequenceState& state,
                                                          double alpha) const {
    const auto& root = sequence_states_.at(state.root);
    const bool wrapped = root.config.loop &&
                         root.previous + root.advance >= root.config.duration * animation_timebase;
    const bool reset = state.mapping->reset_crossed(root.previous, root.previous + root.advance);
    return root.advance == 0 || wrapped || reset ? root.time : root.previous + root.advance * alpha;
}
double TransformAnimationPlayback::controller_presentation_time(flecs::entity_t id,
                                                                double alpha) const {
    const auto& state = sequence_states_.at(id);
    return controller_time(state, root_presentation_time(state, alpha));
}
flecs::entity_t TransformAnimationPlayback::resolve_sequence(flecs::entity entity,
                                                             EntityRef reference) const {
    const auto membership = scene_membership(entity);
    const auto resolved = context_.resolve(reference, membership);
    if (!membership || resolved.state != WorldContext::ResolveState::Available ||
        !context_.world().entity(resolved.entity).has<AnimationSequence>() ||
        scene_membership(context_.world().entity(resolved.entity)) != membership)
        throw std::runtime_error("Sequence controller is unavailable in this Scene");
    if (inactive_sequences_.contains(resolved.entity))
        return 0;
    if (!sequence_states_.contains(resolved.entity) || sequence_errors_.contains(resolved.entity))
        throw std::runtime_error("Sequence controller configuration is invalid");
    return resolved.entity;
}
double TransformAnimationPlayback::sequence_duration(flecs::entity_t id) const {
    return sequence_states_.at(id).config.duration;
}
TransformAnimationPlayback::SequenceInterval
TransformAnimationPlayback::sequence_interval(flecs::entity_t id) const {
    const auto& state = sequence_states_.at(id);
    const auto& root = sequence_states_.at(state.root);
    return {root.previous / animation_timebase,
            root.advance / animation_timebase,
            root.config.duration,
            root.config.loop,
            root.playing,
            state.discontinuous || root.discontinuous,
            state.root,
            state.mapping.get()};
}
double TransformAnimationPlayback::sequence_sample_time(flecs::entity_t id, double alpha,
                                                        const SequenceTimes& seek) const {
    const auto& state = sequence_states_.at(id);
    const auto proposed = seek.find(state.root);
    const double time = proposed != seek.end() ? controller_time(state, proposed->second)
                                               : controller_presentation_time(id, alpha);
    return time / animation_timebase;
}
bool TransformAnimationPlayback::has_nested_sequences() const {
    return std::any_of(sequence_states_.begin(), sequence_states_.end(),
                       [](const auto& item) { return item.second.parent != 0; });
}
bool TransformAnimationPlayback::has_sequence_arrangements() const {
    return std::any_of(states_.begin(), states_.end(),
                       [](const auto& item) { return bool(item.second.arrangement); });
}
bool TransformAnimationPlayback::has_sequence_mixing() const {
    return std::any_of(states_.begin(), states_.end(),
                       [](const auto& item) { return item.second.config.sequence_multiway; });
}
bool TransformAnimationPlayback::has_sequence_rates() const {
    return std::any_of(sequence_states_.begin(), sequence_states_.end(), [](const auto& item) {
        return item.second.parent && item.second.config.sequence_rate != 1;
    });
}
bool TransformAnimationPlayback::has_sequence_loops() const {
    return std::any_of(sequence_states_.begin(), sequence_states_.end(), [](const auto& item) {
        return item.second.parent && item.second.config.sequence_loop;
    });
}
bool TransformAnimationPlayback::has_sequence_placements() const {
    return std::any_of(sequence_states_.begin(), sequence_states_.end(), [](const auto& item) {
        return item.second.parent && !item.second.config.sequence_extra_placements.empty();
    });
}
void TransformAnimationPlayback::prepare_sequence_preview_root(EntityRef reference) {
    if (!states_.empty())
        throw std::runtime_error("Sequence preview must be composed before clip admission");
    synchronize_sequences();
    const auto resolved = context_.resolve(reference);
    if (resolved.state != WorldContext::ResolveState::Available ||
        !context_.world().entity(resolved.entity).has<AnimationSequence>())
        throw std::runtime_error("Animation Sequence binding is unavailable");
    if (sequence_errors_.contains(resolved.entity))
        throw std::runtime_error(errors_.at(resolved.entity));
    // Host composition will now isolate this root in its copied World. Do not
    // retain playback intent created while validating the original arrangement.
    sequence_states_.clear();
}
void TransformAnimationPlayback::synchronize_sequences() {
    ApplicationStateActivation conditions(context_);
    std::set<flecs::entity_t> alive, reset;
    std::map<flecs::entity_t, SequenceState> candidates;
    std::map<flecs::entity_t, std::string> invalid;
    inactive_sequences_.clear();
    sequences_.each([&](flecs::entity entity, const AnimationSequence& config) {
        const auto reference = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !reference)
            return;
        if (!config.enabled || !conditions.active(entity.id())) {
            inactive_sequences_.insert(entity.id());
            return;
        }
        alive.insert(entity.id());
        try {
            if (alive.size() > 64 || !std::isfinite(config.duration) || config.duration < 1e-6 ||
                config.duration > 3600 || !std::isfinite(config.playback_speed) ||
                config.playback_speed < 0 || config.playback_speed > 4 ||
                bool(config.sequence.scene) != bool(config.sequence.entity) ||
                !std::isfinite(config.sequence_start_seconds) ||
                config.sequence_start_seconds < 0 || config.sequence_start_seconds > 3600)
                throw std::runtime_error(
                    "Invalid Animation Sequence or controller limit (64) exceeded");
            if ((entity.has<Animator>() && entity.get<Animator>().enabled) ||
                (entity.has<TransformAnimator>() && entity.get<TransformAnimator>().enabled &&
                 entity.get<TransformAnimator>().clip.id))
                throw std::runtime_error(
                    "A sequence controller cannot also be an active clip player");
            SequenceState state;
            animation_detail::validate_sequence_placements(config);
            const auto old = sequence_states_.find(entity.id());
            if (old != sequence_states_.end() && old->second.entity == *reference &&
                old->second.config.sequence == config.sequence)
                state = old->second;
            else {
                state.entity = *reference;
                state.playing = config.play_on_start;
            }
            if (old == sequence_states_.end() || old->second.entity != *reference ||
                old->second.config.duration != config.duration ||
                old->second.config.loop != config.loop ||
                old->second.config.sequence != config.sequence)
                reset.insert(entity.id());
            state.config = config;
            state.time = std::min(state.time, config.duration * animation_timebase);
            candidates.emplace(entity.id(), std::move(state));
        } catch (const std::exception& error) {
            invalid[entity.id()] = error.what();
        }
    });
    enum class Visit { Unseen, Visiting, Ready, Inactive, Invalid };
    std::map<flecs::entity_t, Visit> visits;
    std::function<bool(flecs::entity_t)> visit = [&](flecs::entity_t id) {
        auto& mark = visits[id];
        if (mark == Visit::Ready)
            return true;
        if (mark == Visit::Inactive || mark == Visit::Invalid)
            return false;
        if (mark == Visit::Visiting) {
            invalid[id] = "Nested sequence bindings contain a cycle";
            return false;
        }
        mark = Visit::Visiting;
        try {
            auto& state = candidates.at(id);
            state.parent = 0;
            state.root = id;
            const auto old = sequence_states_.find(id);
            if (state.config.sequence.entity) {
                const auto entity = context_.world().entity(id);
                const auto membership = scene_membership(entity);
                const auto resolved = context_.resolve(state.config.sequence, membership);
                if (!membership || resolved.state != WorldContext::ResolveState::Available)
                    throw std::runtime_error("Parent sequence is unavailable in this Scene");
                const auto parent = context_.world().entity(resolved.entity);
                if (!parent.has<AnimationSequence>() || scene_membership(parent) != membership)
                    throw std::runtime_error(
                        "Parent must be an Animation Sequence in the same Scene");
                if (inactive_sequences_.contains(resolved.entity)) {
                    inactive_sequences_.insert(id);
                    mark = Visit::Inactive;
                    return false;
                }
                if (!candidates.contains(resolved.entity))
                    throw std::runtime_error("Parent sequence configuration is invalid");
                if (!visit(resolved.entity)) {
                    if (invalid.contains(resolved.entity))
                        throw std::runtime_error("Parent sequence binding is invalid or cyclic");
                    inactive_sequences_.insert(id);
                    mark = Visit::Inactive;
                    return false;
                }
                const auto& placement = candidates.at(resolved.entity);
                if (state.config.sequence_start_seconds > placement.config.duration)
                    throw std::runtime_error("Nested start exceeds its parent sequence duration");
                state.parent = resolved.entity;
                state.root = placement.root;
                const bool geometry_changed =
                    !state.mapping || old == sequence_states_.end() ||
                    old->second.parent != state.parent ||
                    old->second.config.duration != state.config.duration ||
                    old->second.config.sequence_start_seconds !=
                        state.config.sequence_start_seconds ||
                    old->second.config.sequence_rate != state.config.sequence_rate ||
                    old->second.config.sequence_loop != state.config.sequence_loop ||
                    old->second.config.sequence_extra_placements !=
                        state.config.sequence_extra_placements ||
                    !sequence_states_.contains(state.parent) ||
                    sequence_states_.at(state.parent).mapping != placement.mapping;
                if (geometry_changed) {
                    static_assert(sequence_placement_limit ==
                                  detail::SequenceClockMapping::maximum_placements);
                    std::array<detail::SequenceClockMapping::Placement, sequence_placement_limit>
                        placements{};
                    placements[0] = {state.config.sequence_start_seconds,
                                     state.config.sequence_rate, state.config.sequence_loop};
                    for (std::size_t i = 0; i < state.config.sequence_extra_placements.size();
                         ++i) {
                        const auto& extra = state.config.sequence_extra_placements[i];
                        placements[i + 1] = {extra.start_seconds, extra.rate, extra.loop};
                    }
                    for (const auto& parent_span : placement.mapping->spans())
                        for (std::size_t i = 0; i <= state.config.sequence_extra_placements.size();
                             ++i) {
                            const double rate = parent_span.rate * placements[i].rate;
                            if (!std::isfinite(rate) || rate < 1. / 64 || rate > 64)
                                throw std::runtime_error(
                                    "Cumulative sequence placement rate must be within 1/64–64");
                        }
                    state.mapping = std::make_shared<const detail::SequenceClockMapping>(
                        detail::SequenceClockMapping::child(
                            *placement.mapping, state.config.duration,
                            std::span(placements)
                                .first(1 + state.config.sequence_extra_placements.size())));
                }
                state.time = controller_time(state, candidates.at(state.root).time);
                state.playing = state.entry_pending = false;
            } else if (!state.mapping || old == sequence_states_.end() || old->second.parent ||
                       old->second.config.duration != state.config.duration) {
                state.mapping = std::make_shared<const detail::SequenceClockMapping>(
                    detail::SequenceClockMapping::root(state.config.duration, animation_timebase));
            }
            const bool changed = reset.contains(id) || old == sequence_states_.end() ||
                                 old->second.parent != state.parent ||
                                 old->second.root != state.root ||
                                 old->second.mapping != state.mapping;
            state.discontinuous |= changed;
            if (changed) {
                state.previous = state.time;
                state.advance = 0;
            }
            mark = Visit::Ready;
            return true;
        } catch (const std::exception& error) {
            invalid[id] = error.what();
            mark = Visit::Invalid;
            return false;
        }
    };
    for (const auto& [id, candidate] : candidates) {
        (void)candidate;
        (void)visit(id);
    }
    for (auto& [id, candidate] : candidates)
        if (visits[id] == Visit::Ready)
            sequence_states_.insert_or_assign(id, std::move(candidate));
    std::erase_if(sequence_states_, [&](const auto& item) {
        return !alive.contains(item.first) || inactive_sequences_.contains(item.first);
    });
    // Retained invalid arrangements may have lost an explicitly disabled ancestor.
    // Retire that chain before any reader can observe a dangling derived root.
    for (unsigned remaining = 64; remaining > 0; --remaining) {
        const auto removed = std::erase_if(sequence_states_, [&](const auto& item) {
            return item.second.parent && (!sequence_states_.contains(item.second.parent) ||
                                          !sequence_states_.contains(item.second.root));
        });
        if (!removed)
            break;
    }
    const auto previous_errors = sequence_errors_;
    sequence_errors_.clear();
    for (const auto& [id, message] : invalid) {
        sequence_errors_.insert(id);
        error(id, message);
    }
    for (auto id : previous_errors)
        if (!sequence_errors_.contains(id) && !load_errors_.contains(id) &&
            !shot_errors_.contains(id))
            errors_.erase(id);
}
double TransformAnimationPlayback::sequence_time(const State& state, double time) const {
    if (state.arrangement)
        return state.arrangement->relative_time(time);
    return state.span ? state.span->relative_time(time)
                      : std::clamp(time - state.config.sequence_start_seconds * animation_timebase,
                                   0., double(state.clip->duration));
}
std::map<flecs::entity_t, double>
TransformAnimationPlayback::times_at(const SequenceTimes& roots) const {
    std::map<flecs::entity_t, double> result;
    for (const auto& [id, state] : states_) {
        if (!state.clip)
            continue;
        const auto group = sequence_states_.find(state.sequence);
        const auto proposed =
            group != sequence_states_.end() ? roots.find(group->second.root) : roots.end();
        result.emplace(id,
                       proposed != roots.end()
                           ? sequence_time(state, controller_time(group->second, proposed->second))
                           : state.time);
    }
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
        const auto& root = sequence_states_.at(state.root);
        result.time = state.time / animation_timebase;
        result.duration = state.config.duration;
        result.speed = root.config.playback_speed * state.mapping->rate(root.time);
        result.loop = state.parent ? state.mapping->loop(root.time) : state.config.loop;
        result.enabled = state.config.enabled;
        result.playing =
            root.playing && result.speed > 0 && (!state.parent || state.mapping->active(root.time));
    } else if (const auto at = states_.find(resolved.entity);
               at != states_.end() && at->second.clip) {
        const auto& state = at->second;
        result.clip = state.config.clip.id;
        result.time = state.time / animation_timebase;
        result.duration = clip_span_duration(state) / animation_timebase;
        if (state.arrangement) {
            const auto sample = clip_sample(state, state.time);
            result.clip = clip_reference(state, sample.primary).id;
            result.blend_clip = clip_reference(state, sample.next).id;
            result.blend_weight = sample.weight;
        } else if (state.span && state.next_clip) {
            result.blend_clip = state.config.sequence_next_clip.id;
            result.blend_weight = state.span->next_weight(state.time);
        }
        result.speed = state.config.playback_speed;
        result.loop = state.config.loop;
        result.enabled = state.config.enabled;
        result.playing = state.playing && result.speed > 0;
        if (state.sequence) {
            const auto& sequence = sequence_states_.at(state.sequence);
            const auto& root = sequence_states_.at(sequence.root);
            result.speed = root.config.playback_speed * sequence.mapping->rate(root.time);
            result.loop = false;
            result.playing =
                root.playing && result.speed > 0 && sequence.mapping->active(root.time) &&
                sequence.time >= state.config.sequence_start_seconds * animation_timebase &&
                state.time < clip_span_duration(state);
        }
    } else
        return {};
    result.progress = std::clamp(result.time / result.duration, 0., 1.);
    result.finished = !result.loop && result.time >= result.duration;
    if (const auto at = sequence_states_.find(resolved.entity);
        at != sequence_states_.end() && at->second.parent &&
        !at->second.config.sequence_extra_placements.empty())
        result.finished &= at->second.mapping->ended(sequence_states_.at(at->second.root).time);
    return result;
}
EntityRef TransformAnimationPlayback::parent_sequence(EntityRef ref) const {
    if (!ready())
        return {};
    const auto resolved = context_.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        return {};
    const auto found = sequence_states_.find(resolved.entity);
    return found != sequence_states_.end() && found->second.parent ? found->second.config.sequence
                                                                   : EntityRef{};
}
Json TransformAnimationPlayback::sequence_placement_status(EntityRef ref) const {
    if (!ready())
        return nullptr;
    const auto resolved = context_.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        return nullptr;
    const auto found = sequence_states_.find(resolved.entity);
    if (found == sequence_states_.end() || !found->second.parent ||
        found->second.config.sequence_extra_placements.empty())
        return nullptr;
    const auto& state = found->second;
    const auto& root = sequence_states_.at(state.root);
    auto placements = Json::array({{{"start_seconds", state.config.sequence_start_seconds},
                                    {"rate", state.config.sequence_rate},
                                    {"loop", state.config.sequence_loop}}});
    const auto encoded = detail::encode_builtin_value(state.config);
    for (const auto& extra : encoded.at("sequence_extra_placements"))
        placements.push_back(extra);
    const auto* span = state.mapping->at(root.time);
    return {{"sequence", state.config.sequence},
            {"duration_seconds", state.config.duration},
            {"active_placement", span ? span->placement : 0},
            {"active", state.mapping->active(root.time)},
            {"placements", std::move(placements)}};
}
Json TransformAnimationPlayback::sequence_clip_status(EntityRef ref) const {
    if (!ready())
        return nullptr;
    const auto resolved = context_.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        return nullptr;
    const auto found = states_.find(resolved.entity);
    if (found == states_.end() || !found->second.clip || !found->second.sequence)
        return nullptr;
    const auto& state = found->second;
    if (state.arrangement) {
        const auto sample = clip_sample(state, state.time);
        const auto description = [&](std::size_t index) {
            return Json{{"clip", clip_reference(state, index).id},
                        {"start_seconds", clip_start_seconds(state, index)},
                        {"duration_seconds",
                         double(clip_view(state, index).clip.duration) / animation_timebase}};
        };
        auto result = description(sample.primary);
        result["sequence"] = state.config.sequence;
        result["next"] = description(sample.next);
        result["next"]["weight"] = sample.weight;
        result["active_primary"] = sample.primary;
        result["active_next"] = sample.next;
        result["clips"] = Json::array();
        for (std::size_t i = 0; i < state.arrangement->size(); ++i)
            result["clips"].push_back(description(i));
        if (state.config.sequence_multiway) {
            result["multiway"] = true;
            result["summary_only"] = sample.summary_only;
            const auto weights = state.arrangement->influences(state.time);
            for (std::size_t i = 0; i < weights.count; ++i) {
                result["clips"][i]["weight"] = weights.weights[i];
                result["clips"][i]["time_seconds"] =
                    state.arrangement->clip_time_us(i, state.time) / animation_timebase;
            }
        }
        return result;
    }
    Json result{{"clip", state.config.clip.id},
                {"sequence", state.config.sequence},
                {"start_seconds", state.config.sequence_start_seconds},
                {"duration_seconds", double(state.clip->duration) / animation_timebase}};
    if (state.span && state.next_clip)
        result["next"] = {
            {"clip", state.config.sequence_next_clip.id},
            {"start_seconds", state.config.sequence_next_start_seconds},
            {"duration_seconds", double(state.next_clip->duration) / animation_timebase},
            {"weight", state.span->next_weight(state.time)}};
    return result;
}
bool TransformAnimationPlayback::seek(EntityRef ref, double seconds) {
    const auto status = playback(ref);
    const auto selected = context_.resolve(ref);
    if (selected.state == WorldContext::ResolveState::Available) {
        const auto group = sequence_states_.find(selected.entity);
        if (group != sequence_states_.end() && group->second.parent)
            return false;
    }
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
        const auto times = times_at({{id, time}});
        apply(times, false, {{id, time}});
        for (auto& [controller, child] : sequence_states_) {
            (void)controller;
            if (child.root != id)
                continue;
            child.time = child.previous = controller_time(child, time);
            child.advance = 0;
            child.discontinuous = true;
            child.entry_pending = false;
        }
        for (auto& [target, state] : states_)
            if (state.clip && sequence_states_.contains(state.sequence) &&
                sequence_states_.at(state.sequence).root == id) {
                state.time = state.previous = times.at(target);
                state.advance = 0;
                state.discontinuous = true;
            }
    } else {
        auto times = times_at({});
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
    if (sequence_states_.contains(target) && sequence_states_.at(target).parent)
        return false;
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
    if (auto group = sequence_states_.find(id); group != sequence_states_.end()) {
        group->second.playing = playing;
        if (command == AnimationControl::Restart)
            group->second.entry_pending = true;
    } else
        states_.at(id).playing = playing;
    return true;
}
Json TransformAnimationPlayback::sequence_checkpoint(unsigned profile, bool rates, bool loops,
                                                     bool placements) const {
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
        if (profile >= 5)
            result.back()["entry_pending"] = state.entry_pending;
        if (profile >= 7) {
            result.back()["sequence"] =
                state.config.sequence.entity ? Json(state.config.sequence) : Json();
            result.back()["sequence_start_seconds"] = state.config.sequence_start_seconds;
        }
        if (rates)
            result.back()["sequence_rate"] = state.config.sequence_rate;
        if (loops)
            result.back()["sequence_loop"] = state.config.sequence_loop;
        if (placements)
            result.back()["sequence_extra_placements"] =
                detail::encode_builtin_value(state.config).at("sequence_extra_placements");
    }
    return result;
}
void TransformAnimationPlayback::synchronize_camera_shots() {
    ApplicationStateActivation conditions(context_);
    const auto previous_errors = std::move(shot_errors_);
    shot_errors_.clear();
    std::map<flecs::entity_t, std::vector<Shot>> candidate;
    std::set<flecs::entity_t> invalid_scenes;
    std::size_t count = 0;
    camera_shots_.each([&](flecs::entity entity, const CameraShot& config) {
        const auto ref = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !ref || !config.enabled || !config.sequence.entity ||
            !conditions.active(entity.id()))
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
            if (!controller.get<AnimationSequence>().enabled ||
                !conditions.active(controller.id()) ||
                inactive_sequences_.contains(controller.id()))
                return;
            if (!sequence_states_.contains(resolved.entity) ||
                sequence_errors_.contains(resolved.entity))
                throw std::runtime_error("Camera Shot sequence configuration is invalid");
            const auto camera = context_.resolve(config.camera, membership);
            if (camera.state != WorldContext::ResolveState::Available ||
                !context_.world().entity(camera.entity).has<Camera>())
                throw std::runtime_error("Camera Shot camera is unavailable in this Scene");
            validate_shot_camera(context_.world().entity(camera.entity).get<Camera>());
            const auto duration = sequence_states_.at(resolved.entity).config.duration;
            if (!std::isfinite(config.start_seconds) || !std::isfinite(config.end_seconds) ||
                config.start_seconds < 0 || config.start_seconds >= config.end_seconds ||
                config.end_seconds > duration)
                throw std::runtime_error(
                    "Camera Shot requires 0 <= start < end <= sequence duration");
            if (!std::isfinite(config.blend_in_seconds) || config.blend_in_seconds < 0 ||
                config.blend_in_seconds > config.end_seconds - config.start_seconds)
                throw std::runtime_error("Camera Shot blend must fit within its interval");
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
    for (const auto& [membership, shots] : candidate) {
        if (invalid_scenes.contains(membership))
            continue;
        for (std::size_t i = 1; i < shots.size(); ++i) {
            const auto& incoming = shots[i];
            if (incoming.config.blend_in_seconds == 0 ||
                shots[i - 1].config.end_seconds != incoming.config.start_seconds)
                continue;
            try {
                validate_camera_shot_blend(
                    context_.world().entity(shots[i - 1].camera).get<Camera>(),
                    context_.world().entity(incoming.camera).get<Camera>());
            } catch (const std::exception& e) {
                invalid_scenes.insert(membership);
                const auto id = context_.resolve(incoming.entity, membership).entity;
                shot_errors_.insert(id);
                error(id, e.what());
            }
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
TransformAnimationPlayback::ShotSample
TransformAnimationPlayback::sample_camera_shot(flecs::entity_t membership, double alpha) const {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
        throw std::runtime_error("Invalid camera-shot presentation alpha");
    const auto at = shot_scenes_.find(membership);
    if (at == shot_scenes_.end())
        return {};
    const auto& shots = at->second;
    const auto& group = sequence_states_.at(shots.front().sequence);
    const auto duration = group.config.duration * animation_timebase;
    const auto root_time = root_presentation_time(group, alpha);
    if (!group.mapping->active(root_time, true))
        return {};
    const auto time = controller_presentation_time(shots.front().sequence, alpha);
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const auto& shot = shots[i];
        const auto start = shot.config.start_seconds * animation_timebase;
        const auto end = shot.config.end_seconds * animation_timebase;
        if (time < start || !(time < end || (time == duration && end == duration)))
            continue;
        ShotSample result{&shot};
        const auto span = shot.config.blend_in_seconds * animation_timebase;
        if (i && span > 0 && time < start + span &&
            shots[i - 1].config.end_seconds == shot.config.start_seconds) {
            result.previous = &shots[i - 1];
            const double linear = std::clamp((time - start) / span, 0., 1.);
            result.weight = linear * linear * (3 - 2 * linear);
        }
        return result;
    }
    return {};
}
Json TransformAnimationPlayback::camera_presentation(flecs::entity_t id, double alpha) const {
    const auto entity = context_.world().entity(id);
    if (!entity.has<Camera>() || entity.get<Camera>().render_texture.id)
        return nullptr;
    const auto sample = sample_camera_shot(scene_membership(entity), alpha);
    return sample.current ? Json(id == sample.current->camera) : Json();
}
Json TransformAnimationPlayback::tune_camera_shot(EntityRef target, const Json& value) {
    for (auto& [membership, shots] : shot_scenes_) {
        for (std::size_t i = 0; i < shots.size(); ++i) {
            auto& shot = shots[i];
            if (shot.entity != target)
                continue;
            const auto& codecs = detail::builtins();
            const auto codec = std::find_if(codecs.begin(), codecs.end(), [](const auto& entry) {
                return std::string_view(entry.name) == "forge.camera_shot";
            });
            const auto resolved = context_.resolve(target, membership);
            if (resolved.state != WorldContext::ResolveState::Available)
                throw std::runtime_error("Live Camera Shot target is unavailable");
            auto actor = context_.world().entity(resolved.entity);
            auto candidate = codec->read(actor, true);
            candidate["blend_in_seconds"] = value;
            detail::validate_components({{"forge.camera_shot", candidate}});
            const auto config = std::get<CameraShot>(codec->decode(candidate));
            detail::validate_reflected_value(actor, config);
            if (config.blend_in_seconds > 0 && i &&
                shots[i - 1].config.end_seconds == config.start_seconds)
                validate_camera_shot_blend(
                    context_.world().entity(shots[i - 1].camera).get<Camera>(),
                    context_.world().entity(shot.camera).get<Camera>());
            actor.set(config);
            shot.config = config;
            return config.blend_in_seconds;
        }
        (void)membership;
    }
    throw std::runtime_error("Live Camera Shot tuning needs an admitted enabled shot");
}
bool TransformAnimationPlayback::has_camera_blends() const {
    for (const auto& [membership, shots] : shot_scenes_) {
        (void)membership;
        for (const auto& shot : shots)
            if (shot.config.blend_in_seconds > 0)
                return true;
    }
    return false;
}
void TransformAnimationPlayback::project_camera_shots(Json& document, double alpha) const {
    if (!has_camera_blends())
        return;
    const auto rows = detail::index_json_rows(document.at("entities"));
    std::map<std::string, RenderCamera> candidate;
    const auto read = [&](flecs::entity_t id) {
        const auto ref = context_.reference(id);
        if (!ref || !rows.contains(ref->entity.str()))
            throw std::runtime_error("Camera Shot view is missing from copied presentation");
        const auto& row = *rows.at(ref->entity.str());
        if (!row.value("spatial_resolved", false))
            throw std::runtime_error("Camera Shot view has an unresolved world pose");
        RenderCamera value{
            ref->entity, detail::camera_value(row.at("components").at("forge.camera")), {}};
        value.world.m = row.at("world_affine").get<std::array<double, 12>>();
        return value;
    };
    for (const auto& [membership, shots] : shot_scenes_) {
        (void)shots;
        const auto sample = sample_camera_shot(membership, alpha);
        if (!sample.previous || sample.previous->camera == sample.current->camera)
            continue;
        auto value = blend_camera_shot(read(sample.previous->camera), read(sample.current->camera),
                                       sample.weight);
        candidate.emplace(value.entity.str(), std::move(value));
    }
    // Build every view before publishing any override into this copied document.
    for (auto& row : document.at("entities")) {
        const auto at = candidate.find(row.at("id").get_ref<const std::string&>());
        if (at == candidate.end())
            continue;
        const auto values = detail::render_value(at->second.camera);
        for (const auto* field : {"vertical_fov", "orthographic_height", "orthographic_width",
                                  "near_plane", "far_plane"})
            row["components"]["forge.camera"][field] = values.at(field);
        row["world_affine"] = at->second.world.m;
    }
}
Json TransformAnimationPlayback::camera_shots_checkpoint(bool transitions) const {
    auto result = Json::array();
    for (const auto& [membership, shots] : shot_scenes_) {
        (void)membership;
        for (const auto& shot : shots) {
            result.push_back({{"entity", shot.entity},
                              {"sequence", shot.config.sequence},
                              {"camera", shot.config.camera},
                              {"start_seconds", shot.config.start_seconds},
                              {"end_seconds", shot.config.end_seconds}});
            if (transitions)
                result.back()["blend_in_seconds"] = shot.config.blend_in_seconds;
        }
    }
    // Native entity/membership allocation order is not persistent identity.
    std::sort(result.begin(), result.end(), [](const Json& a, const Json& b) {
        return a.at("entity").dump() < b.at("entity").dump();
    });
    return result;
}
void TransformAnimationPlayback::cue_error(Cue& cue, const std::string& text, bool binding) {
    if (cue.error == text && cue.binding_error == binding)
        return;
    cue.error = text;
    cue.binding_error = binding;
    Diagnostic diagnostic{Severity::Error, "animation.audio_cue", text, {}};
    diagnostic.context.entity = cue.entity.entity;
    if (cue.config.source.scene && cue.config.source.entity)
        diagnostic.context.related_entity = cue.config.source;
    diagnostic.context.module = "forge.animation";
    diagnostic.context.property = "forge.audio_cue";
    context_.services().emit(std::move(diagnostic));
}
void TransformAnimationPlayback::synchronize_audio_cues() {
    ApplicationStateActivation conditions(context_);
    // This is a total component budget, including unbound/disabled authored cues.
    if (audio_cues_.count() > 256) {
        cue_states_.clear();
        if (!cue_over_capacity_) {
            cue_over_capacity_ = true;
            context_.services().emit({Severity::Error,
                                      "animation.audio_cue",
                                      "Audio Cue component limit (256) exceeded; cues are inactive",
                                      {}});
        }
        return;
    }
    cue_over_capacity_ = false;
    std::set<flecs::entity_t> alive;
    audio_cues_.each([&](flecs::entity entity, const AudioCue& config) {
        const auto reference = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !reference || !conditions.active(entity.id()))
            return;
        alive.insert(entity.id());
        auto& cue = cue_states_[entity.id()];
        const bool same_time =
            cue.config.time_seconds == config.time_seconds ||
            (std::isnan(cue.config.time_seconds) && std::isnan(config.time_seconds));
        if (cue.entity != *reference || cue.config.sequence != config.sequence ||
            cue.config.source != config.source || cue.config.enabled != config.enabled ||
            !same_time) {
            cue.error.clear();
            cue.binding_error = false;
        }
        cue.entity = *reference;
        cue.config = config;
        cue.ready = false;
        cue.sequence = 0;
        if (!config.enabled || !config.sequence.entity) {
            cue.error.clear();
            cue.binding_error = false;
            return;
        }
        try {
            if (!std::isfinite(config.time_seconds) || config.time_seconds < 0 ||
                config.time_seconds > 3600)
                throw std::runtime_error(
                    "Audio Cue time must be finite and between 0 and 3600 seconds");
            const auto sequence = context_.resolve(config.sequence);
            const auto source = context_.resolve(config.source);
            if (sequence.state != WorldContext::ResolveState::Available ||
                source.state != WorldContext::ResolveState::Available)
                throw std::runtime_error(
                    "Audio Cue sequence/source is missing, ambiguous or unloaded");
            const auto controller = context_.world().entity(sequence.entity);
            const auto actor = context_.world().entity(source.entity);
            const auto membership = scene_membership(entity);
            if (!membership || scene_membership(controller) != membership ||
                scene_membership(actor) != membership)
                throw std::runtime_error(
                    "Audio Cue sequence and source must belong to the same loaded Scene");
            if (!controller.has<AnimationSequence>() ||
                (actor.has<AudioSource>() == actor.has<SoundCue>()))
                throw std::runtime_error(
                    "Audio Cue requires an Animation Sequence and an Audio Source or Sound Cue");
            if (!conditions.active(controller.id()) || !conditions.active(actor.id()))
                return;
            if (!sequence_states_.contains(controller.id()) ||
                sequence_errors_.contains(controller.id())) {
                if (cue.binding_error)
                    cue.error.clear();
                cue.binding_error = false;
                return; // Disabled/invalid controllers already own their state/diagnostic.
            }
            if (config.time_seconds > sequence_states_.at(controller.id()).config.duration)
                throw std::runtime_error("Audio Cue time exceeds its sequence duration");
            if (actor.has<AudioSource>() && !actor.get<AudioSource>().clip.id)
                throw std::runtime_error("Audio Cue's Audio Source requires a clip");
            cue.sequence = controller.id();
            cue.ready = true;
            if (cue.binding_error)
                cue.error.clear();
            cue.binding_error = false;
        } catch (const std::exception& error) {
            cue_error(cue, error.what(), true);
        }
    });
    std::erase_if(cue_states_, [&](const auto& entry) { return !alive.contains(entry.first); });
}
void TransformAnimationPlayback::dispatch_audio_cues() {
    if (cue_states_.empty())
        return;
    const auto services = context_.services();
    if (!services.available(Capability::Audio))
        return;
    struct Occurrence {
        Cue* cue{};
        double offset{};
    };
    std::array<Occurrence, 128> events{};
    std::size_t count = 0;
    for (auto& [id, cue] : cue_states_) {
        (void)id;
        if (!cue.ready)
            continue;
        const auto& placement = sequence_states_.at(cue.sequence);
        const auto& sequence = sequence_states_.at(placement.root);
        if (sequence.advance <= 0)
            continue;
        const auto end = sequence.previous + sequence.advance;
        const auto duration = sequence.config.duration * animation_timebase;
        std::optional<double> previous_point;
        for (const auto& span : placement.mapping->spans()) {
            const auto local = cue.config.time_seconds * animation_timebase;
            if (!span.active || local < span.local_begin || local > span.local_end)
                continue;
            const auto point = span.root_point(local);
            // An inherited endpoint and entry can describe the same cue/root point.
            if (previous_point && *previous_point == point)
                continue;
            previous_point = point;
            const bool entry = sequence.entry_pending && sequence.previous == 0 && point == 0;
            double first = 0, occurrences = point > sequence.previous && point <= end ? 1 : 0;
            if (sequence.config.loop)
                std::tie(first, occurrences) =
                    animation_periodic_range(sequence.previous, end, point, duration);
            if (!std::isfinite(occurrences) ||
                occurrences + unsigned(entry) > events.size() - count) {
                cue_error(cue, "Audio Cue batch exceeds 128 occurrences; no cues queued this tick",
                          false);
                return;
            }
            if (entry)
                events[count++] = {&cue, 0};
            for (std::size_t i = 0; i < static_cast<std::size_t>(occurrences); ++i) {
                const auto time = sequence.config.loop ? point + (first + i) * duration : point;
                events[count++] = {
                    &cue, std::clamp((time - sequence.previous) / sequence.advance, 0., 1.)};
            }
        }
    }
    std::sort(events.begin(), events.begin() + count, [](const auto& a, const auto& b) {
        if (a.offset != b.offset)
            return a.offset < b.offset;
        return a.cue->entity < b.cue->entity;
    });
    const auto audio = services.audio();
    for (std::size_t i = 0; i < count; ++i) {
        auto& cue = *events[i].cue;
        try {
            audio->play_one_shot(cue.config.source);
            cue.error.clear();
            cue.binding_error = false;
        } catch (const std::exception& error) {
            cue_error(cue, error.what(), false);
        }
    }
}
bool TransformAnimationPlayback::audio_cues_checkpoint_ready() const {
    return !cue_over_capacity_ &&
           std::all_of(cue_states_.begin(), cue_states_.end(), [](const auto& entry) {
               const auto& cue = entry.second.config;
               return std::isfinite(cue.time_seconds) && cue.time_seconds >= 0 &&
                      cue.time_seconds <= 3600 &&
                      bool(cue.sequence.scene) == bool(cue.sequence.entity) &&
                      bool(cue.source.scene) == bool(cue.source.entity);
           });
}
Json TransformAnimationPlayback::audio_cues_checkpoint() const {
    // Recovery comparison must not mistake an overfull collection or a partial
    // native EntityRef for an empty/null authored configuration.
    if (!audio_cues_checkpoint_ready())
        throw std::runtime_error("Invalid Audio Cue recovery configuration");
    auto result = Json::array();
    for (const auto& [id, cue] : cue_states_) {
        (void)id;
        result.push_back(
            {{"entity", cue.entity},
             {"sequence", cue.config.sequence.entity ? Json(cue.config.sequence) : Json()},
             {"source", cue.config.source.entity ? Json(cue.config.source) : Json()},
             {"time_seconds", cue.config.time_seconds},
             {"enabled", cue.config.enabled}});
    }
    std::sort(result.begin(), result.end(), [](const Json& a, const Json& b) {
        return a.at("entity").dump() < b.at("entity").dump();
    });
    return result;
}
} // namespace forge
