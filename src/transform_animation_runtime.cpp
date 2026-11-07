#include "transform_animation_runtime.hpp"
#include "animation_component_values.hpp"
#include "animation_material_patch.hpp"
#include "animation_material_values.hpp"
#include "animation_property_state.hpp"
#include "animation_sequence_binding.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/application_state_activation.hpp>
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
namespace {
template <class Binding>
std::vector<std::size_t> sequence_binding_order(const std::vector<Binding>& first,
                                                const std::vector<Binding>& next,
                                                const char* kind) {
    const auto fail = [&] {
        throw std::runtime_error(std::string("Sequence crossfade requires matching ") + kind);
    };
    if (first.size() != next.size())
        fail();
    std::vector<std::size_t> order;
    order.reserve(first.size());
    for (const auto& binding : first) {
        const auto at = std::find(next.begin(), next.end(), binding);
        if (at == next.end())
            fail();
        order.push_back(std::size_t(at - next.begin()));
    }
    return order;
}
bool held_crossed(const std::vector<TransformAnimationKey>& keys, double from, double to) {
    auto next = std::upper_bound(keys.begin(), keys.end(), from,
                                 [](double t, const auto& k) { return t < k.time; });
    for (; next != keys.end() && next->time <= to; ++next)
        if (next != keys.begin() && (next - 1)->held)
            return true;
    return false;
}
} // namespace
TransformAnimationPlayback::TransformAnimationPlayback(WorldContext& context,
                                                       std::filesystem::path project)
    : context_(context), project_(std::move(project)), pool_({1, 64, 64, 16 * 1024 * 1024}),
      players_(context.world().query<const TransformAnimator>()),
      sequences_(context.world().query<const AnimationSequence>()),
      camera_shots_(context.world().query<const CameraShot>()),
      audio_cues_(context.world().query<const AudioCue>()) {}
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
         selected.at("output_version") != 3 && selected.at("output_version") != 4 &&
         selected.at("output_version") != 5 && selected.at("output_version") != 6 &&
         selected.at("output_version") != 7 && selected.at("output_version") != 8 &&
         selected.at("output_version") != 9) ||
        !generation.is_number_unsigned() || generation == 0)
        throw std::runtime_error("Unsupported transform animation selection");
    return pool_.request(
        asset, revision, generation.get<std::uint64_t>(),
        [project = project_, asset, selected, metadata, revision](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation load cancelled");
            DerivedDataCache cache(project, {transform_animation_cooked_byte_limit,
                                             transform_animation_cooked_byte_limit + 65536, 1});
            std::optional<TransformAnimationClip> clip;
            auto artifact = cache.load_selected(revision, [&](const auto& cooked) {
                if (cooked.files.size() != 1 || cooked.files.front().name != "animation.clip")
                    throw std::runtime_error("Invalid transform animation bundle");
                const auto document = asset_detail::parse_bounded_json(
                    cooked.files.front().bytes, transform_animation_cooked_byte_limit);
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
    ApplicationStateActivation conditions(context_);
    pool_.pump();
    synchronize_sequences();
    pending_ = false;
    std::set<flecs::entity_t> alive;
    std::set<AssetId> used, previous_assets;
    for (const auto& [id, state] : states_) {
        (void)id;
        for (auto asset : {state.config.clip, state.requested, state.config.sequence_next_clip,
                           state.requested_next})
            if (asset.id)
                previous_assets.insert(asset.id);
        for (const auto& clip : state.config.sequence_extra_clips)
            previous_assets.insert(clip.clip.id);
        for (const auto clip : state.requested_extras)
            previous_assets.insert(clip.id);
    }
    players_.each([&](flecs::entity entity, const TransformAnimator& config) {
        const auto reference = context_.reference(entity.id());
        if (entity.has(flecs::Prefab) || !reference || !config.enabled ||
            (!config.clip.id && !config.sequence_next_clip.id &&
             config.sequence_extra_clips.empty()) ||
            !conditions.active(entity.id()))
            return;
        alive.insert(entity.id());
        if (alive.size() > 256) {
            error(entity.id(), "Transform animation player limit (256) exceeded");
            return;
        }
        if (!config.clip.id) {
            load_errors_.insert(entity.id());
            error(entity.id(), "Sequence next clip requires a primary transform clip");
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
            if (!std::isfinite(config.sequence_start_seconds) ||
                config.sequence_start_seconds < 0 || config.sequence_start_seconds > 3600)
                throw std::runtime_error("Sequence track start must be within 0–3600 seconds");
            if (!std::isfinite(config.sequence_next_start_seconds) ||
                config.sequence_next_start_seconds < 0 || config.sequence_next_start_seconds > 3600)
                throw std::runtime_error("Sequence next clip start must be within 0–3600 seconds");
            flecs::entity_t sequence = 0;
            if (config.sequence.entity) {
                sequence = resolve_sequence(entity, config.sequence);
                if (!sequence) {
                    alive.erase(entity.id());
                    return;
                }
                if (config.sequence_start_seconds > sequence_states_.at(sequence).config.duration)
                    throw std::runtime_error("Track start exceeds the sequence duration");
            }
            if (config.sequence_next_clip.id &&
                (!sequence || config.sequence_next_start_seconds >
                                  sequence_states_.at(sequence).config.duration))
                throw std::runtime_error(
                    "Next clip requires a sequence and a start within its duration");
            animation_detail::validate_sequence_extras(config.sequence_extra_clips, config.clip,
                                                       config.sequence_next_clip, bool(sequence),
                                                       config.sequence_next_start_seconds);
            for (const auto& extra : config.sequence_extra_clips) {
                if (extra.start_seconds > sequence_states_.at(sequence).config.duration)
                    throw std::runtime_error("Extra clip start exceeds the sequence duration");
                used.insert(extra.clip.id);
            }
            if (config.sequence_next_clip.id)
                used.insert(config.sequence_next_clip.id);
            used.insert(config.clip.id);
            auto& state = states_[entity.id()];
            const bool changed =
                state.config.clip != config.clip || state.entity != *reference ||
                state.config.sequence != config.sequence ||
                state.config.sequence_start_seconds != config.sequence_start_seconds ||
                state.config.sequence_next_clip != config.sequence_next_clip ||
                state.config.sequence_extra_clips != config.sequence_extra_clips ||
                (config.sequence_next_clip.id &&
                 state.config.sequence_next_start_seconds != config.sequence_next_start_seconds);
            state.requested = config.clip;
            state.requested_next = config.sequence_next_clip;
            state.requested_extras.clear();
            for (const auto& extra : config.sequence_extra_clips)
                state.requested_extras.push_back(extra.clip);
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
            const auto validate_channels = [&](const TransformAnimationClip& incoming) {
                const auto supported = [](const TransformAnimationClip& value) {
                    return value.channels || value.properties || !value.material_bindings.empty() ||
                           !value.component_bindings.empty();
                };
                if (!supported(clip.get()) || !supported(incoming) ||
                    clip->channels != incoming.channels || clip->properties != incoming.properties)
                    throw std::runtime_error("Sequence crossfade requires matching transform "
                                             "channels and built-in property tracks");
            };
            ResourceTicket next_ticket;
            ResourceLease<TransformAnimationAsset> next_clip;
            std::optional<detail::SequenceClipSpan> span;
            if (config.sequence_next_clip.id) {
                next_ticket = request(config.sequence_next_clip);
                state.next_ticket = next_ticket;
                next_clip = pool_.acquire(next_ticket);
                if (!next_clip) {
                    const auto info = next_ticket.inspect();
                    if (resource_detail::terminal(info.state))
                        throw std::runtime_error(info.diagnostic);
                    pending_ = true;
                    return;
                }
                validate_channels(next_clip.get());
                span = detail::SequenceClipSpan::make(config.sequence_start_seconds,
                                                      config.sequence_next_start_seconds,
                                                      clip->duration, next_clip->duration);
            }
            std::array<ExtraClip, sequence_clip_limit - 2> extra_clips{};
            state.extra_tickets.resize(config.sequence_extra_clips.size());
            // Pending revisions stay distinct from admitted leases, including
            // repairs of a previously rejected Material target.
            for (std::size_t i = 0; i < config.sequence_extra_clips.size(); ++i)
                state.extra_tickets[i] = request(config.sequence_extra_clips[i].clip);
            for (std::size_t i = 0; i < config.sequence_extra_clips.size(); ++i) {
                auto& extra = extra_clips[i];
                const auto& requested = state.extra_tickets[i];
                extra.clip = pool_.acquire(requested);
                if (!extra.clip) {
                    const auto info = requested.inspect();
                    if (resource_detail::terminal(info.state))
                        throw std::runtime_error(info.diagnostic);
                    pending_ = true;
                    return;
                }
                validate_channels(extra.clip.get());
            }
            std::optional<detail::SequenceClipArrangement> arrangement;
            if (!config.sequence_extra_clips.empty()) {
                std::array<detail::SequenceClipArrangement::Clip, sequence_clip_limit> placements{};
                placements[0] = {config.sequence_start_seconds, double(clip->duration)};
                placements[1] = {config.sequence_next_start_seconds, double(next_clip->duration)};
                for (std::size_t i = 0; i < config.sequence_extra_clips.size(); ++i)
                    placements[i + 2] = {config.sequence_extra_clips[i].start_seconds,
                                         double(extra_clips[i].clip->duration)};
                arrangement = detail::SequenceClipArrangement::make(
                    std::span(placements).first(config.sequence_extra_clips.size() + 2));
            }
            bool extra_revision_changed =
                state.extra_clips.size() != config.sequence_extra_clips.size();
            if (!extra_revision_changed)
                for (std::size_t i = 0; i < state.extra_clips.size(); ++i)
                    extra_revision_changed |=
                        state.extra_clips[i].clip.identity() != extra_clips[i].clip.identity();
            const bool next_revision_changed = bool(state.next_clip) != bool(next_clip) ||
                                               (state.next_clip && next_clip &&
                                                state.next_clip.identity() != next_clip.identity());
            const bool clips_changed = changed || !state.clip || next_revision_changed ||
                                       extra_revision_changed ||
                                       state.clip.identity().revision != clip.identity().revision;
            std::vector<std::size_t> material_order, component_order;
            std::vector<ExtraClip> admitted_extras;
            if (clips_changed) {
                if (next_clip) {
                    material_order = sequence_binding_order(
                        clip->material_bindings, next_clip->material_bindings,
                        "Material slots, keys and semantic types");
                    component_order = sequence_binding_order(
                        clip->component_bindings, next_clip->component_bindings,
                        "reflected fields, schemas and value types");
                }
                (void)prepare_animation_components(context_, entity, clip.get(), 0, true);
                if (next_clip)
                    (void)prepare_animation_components(context_, entity, next_clip.get(), 0, true);
                admitted_extras.reserve(config.sequence_extra_clips.size());
                for (std::size_t i = 0; i < config.sequence_extra_clips.size(); ++i) {
                    auto& extra = extra_clips[i];
                    extra.material_order = sequence_binding_order(
                        clip->material_bindings, extra.clip->material_bindings,
                        "Material slots, keys and semantic types");
                    extra.component_order = sequence_binding_order(
                        clip->component_bindings, extra.clip->component_bindings,
                        "reflected fields, schemas and value types");
                    (void)prepare_animation_components(context_, entity, extra.clip.get(), 0, true);
                    admitted_extras.push_back(std::move(extra));
                }
            }
            if (clip->properties & AnimationPropertyState::sprite_mask) {
                const auto values =
                    AnimationPropertyState::from(entity, AnimationPropertyState::sprite_mask);
                const auto frames = detail::sprite_frame_key_limit(*values.sprite);
                if (changed || next_revision_changed || extra_revision_changed || !state.clip ||
                    state.clip.identity().revision != clip.identity().revision ||
                    state.sprite_frames != frames) {
                    values.validate_clip(clip.get());
                    if (next_clip)
                        values.validate_clip(next_clip.get());
                    for (const auto& extra : admitted_extras)
                        values.validate_clip(extra.clip.get());
                    if (!clips_changed)
                        for (const auto& extra : state.extra_clips)
                            values.validate_clip(extra.clip.get());
                }
                state.sprite_frames = frames;
            } else
                state.sprite_frames.reset();
            if (changed || !state.clip) {
                state.time = 0;
                state.playing = config.play_on_start;
            }
            if (clips_changed) {
                state.discontinuous = true;
                state.previous = state.time;
                state.advance = 0;
            }
            state.config = config;
            state.sequence = sequence;
            state.entity = *reference;
            state.ticket = std::move(ticket);
            state.clip = std::move(clip);
            state.next_clip = std::move(next_clip);
            state.next_ticket = std::move(next_ticket);
            state.span = std::move(span);
            state.arrangement = std::move(arrangement);
            if (clips_changed) {
                state.extra_clips = std::move(admitted_extras);
                if (extra_revision_changed)
                    state.material.reset();
                state.material_order = std::move(material_order);
                state.component_order = std::move(component_order);
            }
            state.time = std::min(state.time, clip_span_duration(state));
            if (sequence)
                state.time = sequence_time(state, sequence_states_.at(sequence).time);
            if (load_errors_.erase(entity.id()))
                errors_.erase(entity.id());
        } catch (const std::exception& e) {
            load_errors_.insert(entity.id());
            error(entity.id(), e.what());
        }
    });
    auto retained = used;
    for (const auto& [id, state] : states_)
        if (alive.contains(id)) {
            for (auto asset : {state.config.clip, state.config.sequence_next_clip})
                if (asset.id)
                    retained.insert(asset.id);
            for (const auto& extra : state.config.sequence_extra_clips)
                retained.insert(extra.clip.id);
        }
    // Keep admitted last-good leases while a replacement prepares. Retire old
    // requests through the same bounded pool, including changes on live actors.
    for (auto asset : previous_assets)
        if (!retained.contains(asset))
            pool_.unload({asset});
    std::erase_if(states_, [&](const auto& entry) { return !alive.contains(entry.first); });
    std::erase_if(errors_, [&](const auto& entry) {
        return !alive.contains(entry.first) && !sequence_states_.contains(entry.first) &&
               !sequence_errors_.contains(entry.first) && !shot_errors_.contains(entry.first);
    });
    std::erase_if(load_errors_, [&](auto id) { return !alive.contains(id); });
    synchronize_materials();
    synchronize_camera_shots();
    synchronize_audio_cues();
}
double TransformAnimationPlayback::clip_span_duration(const State& state) const {
    if (state.arrangement)
        return state.arrangement->duration_us;
    return state.span ? state.span->duration_us : double(state.clip->duration);
}
AssetRef<TransformAnimationAsset>
TransformAnimationPlayback::clip_reference(const State& state, std::size_t index) const {
    return index == 0   ? state.config.clip
           : index == 1 ? state.config.sequence_next_clip
                        : state.config.sequence_extra_clips.at(index - 2).clip;
}
double TransformAnimationPlayback::clip_start_seconds(const State& state, std::size_t index) const {
    return index == 0   ? state.config.sequence_start_seconds
           : index == 1 ? state.config.sequence_next_start_seconds
                        : state.config.sequence_extra_clips.at(index - 2).start_seconds;
}
TransformAnimationPlayback::ClipView
TransformAnimationPlayback::clip_view(const State& state, std::size_t index) const {
    if (index == 0)
        return {state.clip.get(), {}, {}};
    if (index == 1)
        return {state.next_clip.get(), state.material_order, state.component_order};
    const auto& clip = state.extra_clips.at(index - 2);
    return {clip.clip.get(), clip.material_order, clip.component_order};
}
detail::SequenceClipArrangement::Sample TransformAnimationPlayback::clip_sample(const State& state,
                                                                                double time) const {
    if (state.arrangement)
        return state.arrangement->sample(time);
    const bool pair = state.span && state.next_clip;
    return {
        0, pair ? 1u : 0u, std::clamp(time, 0., double(state.clip->duration)),
        pair ? std::clamp(time - state.span->next_offset_us, 0., double(state.next_clip->duration))
             : 0.,
        pair ? state.span->next_weight(time) : 0.};
}
LocalTransform TransformAnimationPlayback::sample_pose(const State& state, double time) const {
    const auto sample = clip_sample(state, time);
    const auto first = clip_view(state, sample.primary).clip.sample(sample.primary_us);
    if (sample.weight == 0)
        return first;
    const auto next = clip_view(state, sample.next).clip.sample(sample.next_us);
    return sample.weight == 1 ? next : interpolate_animation_poses(first, next, sample.weight);
}
AnimationPropertyValues TransformAnimationPlayback::sample_properties(const State& state,
                                                                      double time) const {
    const auto sample = clip_sample(state, time);
    auto first = clip_view(state, sample.primary).clip.sample_properties(sample.primary_us);
    if (sample.weight == 0)
        return first;
    const auto next = clip_view(state, sample.next).clip.sample_properties(sample.next_us);
    if (sample.weight == 1)
        return next;
    for (const auto& binding : animation_property_bindings()) {
        if (!(state.clip->properties & animation_property_bit(binding.property)))
            continue;
        const auto index = unsigned(binding.property);
        first[index] = animation_property_discrete(binding.property)
                           ? (sample.weight < .5 ? first[index] : next[index])
                           : std::lerp(first[index], next[index], sample.weight);
    }
    return first;
}
void TransformAnimationPlayback::sample_materials(const State& state, double time,
                                                  std::span<AnimationMaterialValues> output) const {
    const auto sample = clip_sample(state, time);
    const auto first = clip_view(state, sample.primary);
    if (first.material_order.empty())
        first.clip.sample_materials(sample.primary_us, output);
    else {
        std::array<AnimationMaterialValues, material_parameter_override_limit> values;
        first.clip.sample_materials(sample.primary_us, std::span(values).first(output.size()));
        for (std::size_t i = 0; i < output.size(); ++i)
            output[i] = values[first.material_order[i]];
    }
    if (sample.weight == 0)
        return;
    const auto next = clip_view(state, sample.next);
    std::array<AnimationMaterialValues, material_parameter_override_limit> incoming;
    next.clip.sample_materials(sample.next_us, std::span(incoming).first(output.size()));
    for (std::size_t i = 0; i < output.size(); ++i)
        for (unsigned lane = 0;
             lane < material_parameter_width(state.clip->material_bindings[i].type); ++lane) {
            const double value =
                incoming[next.material_order.empty() ? i : next.material_order[i]][lane];
            output[i][lane] =
                sample.weight == 1 ? value : std::lerp(output[i][lane], value, sample.weight);
        }
}
void TransformAnimationPlayback::sample_components(const State& state, double time,
                                                   std::span<Json> output) const {
    const auto sample = clip_sample(state, time);
    const auto first = clip_view(state, sample.primary);
    if (first.component_order.empty())
        first.clip.sample_component_values(sample.primary_us, output);
    else {
        std::array<Json, 64> values;
        first.clip.sample_component_values(sample.primary_us,
                                           std::span(values).first(output.size()));
        for (std::size_t i = 0; i < output.size(); ++i)
            output[i] = values[first.component_order[i]];
    }
    if (sample.weight == 0)
        return;
    const auto next = clip_view(state, sample.next);
    std::array<Json, 64> incoming;
    next.clip.sample_component_values(sample.next_us, std::span(incoming).first(output.size()));
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto& value = incoming[next.component_order.empty() ? i : next.component_order[i]];
        if (sample.weight == 1)
            output[i] = value;
        else if (state.clip->component_bindings[i].value_type ==
                 AnimationComponentValueType::Scalar)
            output[i] = std::lerp(output[i].get<double>(), value.get<double>(), sample.weight);
        else if (sample.weight >= .5)
            output[i] = value;
    }
}
bool TransformAnimationPlayback::has_next_sequence_clips() const {
    return std::any_of(states_.begin(), states_.end(),
                       [](const auto& value) { return bool(value.second.next_clip); });
}
void TransformAnimationPlayback::apply(const std::map<flecs::entity_t, double>& times, bool commit,
                                       const SequenceTimes& seek) {
    if (times.empty() && !skeletal_pose_)
        return;
    const bool transforms = std::any_of(times.begin(), times.end(), [&](const auto& entry) {
        return states_.at(entry.first).clip->channels != 0;
    });
    auto candidate = transforms || skeletal_pose_ ? context_.transform_nodes()
                                                  : decltype(context_.transform_nodes()){};
    std::map<flecs::entity_t, AnimationPropertyState> properties;
    std::map<flecs::entity_t, MeshRenderer> materials;
    std::map<flecs::entity_t, std::vector<AnimationComponentCandidate>> components;
    for (const auto& [id, time] : times) {
        const auto& state = states_.at(id);
        auto entity = context_.world().entity(id);
        if (!state.clip->component_bindings.empty()) {
            std::array<Json, 64> samples;
            auto values = std::span(samples).first(state.clip->component_bindings.size());
            sample_components(state, time, values);
            components.emplace(id, prepare_animation_components(context_, entity, state.clip.get(),
                                                                time, false, values));
        }
        if (!state.clip->material_bindings.empty()) {
            if (state.material_error)
                throw std::runtime_error("Animated material target is invalid");
            if (state.material_pending || !state.material)
                throw AnimationMaterialPending("Waiting for animated material target admission");
            if (!entity.has<MeshRenderer>())
                throw std::runtime_error("Material animation requires a MeshRenderer component");
            auto renderer = entity.get<MeshRenderer>();
            std::array<AnimationMaterialValues, material_parameter_override_limit> samples;
            auto values = std::span(samples).first(state.clip->material_bindings.size());
            sample_materials(state, time, values);
            renderer.material_parameters = project_animation_material_parameters(
                state.clip.get(), values, renderer.material_parameters);
            (void)asset_detail::prepare_model_material_parameters(state.material->draw,
                                                                  renderer.material_parameters);
            materials.emplace(id, std::move(renderer));
        }
        if (state.clip->properties) {
            auto values = AnimationPropertyState::from(entity, state.clip->properties);
            values.sample(state.clip->properties, sample_properties(state, time));
            properties.emplace(id, std::move(values));
        }
        if (!state.clip->channels)
            continue;
        if ((entity.has<PhysicsBody>() && entity.get<PhysicsBody>().enabled &&
             entity.get<PhysicsBody>().motion == 2) ||
            (entity.has<PhysicsBody2D>() && entity.get<PhysicsBody2D>().enabled &&
             entity.get<PhysicsBody2D>().motion == 2) ||
            (entity.has<CharacterController>() && entity.get<CharacterController>().enabled) ||
            (entity.has<CharacterController2D>() && entity.get<CharacterController2D>().enabled) ||
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
        const auto pose = sample_pose(state, time);
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
    const auto publish_skeletons =
        skeletal_pose_ ? skeletal_pose_(candidate, seek) : std::function<void()>{};
    // The skeletal preparation validates the combined pose through the same
    // AnimationRuntime validator. Avoid evaluating that complete candidate twice.
    if (transforms && !skeletal_pose_ && validator_)
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
        if (components.contains(id))
            for (const auto& value : components.at(id))
                value.apply(entity);
        if (properties.contains(id))
            properties.at(id).commit(entity);
        if (materials.contains(id))
            entity.set<MeshRenderer>(materials.at(id));
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
    if (publish_skeletons)
        publish_skeletons();
    for (auto& [id, sequence] : sequence_states_) {
        (void)id;
        sequence.discontinuous = false;
    }
}
bool TransformAnimationPlayback::ready() const { return !pending_ && errors_.empty(); }
bool TransformAnimationPlayback::checkpoint_ready() const {
    return ready() && audio_cues_checkpoint_ready() &&
           std::none_of(states_.begin(), states_.end(),
                        [](const auto& entry) { return entry.second.discontinuous; });
}
bool TransformAnimationPlayback::initial_pose() {
    // AnimationRuntime synchronizes assets before this provider is evaluated.
    if (!errors_.empty())
        throw std::runtime_error("Invalid Transform Animator configuration: " +
                                 errors_.begin()->second);
    if (pending_)
        return false;
    std::map<flecs::entity_t, double> times;
    for (const auto& [id, state] : states_)
        if (state.clip)
            times.emplace(id, state.time);
    apply(times, true);
    return true;
}
void TransformAnimationPlayback::tick(double seconds, bool deliver_events) {
    // Reuse the owner's synchronization; do not query/admit every player twice.
    discontinuities_.clear();
    for (auto& [id, sequence] : sequence_states_) {
        (void)id;
        sequence.previous = sequence.time;
        sequence.advance = 0;
        if (sequence.parent || !sequence.playing)
            continue;
        const auto next = sequence_step(sequence, seconds);
        sequence.time = next.time;
        sequence.advance = next.advance;
        sequence.playing = next.playing;
        sequence.discontinuous = next.discontinuous;
    }
    for (auto& [id, sequence] : sequence_states_) {
        (void)id;
        if (!sequence.parent)
            continue;
        const auto& root = sequence_states_.at(sequence.root);
        sequence.time = controller_time(sequence, root.time);
        sequence.advance = sequence.time - sequence.previous;
        sequence.discontinuous |=
            root.discontinuous ||
            sequence.mapping->reset_crossed(root.previous, root.previous + root.advance);
    }
    std::map<flecs::entity_t, double> times;
    for (auto& [id, state] : states_) {
        if (!state.clip)
            continue;
        state.previous = state.time;
        state.advance = 0;
        if (state.sequence) {
            const auto group = sequence_states_.find(state.sequence);
            if (group == sequence_states_.end())
                continue;
            state.time = sequence_time(state, group->second.time);
            state.advance = state.time - state.previous;
            state.discontinuous |= group->second.discontinuous;
            if (state.arrangement && state.advance > 0) {
                state.discontinuous |= state.arrangement->cut_crossed(state.previous, state.time);
                for (std::size_t i = 0; i < state.arrangement->size(); ++i) {
                    const auto offset = state.arrangement->clip_offset_us(i);
                    const auto duration = state.arrangement->clip_duration_us(i);
                    state.discontinuous |=
                        held_crossed(clip_view(state, i).clip.keys,
                                     std::clamp(state.previous - offset, 0., duration),
                                     std::clamp(state.time - offset, 0., duration));
                }
            } else if (state.span && state.next_clip && state.advance > 0) {
                state.discontinuous |=
                    state.span->cut_crossed(state.previous, state.time) ||
                    held_crossed(state.next_clip->keys,
                                 std::clamp(state.previous - state.span->next_offset_us, 0.,
                                            state.span->next_duration_us),
                                 std::clamp(state.time - state.span->next_offset_us, 0.,
                                            state.span->next_duration_us));
            }
            if (!state.arrangement && state.time >= state.previous &&
                held_crossed(state.clip->keys, state.previous, state.time))
                state.discontinuous = true;
        } else if (state.playing) {
            const double before = state.time;
            state.time += seconds * animation_timebase * state.config.playback_speed;
            state.advance = state.time - before;
            if (held_crossed(state.clip->keys, before,
                             std::min(state.time, double(state.clip->duration))))
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
                    held_crossed(state.clip->keys, 0,
                                 std::fmod(state.time, double(state.clip->duration))))
                    state.discontinuous = true;
                if (state.clip->properties &&
                    state.clip->sample_properties(0) !=
                        state.clip->sample_properties(state.clip->duration))
                    state.discontinuous = true;
                if (!state.clip->material_bindings.empty()) {
                    std::array<AnimationMaterialValues, material_parameter_override_limit> a{}, b{};
                    const auto count = state.clip->material_bindings.size();
                    state.clip->sample_materials(0, std::span(a).first(count));
                    state.clip->sample_materials(state.clip->duration, std::span(b).first(count));
                    if (!std::equal(a.begin(), a.begin() + count, b.begin()))
                        state.discontinuous = true;
                }
                if (!state.clip->component_bindings.empty()) {
                    std::array<Json, 64> a{}, b{};
                    const auto count = state.clip->component_bindings.size();
                    state.clip->sample_component_values(0, std::span(a).first(count));
                    state.clip->sample_component_values(state.clip->duration,
                                                        std::span(b).first(count));
                    if (!std::equal(a.begin(), a.begin() + count, b.begin()))
                        state.discontinuous = true;
                }
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
    // Preparation already owns these diagnostics. Hold the complete candidate
    // while an input is invalid/loading; do not copy one target's error onto
    // healthy players or emit a second generic error every fixed tick.
    if (std::any_of(times.begin(), times.end(), [&](const auto& entry) {
            const auto& state = states_.at(entry.first);
            return state.material_error || state.material_pending;
        }))
        return;
    try {
        apply(times, true);
        if (deliver_events)
            dispatch_audio_cues();
        for (auto& [id, sequence] : sequence_states_)
            if (sequence.advance > 0)
                sequence.entry_pending = false;
    } catch (const AnimationMaterialPending&) {
        pending_ = true;
    } catch (const std::exception& e) {
        for (const auto& [id, time] : times) {
            (void)time;
            error(id, e.what());
        }
    }
}
void TransformAnimationPlayback::reset_presentation() {
    for (auto& [id, sequence] : sequence_states_) {
        (void)id;
        sequence.previous = sequence.time;
        sequence.advance = 0;
    }
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
    if (at == states_.end() || !at->second.clip ||
        (!at->second.clip->properties && at->second.clip->material_bindings.empty() &&
         at->second.clip->component_bindings.empty()) ||
        at->second.material_pending || errors_.contains(id))
        return nullptr;
    const auto& state = at->second;
    auto time = state.previous + state.advance * alpha;
    if (state.sequence) {
        time = state.advance == 0
                   ? state.time
                   : sequence_time(state, controller_presentation_time(state.sequence, alpha));
    } else if (state.config.loop)
        time = std::fmod(time, double(state.clip->duration));
    auto values = AnimationPropertyState::from(context_.world().entity(id), state.clip->properties);
    values.sample(state.clip->properties, sample_properties(state, time));
    auto result = values.patch(state.clip->properties);
    if (!state.clip->component_bindings.empty()) {
        std::array<Json, 64> samples;
        auto sampled = std::span(samples).first(state.clip->component_bindings.size());
        sample_components(state, time, sampled);
        const auto patch = animation_component_patch(prepare_animation_components(
            context_, context_.world().entity(id), state.clip.get(), time, false, sampled));
        for (const auto& [key, value] : patch.items())
            result[key] = value;
    }
    if (!state.clip->material_bindings.empty()) {
        if (!state.material || state.material_error)
            return nullptr;
        const auto entity = context_.world().entity(id);
        if (!entity.has<MeshRenderer>())
            return nullptr;
        std::array<AnimationMaterialValues, material_parameter_override_limit> samples;
        auto values = std::span(samples).first(state.clip->material_bindings.size());
        sample_materials(state, time, values);
        const auto sampled = project_animation_material_parameters(
            state.clip.get(), values, entity.get<MeshRenderer>().material_parameters);
        (void)asset_detail::prepare_model_material_parameters(state.material->draw, sampled);
        result["forge.mesh_renderer"]["material_parameters"] =
            animation_material_parameters_json(sampled);
    }
    return result;
}
std::vector<std::uint64_t> TransformAnimationPlayback::take_discontinuities() {
    std::vector<std::uint64_t> result(discontinuities_.begin(), discontinuities_.end());
    discontinuities_.clear();
    return result;
}
Json TransformAnimationPlayback::extra_clips_checkpoint(const State& state) const {
    auto result = Json::array();
    for (std::size_t i = 0; i < state.extra_clips.size(); ++i)
        result.push_back({{"clip", state.config.sequence_extra_clips[i].clip.id},
                          {"start_seconds", state.config.sequence_extra_clips[i].start_seconds},
                          {"revision", state.extra_clips[i].clip.identity().revision}});
    return result;
}
Json TransformAnimationPlayback::checkpoint() const {
    if (!checkpoint_ready())
        throw std::runtime_error("Transform animation is not ready for recovery");
    auto result = Json::array();
    for (const auto& [id, state] : states_) {
        (void)id;
        if (!state.clip)
            continue;
        Json value = {
            {"entity", state.entity},
            {"clip", state.config.clip.id},
            {"revision", state.clip.identity().revision},
            {"time_us", state.time},
            {"playing", state.playing},
            {"sequence", state.config.sequence.entity ? Json(state.config.sequence) : Json()},
            {"sequence_start_seconds", state.config.sequence_start_seconds}};
        if (state.next_clip) {
            value["sequence_next_clip"] = state.config.sequence_next_clip.id;
            value["sequence_next_revision"] = state.next_clip.identity().revision;
            value["sequence_next_start_seconds"] = state.config.sequence_next_start_seconds;
        }
        if (state.arrangement)
            value["sequence_extra_clips"] = extra_clips_checkpoint(state);
        result.push_back(std::move(value));
    }
    return result;
}
void TransformAnimationPlayback::restore(const Json& values) { restore(values, Json::array()); }
void TransformAnimationPlayback::restore(const Json& values, const Json& sequences,
                                         unsigned profile, bool rates, bool arrangements,
                                         bool loops, bool placements) {
    const bool audio_entries = profile >= 5;
    const bool nested_entries = profile >= 7;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do {
        if (context_.services().available(Capability::Resources))
            context_.services().resources()->synchronize();
        synchronize();
        if (ready())
            break;
        if (!errors_.empty())
            throw std::runtime_error("Invalid transform animation recovery assets");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    if (!ready() || !values.is_array() || values.size() != states_.size())
        throw std::runtime_error("Transform animation recovery configuration differs");
    // A fresh worker may not have admitted either clip when restore begins.
    // Compare the recovery profile only after admission, before adopting state.
    if ((profile == 6 && !has_next_sequence_clips()) ||
        (profile < 6 && has_next_sequence_clips()) || nested_entries != has_nested_sequences() ||
        rates != has_sequence_rates() || loops != has_sequence_loops() ||
        placements != has_sequence_placements())
        throw std::runtime_error("Sequence pair/nested recovery profile differs");
    if (!sequences.is_array() || sequences.size() != sequence_states_.size())
        throw std::runtime_error("Animation sequence recovery configuration differs");
    auto groups = sequence_states_;
    std::set<flecs::entity_t> group_seen;
    for (const auto& value : sequences) {
        const auto resolved = context_.resolve(value.at("entity").get<EntityRef>());
        if (resolved.state != WorldContext::ResolveState::Available ||
            !groups.contains(resolved.entity) || !group_seen.insert(resolved.entity).second)
            throw std::runtime_error("Unresolved or duplicate sequence recovery controller");
        auto& state = groups.at(resolved.entity);
        const double time = value.at("time_us");
        if (value.at("duration") != state.config.duration ||
            value.at("loop") != state.config.loop ||
            value.at("speed") != state.config.playback_speed ||
            value.at("play_on_start") != state.config.play_on_start || !std::isfinite(time) ||
            time < 0 || time > state.config.duration * animation_timebase ||
            !value.at("playing").is_boolean())
            throw std::runtime_error("Sequence recovery configuration/time mismatch");
        state.time = state.previous = time;
        state.advance = 0;
        state.playing = value.at("playing");
        if (audio_entries
                ? (!value.contains("entry_pending") || !value.at("entry_pending").is_boolean())
                : value.contains("entry_pending"))
            throw std::runtime_error("Sequence recovery cue-entry state/version mismatch");
        state.entry_pending = audio_entries ? value.at("entry_pending").get<bool>() : false;
        if (nested_entries) {
            const auto parent = state.config.sequence.entity ? Json(state.config.sequence) : Json();
            if (value.at("sequence") != parent ||
                value.at("sequence_start_seconds") != state.config.sequence_start_seconds ||
                (state.parent && (state.playing || state.entry_pending)))
                throw std::runtime_error("Nested sequence recovery binding/intent differs");
        } else if (value.contains("sequence") || value.contains("sequence_start_seconds"))
            throw std::runtime_error("Unexpected nested sequence recovery fields");
        if ((rates || placements) ? (!value.contains("sequence_rate") ||
                                     value.at("sequence_rate") != state.config.sequence_rate)
                                  : value.contains("sequence_rate"))
            throw std::runtime_error("Sequence recovery placement rate differs");
        if ((loops || placements) ? (!value.contains("sequence_loop") ||
                                     value.at("sequence_loop") != state.config.sequence_loop)
                                  : value.contains("sequence_loop"))
            throw std::runtime_error("Sequence recovery placement loop differs");
        if (placements
                ? (!value.contains("sequence_extra_placements") ||
                   value.at("sequence_extra_placements") !=
                       detail::encode_builtin_value(state.config).at("sequence_extra_placements"))
                : value.contains("sequence_extra_placements"))
            throw std::runtime_error("Sequence recovery repeated placements differ");
        state.discontinuous = true;
    }
    for (const auto& [id, state] : groups) {
        (void)id;
        if (state.parent && state.time != controller_time(state, groups.at(state.root).time))
            throw std::runtime_error("Nested sequence recovery clock differs from its root");
    }
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
            time < 0 || time > clip_span_duration(state) || !value.at("playing").is_boolean() ||
            !times.emplace(resolved.entity, time).second)
            throw std::runtime_error("Transform animation recovery revision/time mismatch");
        if (state.next_clip) {
            if (value.at("sequence_next_clip") != Json(state.config.sequence_next_clip.id) ||
                value.at("sequence_next_revision") != state.next_clip.identity().revision ||
                value.at("sequence_next_start_seconds") != state.config.sequence_next_start_seconds)
                throw std::runtime_error(
                    "Sequence next clip recovery revision/configuration mismatch");
        } else if (value.contains("sequence_next_clip") ||
                   value.contains("sequence_next_revision") ||
                   value.contains("sequence_next_start_seconds"))
            throw std::runtime_error("Unexpected sequence next clip recovery metadata");
        if (state.arrangement) {
            if (!arrangements || !value.contains("sequence_extra_clips") ||
                value.at("sequence_extra_clips") != extra_clips_checkpoint(state))
                throw std::runtime_error(
                    "Sequence collection recovery resource/configuration differs");
        } else if (value.contains("sequence_extra_clips"))
            throw std::runtime_error("Unexpected sequence collection recovery metadata");
        const auto expected_sequence =
            state.config.sequence.entity ? Json(state.config.sequence) : Json();
        if (value.value("sequence", Json()) != expected_sequence ||
            value.value("sequence_start_seconds", 0.) != state.config.sequence_start_seconds ||
            (state.sequence && (time != sequence_time(state, groups.at(state.sequence).time))))
            throw std::runtime_error("Sequence recovery track binding/time mismatch");
        playing.emplace(resolved.entity, value.at("playing").get<bool>());
    }
    SequenceTimes roots;
    for (const auto& [id, group] : groups)
        if (!group.parent)
            roots.emplace(id, group.time);
    apply(times, false, roots);
    sequence_states_.swap(groups);
    apply(times, true);
    for (auto& [id, state] : states_) {
        state.time = times.at(id);
        state.playing = playing.at(id);
        state.previous = state.time;
        state.advance = 0;
    }
}
} // namespace forge
