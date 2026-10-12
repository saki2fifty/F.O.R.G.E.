#include "audio_preview_access.hpp"
#include "audio_selection.hpp"
#include "audio_source_access.hpp"
#include "builtins.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <miniaudio.h>
#include <set>
#include <thread>
namespace forge {
namespace {
constexpr std::size_t max_voices = 256, max_commands = 1024;
constexpr std::size_t max_cache = 256 * 1024 * 1024;
void checked(ma_result r, const char* action) {
    if (r != MA_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + ma_result_description(r));
}
Double3 unit(Double3 v, Double3 fallback) {
    const double length = std::hypot(v[0], v[1], v[2]);
    if (!std::isfinite(length) || length < 1e-12)
        return fallback;
    for (auto& x : v)
        x /= length;
    return v;
}
bool usable_pose(flecs::entity entity) {
    if (!entity.has<WorldTransform>())
        return false;
    const auto& world = entity.get<WorldTransform>();
    if (!world.resolved)
        return false;
    for (double coordinate : world.affine.point({0, 0, 0}))
        if (!std::isfinite(coordinate) || std::abs(coordinate) > 1e9)
            return false;
    return true;
}
void validate(flecs::entity entity, const AudioSource& s) {
    detail::validate_reflected_value(entity, s);
    if (s.maximum_distance < s.minimum_distance)
        throw std::runtime_error("Audio maximum distance must be at least minimum distance");
    if (s.outer_angle < s.inner_angle)
        throw std::runtime_error("Audio outer opening must be at least inner opening");
}
bool same_failed_config(const AudioSource& a, const AudioSource& b) {
    if (a == b)
        return true;
    if (a.clip != b.clip || a.play_on_start != b.play_on_start || a.loop != b.loop ||
        a.spatialized != b.spatialized || a.attenuation != b.attenuation ||
        a.cone_enabled != b.cone_enabled || a.bus != b.bus)
        return false;
    const std::array left{a.gain,    a.pitch,       a.minimum_distance, a.maximum_distance,
                          a.rolloff, a.inner_angle, a.outer_angle,      a.outer_gain,
                          a.doppler};
    const std::array right{b.gain,    b.pitch,       b.minimum_distance, b.maximum_distance,
                           b.rolloff, b.inner_angle, b.outer_angle,      b.outer_gain,
                           b.doppler};
    for (unsigned i = 0; i < left.size(); ++i)
        if (!(left[i] == right[i] || (std::isnan(left[i]) && std::isnan(right[i]))))
            return false;
    return true;
}
struct AudioMotion {
    Double3 position{};
    std::uint64_t tick = 0;
    bool valid = false;
    Double3 observe(Double3 next, std::uint64_t next_tick, float fixed_delta) {
        Double3 velocity{};
        if (valid && next_tick > tick && next_tick - tick == 1 && fixed_delta > 0) {
            for (unsigned i = 0; i < 3; ++i)
                velocity[i] = (next[i] - position[i]) / double(fixed_delta);
            // Keep native vector arithmetic finite even for tiny deltas or teleports.
            const double speed = std::hypot(velocity[0], velocity[1], velocity[2]);
            if (speed > 100000)
                for (auto& value : velocity)
                    value *= 100000 / speed;
        }
        position = next;
        tick = next_tick;
        valid = true;
        return velocity;
    }
};
} // namespace
struct AudioRuntime::Impl {
    WorldContext& world;
    AudioConfig config;
    AssetCatalog catalog;
    ma_engine engine{};
    ma_sound_group group{};
    bool engine_ready = false, group_ready = false, stopped = false, is_paused = true;
    std::uint64_t tick = 0;
    std::atomic<unsigned> device_events{0};
    std::atomic<std::uint64_t> mixed_frames{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::thread::id owner = std::this_thread::get_id();
    using Clip = asset_detail::AudioClipData;
    struct Voice {
        AudioSource authored;
        EntityRef ref;
        std::shared_ptr<Clip> clip;
        ma_audio_buffer_ref buffer{};
        ma_sound sound{};
        bool ready = false, buffer_ready = false;
        bool source_paused = false;
        float source_volume = 1;
        AudioMotion motion;
        ~Voice() {
            if (ready)
                ma_sound_uninit(&sound);
            if (buffer_ready)
                ma_audio_buffer_ref_uninit(&buffer);
        }
    };
    std::map<AssetId, std::shared_ptr<Clip>> clips;
    std::map<std::uint64_t, std::unique_ptr<Voice>> voices;
    std::map<std::uint64_t, std::string> failures;
    std::map<std::uint64_t, AudioSource> failed_config;
    struct Command {
        EntityRef ref;
        std::uint64_t generation;
        AudioSourceOperation operation;
    };
    std::vector<Command> commands;
    std::size_t cache_bytes = 0;
    std::uint64_t configuration_checks = 0;
    std::string listener_error;
    AudioMotion listener_motion;
    std::uint64_t listener_entity = 0;
    std::set<std::uint64_t> spatial_errors;
    Impl(WorldContext& w, AudioConfig c)
        : world(w), config(std::move(c)), catalog(AssetCatalog::open_project(config.project)) {
        if (!std::isfinite(config.master_volume) || config.master_volume < 0 ||
            config.master_volume > 1)
            throw std::runtime_error("Gameplay master volume must be between zero and one");
        for (float volume : config.channel_volumes)
            if (!std::isfinite(volume) || volume < 0 || volume > 1)
                throw std::runtime_error(
                    "Audio channel volume must be finite and between zero and one");
        auto ec = ma_engine_config_init();
        ec.noAutoStart = MA_TRUE;
        ec.noDevice = config.output == AudioOutput::Offline;
        ec.channels = 2;
        ec.sampleRate = 48000;
        ec.listenerCount = 1;
        ec.pProcessUserData = this;
        ec.onProcess = [](void* p, float*, ma_uint64 frames) {
            static_cast<Impl*>(p)->mixed_frames.fetch_add(frames, std::memory_order_relaxed);
        };
        ec.notificationCallback = [](const ma_device_notification* n) {
            auto* e = static_cast<ma_engine*>(n->pDevice->pUserData);
            if (e && e->pProcessUserData)
                static_cast<Impl*>(e->pProcessUserData)
                    ->device_events.fetch_or(1u << unsigned(n->type), std::memory_order_relaxed);
        };
        // Do not let miniaudio's null backend impersonate a working physical output.
        if (!ec.noDevice) {
#ifdef _WIN32
            const std::vector<ma_backend> backends =
                config.output == AudioOutput::NullDeviceTest
                    ? std::vector<ma_backend>{ma_backend_null}
                    : std::vector<ma_backend>{ma_backend_wasapi};
#else
            const std::vector<ma_backend> backends =
                config.output == AudioOutput::NullDeviceTest
                    ? std::vector<ma_backend>{ma_backend_null}
                    : std::vector<ma_backend>{ma_backend_pulseaudio, ma_backend_alsa};
#endif
            context = std::make_unique<ma_context>();
            const auto result = ma_context_init(
                backends.data(), static_cast<ma_uint32>(backends.size()), nullptr, context.get());
            if (result != MA_SUCCESS) {
                context.reset();
                checked(result, "Audio context initialization");
            }
            ec.pContext = context.get();
        }
        try {
            checked(ma_engine_init(&ec, &engine), "Audio engine initialization");
            engine_ready = true;
            checked(ma_sound_group_init(&engine, 0, nullptr, &group), "Gameplay audio group");
            group_ready = true;
            ma_sound_group_set_volume(&group, config.master_volume);
            checked(ma_sound_group_stop(&group), "Pause initial audio group");
            if (!ec.noDevice)
                checked(ma_engine_start(&engine), "Audio device start");
        } catch (...) {
            shutdown();
            throw;
        }
    }
    std::unique_ptr<ma_context> context;
    ~Impl() { shutdown(); }
    void check() const {
        if (stopped || owner != std::this_thread::get_id())
            throw std::runtime_error("Audio requires live owner thread");
    }
    void shutdown() noexcept {
        if (stopped)
            return;
        stopped = true;
        if (engine_ready && config.output != AudioOutput::Offline)
            (void)ma_engine_stop(&engine);
        voices.clear();
        commands.clear();
        clips.clear();
        if (group_ready) {
            ma_sound_group_uninit(&group);
            group_ready = false;
        }
        if (engine_ready) {
            ma_engine_uninit(&engine);
            engine_ready = false;
        }
        if (context) {
            ma_context_uninit(context.get());
            context.reset();
        }
    }
    void diagnostic(std::string category, std::string message, std::optional<EntityRef> ref = {},
                    AssetId clip = {}) {
        Diagnostic d{Severity::Warning, std::move(category), std::move(message), {}};
        d.context.module = "forge.audio";
        d.context.tick = tick;
        d.context.world_role = world_role_name(world.role());
        if (ref) {
            d.context.entity = ref->entity;
            d.context.asset = ref->scene;
        }
        if (clip) {
            d.context.asset = clip;
            const auto r = catalog.resolve(clip, AudioClipAsset::type);
            if (r.record)
                d.context.source = path_utf8(r.record->source);
        }
        if (world.services().available(Capability::Diagnostics))
            world.services().emit(std::move(d));
    }
    std::shared_ptr<Clip> load(AssetRef<AudioClipAsset> ref) {
        if (!ref.id)
            throw std::runtime_error("AudioSource has no AudioClip assigned");
        if (clips.contains(ref.id))
            return clips.at(ref.id);
        auto scope = world.services().profile("audio", "AudioAssetResolve", tick);
        const auto record = catalog.records().find(ref.id);
        if (record == catalog.records().end())
            throw std::runtime_error("AudioClip is not registered");
        auto clip =
            std::make_shared<Clip>(asset_detail::load_audio_clip(config.project, record->second));
        const auto n = clip->pcm.size();
        if (n * sizeof(float) > max_cache - cache_bytes)
            throw std::runtime_error(
                "Audio decoded cache exceeds 256 MiB; restart Play with fewer clips");
        cache_bytes += n * sizeof(float);
        clips.emplace(ref.id, clip);
        return clip;
    }
    void queue(EntityRef ref, AudioSourceOperation operation) {
        check();
        if (std::uint32_t(operation) > std::uint32_t(AudioSourceOperation::Resume))
            throw std::runtime_error("Unknown audio source operation");
        const auto r = world.resolve(ref);
        if (r.state != WorldContext::ResolveState::Available ||
            !world.world().entity(r.entity).has<AudioSource>())
            throw std::runtime_error("Audio command references missing, ambiguous or stale source");
        if (commands.size() >= max_commands)
            throw std::runtime_error("Audio command limit reached");
        commands.push_back({ref, r.entity, operation});
    }
    void sync(std::uint64_t next_tick, float fixed_delta,
              std::span<const std::uint64_t> discontinuities) {
        check();
        if (!std::isfinite(fixed_delta) || fixed_delta < 0)
            throw std::runtime_error("Audio motion requires a finite nonnegative fixed delta");
        if (!discontinuities.empty()) {
            // Read the existing effective spatial graph only at teleport/seek boundaries.
            // A child emitter follows its spatial parent, including Explicit/World modes.
            const auto nodes = world.transform_nodes();
            const std::set<std::uint64_t> changed(discontinuities.begin(), discontinuities.end());
            auto affected = [&](std::uint64_t id) {
                for (std::size_t depth = 0; id && depth <= nodes.size(); ++depth) {
                    if (changed.contains(id))
                        return true;
                    const auto node = nodes.find(id);
                    if (node == nodes.end())
                        break;
                    id = node->second.parent;
                }
                return false;
            };
            if (affected(listener_entity))
                listener_motion.valid = false;
            for (auto& [id, voice] : voices)
                if (affected(id))
                    voice->motion.valid = false;
        }
        tick = next_tick;
        const auto notifications = device_events.exchange(0, std::memory_order_relaxed);
        if (notifications & ((1u << unsigned(ma_device_notification_type_stopped)) |
                             (1u << unsigned(ma_device_notification_type_rerouted))))
            diagnostic("audio.device_changed", "Audio output stopped or changed. Backend may "
                                               "recover; restart Play if sound does not return.");
        auto scope = world.services().profile("audio", "AudioSync", tick);
        // Read on owner thread only; audio callback sees exclusively miniaudio-owned node state.
        world.evaluate_world_transforms();
        std::vector<flecs::entity> sources, listeners;
        world.world().each<AudioSource>([&](flecs::entity e, AudioSource&) {
            if (world.reference(e.id()))
                sources.push_back(e);
        });
        world.world().each<AudioListener>([&](flecs::entity e, AudioListener& l) {
            if (l.enabled && world.reference(e.id()))
                listeners.push_back(e);
        });
        std::sort(sources.begin(), sources.end(), [](auto a, auto b) { return a.id() < b.id(); });
        std::sort(listeners.begin(), listeners.end(),
                  [](auto a, auto b) { return a.id() < b.id(); });
        const bool listener_valid = listeners.size() == 1 && usable_pose(listeners.front());
        std::string error =
            listeners.size() > 1
                ? "Multiple enabled AudioListeners; spatial voices muted until exactly one remains"
                : "";
        if (listeners.size() == 1 && !listener_valid)
            error = "AudioListener needs a resolved world transform within +/-1e9 meters";
        if (error != listener_error) {
            listener_error = error;
            if (!error.empty())
                diagnostic(listeners.size() > 1 ? "audio.multiple_listeners"
                                                : "audio.listener_transform",
                           error, world.reference(listeners.front().id()));
        }
        Double3 listener_velocity{};
        if (listener_valid) {
            if (listener_entity != listeners.front().id())
                listener_motion.valid = false;
            listener_entity = listeners.front().id();
            const auto& t = listeners.front().get<WorldTransform>();
            const auto p = t.affine.point({0, 0, 0});
            listener_velocity = listener_motion.observe(p, next_tick, fixed_delta);
            const auto forward = unit(t.affine.vector({0, 0, -1}), {0, 0, -1});
            auto up = t.affine.vector({0, 1, 0});
            const double dot = up[0] * forward[0] + up[1] * forward[1] + up[2] * forward[2];
            for (unsigned i = 0; i < 3; i++)
                up[i] -= dot * forward[i];
            up = unit(up, {0, 1, 0});
            ma_engine_listener_set_position(&engine, 0, float(p[0]), float(p[1]), float(p[2]));
            ma_engine_listener_set_direction(&engine, 0, float(forward[0]), float(forward[1]),
                                             float(forward[2]));
            ma_engine_listener_set_world_up(&engine, 0, float(up[0]), float(up[1]), float(up[2]));
        } else {
            listener_motion.valid = false;
            listener_entity = 0;
        }
        ma_engine_listener_set_velocity(&engine, 0, float(listener_velocity[0]),
                                        float(listener_velocity[1]), float(listener_velocity[2]));
        std::set<std::uint64_t> present;
        for (auto e : sources) {
            present.insert(e.id());
            const auto config_value = e.get<AudioSource>();
            const auto ref = world.reference(e.id()).value();
            if (failed_config.contains(e.id()) &&
                same_failed_config(failed_config.at(e.id()), config_value))
                continue;
            try {
                ++configuration_checks;
                validate(e, config_value);
                auto found = voices.find(e.id());
                if (found != voices.end() && found->second->authored.clip != config_value.clip) {
                    voices.erase(found);
                    found = voices.end();
                }
                const bool created = found == voices.end();
                if (created) {
                    if (voices.size() >= max_voices)
                        throw std::runtime_error("Audio voice limit is 256");
                    auto voice = std::make_unique<Voice>();
                    voice->ref = ref;
                    voice->authored = config_value;
                    voice->clip = load(config_value.clip);
                    checked(ma_audio_buffer_ref_init(ma_format_f32, voice->clip->channels,
                                                     voice->clip->pcm.data(), voice->clip->frames,
                                                     &voice->buffer),
                            "Audio buffer");
                    voice->buffer_ready = true;
                    voice->buffer.sampleRate = voice->clip->rate;
                    checked(ma_sound_init_from_data_source(&engine, &voice->buffer, 0, &group,
                                                           &voice->sound),
                            "Audio voice");
                    voice->ready = true;
                    // Source sample rate is supplied before attaching the buffer to the engine.
                    ma_sound_set_pitch(&voice->sound, config_value.pitch);
                    found = voices.emplace(e.id(), std::move(voice)).first;
                }
                auto& v = *found->second;
                v.authored = config_value;
                ma_sound_set_looping(&v.sound, config_value.loop);
                ma_sound_set_pitch(&v.sound, config_value.pitch);
                ma_sound_set_spatialization_enabled(&v.sound, config_value.spatialized);
                Double3 direction{0, 0, -1};
                bool direction_valid = true;
                const bool source_pose_valid = usable_pose(e);
                if (config_value.spatialized && config_value.cone_enabled && source_pose_valid) {
                    direction = e.get<WorldTransform>().affine.vector({0, 0, -1});
                    const auto length = std::hypot(direction[0], direction[1], direction[2]);
                    direction_valid = std::isfinite(length) && length >= 1e-12;
                    direction = unit(direction, {0, 0, -1});
                }
                const bool spatial_valid = listener_valid && source_pose_valid && direction_valid;
                v.source_volume =
                    (!config_value.spatialized || spatial_valid) ? config_value.gain : 0;
                const float channel_volume =
                    config_value.bus == 0 ? 1 : config.channel_volumes.at(config_value.bus - 1);
                ma_sound_set_volume(&v.sound, v.source_volume * channel_volume);
                if (config_value.spatialized && !spatial_valid) {
                    if (spatial_errors.insert(e.id()).second)
                        diagnostic("audio.spatial_unavailable",
                                   "Spatial sound requires resolved source and listener transforms "
                                   "within +/-1e9 meters, exactly one enabled listener, and a "
                                   "nonzero finite emitter forward for directional sound",
                                   ref, config_value.clip.id);
                } else
                    spatial_errors.erase(e.id());
                static constexpr ma_attenuation_model models[] = {ma_attenuation_model_inverse,
                                                                  ma_attenuation_model_linear,
                                                                  ma_attenuation_model_exponential};
                ma_sound_set_attenuation_model(&v.sound, models[config_value.attenuation]);
                ma_sound_set_rolloff(&v.sound, config_value.rolloff);
                constexpr float radians = 0.017453292519943295f;
                constexpr float full_opening = 6.283185307179586f;
                ma_sound_set_direction(&v.sound, float(direction[0]), float(direction[1]),
                                       float(direction[2]));
                ma_sound_set_cone(
                    &v.sound,
                    config_value.cone_enabled ? config_value.inner_angle * radians : full_opening,
                    config_value.cone_enabled ? config_value.outer_angle * radians : full_opening,
                    config_value.outer_gain);
                ma_sound_set_min_distance(&v.sound, config_value.minimum_distance);
                ma_sound_set_max_distance(&v.sound, config_value.maximum_distance);
                Double3 source_velocity{};
                float doppler = 0;
                if (source_pose_valid) {
                    const auto p = e.get<WorldTransform>().affine.point({0, 0, 0});
                    ma_sound_set_position(&v.sound, float(p[0]), float(p[1]), float(p[2]));
                    if (spatial_valid && config_value.spatialized && config_value.doppler > 0) {
                        source_velocity = v.motion.observe(p, next_tick, fixed_delta);
                        const double speed = std::max(
                            std::hypot(source_velocity[0], source_velocity[1], source_velocity[2]),
                            std::hypot(listener_velocity[0], listener_velocity[1],
                                       listener_velocity[2]));
                        // Selected native Doppler divides by c - factor*radial_velocity.
                        // Bound both scaled speeds to c/2: finite ratio in [1/3, 3].
                        constexpr double half_speed_of_sound = 343.3 / 2;
                        doppler = float(speed > 0 ? std::min(double(config_value.doppler),
                                                             half_speed_of_sound / speed)
                                                  : config_value.doppler);
                    } else
                        v.motion.valid = false;
                } else
                    v.motion.valid = false;
                ma_sound_set_velocity(&v.sound, float(source_velocity[0]),
                                      float(source_velocity[1]), float(source_velocity[2]));
                ma_sound_set_doppler_factor(&v.sound, doppler);
                if (created && config_value.play_on_start)
                    checked(ma_sound_start(&v.sound), "Audio autoplay");
                failures.erase(e.id());
                failed_config.erase(e.id());
            } catch (const std::exception& error_value) {
                voices.erase(e.id());
                failed_config[e.id()] = config_value;
                auto& old = failures[e.id()];
                if (old != error_value.what()) {
                    old = error_value.what();
                    diagnostic("audio.source", old, ref, config_value.clip.id);
                }
            }
        }
        std::erase_if(voices, [&](const auto& p) { return !present.contains(p.first); });
        std::erase_if(spatial_errors, [&](auto id) { return !present.contains(id); });
        std::erase_if(failed_config, [&](const auto& p) { return !present.contains(p.first); });
        std::erase_if(failures, [&](const auto& p) { return !present.contains(p.first); });
        flush_commands();
    }
    void flush_commands() {
        auto pending = std::move(commands);
        commands.clear();
        for (const auto& c : pending) {
            const auto r = world.resolve(c.ref);
            if (r.state != WorldContext::ResolveState::Available || r.entity != c.generation ||
                !voices.contains(r.entity)) {
                diagnostic("audio.stale_source", "Audio command source is stale or unavailable",
                           c.ref);
                continue;
            }
            auto& voice = *voices.at(r.entity);
            auto& sound = voice.sound;
            switch (c.operation) {
            case AudioSourceOperation::Restart:
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Restart source");
                checked(ma_sound_start(&sound), "Play source");
                voice.source_paused = false;
                break;
            case AudioSourceOperation::Stop:
                checked(ma_sound_stop(&sound), "Stop source");
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Reset source");
                voice.source_paused = false;
                break;
            case AudioSourceOperation::Pause:
                if (ma_sound_is_playing(&sound) && !ma_sound_at_end(&sound)) {
                    checked(ma_sound_stop(&sound), "Pause source");
                    voice.source_paused = true;
                }
                break;
            case AudioSourceOperation::Resume:
                if (voice.source_paused) {
                    if (!ma_sound_at_end(&sound))
                        checked(ma_sound_start(&sound), "Resume source");
                    voice.source_paused = false;
                }
                break;
            }
        }
    }
};
AudioRuntime::AudioRuntime(WorldContext& w, AudioConfig c)
    : impl_(std::make_unique<Impl>(w, std::move(c))) {}
AudioRuntime::~AudioRuntime() = default;
void AudioRuntime::shutdown() noexcept { impl_->shutdown(); }
void AudioRuntime::synchronize(std::uint64_t tick) { impl_->sync(tick, 0, {}); }
void AudioRuntime::synchronize(std::uint64_t tick, float fixed_delta,
                               std::span<const std::uint64_t> discontinuities) {
    impl_->sync(tick, fixed_delta, discontinuities);
}
void AudioRuntime::paused(bool value) {
    impl_->check();
    if (value == impl_->is_paused)
        return;
    checked(value ? ma_sound_group_stop(&impl_->group) : ma_sound_group_start(&impl_->group),
            "Audio pause/resume");
    impl_->is_paused = value;
}
void AudioRuntime::play(EntityRef ref) { impl_->queue(ref, AudioSourceOperation::Restart); }
void AudioRuntime::master_volume(float value) {
    impl_->check();
    if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::runtime_error("Master volume must be finite and between zero and one");
    ma_sound_group_set_volume(&impl_->group, value);
    impl_->config.master_volume = value;
}
void AudioRuntime::stop_source(EntityRef ref) { impl_->queue(ref, AudioSourceOperation::Stop); }
void AudioRuntime::pause_source(EntityRef ref) { impl_->queue(ref, AudioSourceOperation::Pause); }
void AudioRuntime::resume_source(EntityRef ref) { impl_->queue(ref, AudioSourceOperation::Resume); }
Json AudioSourceAccess::status(AudioRuntime& runtime, EntityRef ref) {
    runtime.impl_->check();
    const auto resolved = runtime.impl_->world.resolve(ref);
    const auto found = runtime.impl_->voices.find(resolved.entity);
    if (resolved.state != WorldContext::ResolveState::Available ||
        found == runtime.impl_->voices.end() || !found->second->ready || found->second->ref != ref)
        throw std::runtime_error("AudioSource has no admitted voice in the current world");
    return {{"source", ref},
            {"available", true},
            {"playing", bool(ma_sound_is_playing(&found->second->sound))},
            {"source_paused", found->second->source_paused},
            {"paused", runtime.impl_->is_paused},
            {"loop", found->second->authored.loop},
            {"duration", found->second->clip->duration()}};
}
void AudioSourceAccess::control(AudioRuntime& runtime, EntityRef ref,
                                AudioSourceOperation operation) {
    (void)status(runtime, ref); // Admit identity/voice before queuing any mutation.
    runtime.impl_->queue(ref, operation);
    runtime.impl_->flush_commands(); // Owner boundary; no simulation tick or pose resampling.
}
void AudioRuntime::mix_volumes(float master, const std::array<float, 3>& channels) {
    impl_->check();
    for (float value : {master, channels[0], channels[1], channels[2]})
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Audio mix volumes must be finite and between zero and one");
    ma_sound_group_set_volume(&impl_->group, master);
    for (const auto& [id, voice] : impl_->voices) {
        (void)id;
        const float channel_volume =
            voice->authored.bus == 0 ? 1 : channels.at(voice->authored.bus - 1);
        ma_sound_set_volume(&voice->sound, voice->source_volume * channel_volume);
    }
    impl_->config.master_volume = master;
    impl_->config.channel_volumes = channels;
}
Json AudioRuntime::status() const {
    impl_->check();
    unsigned playing = 0;
    for (const auto& [id, voice] : impl_->voices) {
        (void)id;
        playing += ma_sound_is_playing(&voice->sound);
    }
    return {{"mixed_frames", impl_->mixed_frames.load(std::memory_order_relaxed)},
            {"playing_sources", playing},
            {"configuration_checks", impl_->configuration_checks},
            {"output", impl_->config.output == AudioOutput::Offline          ? "offline"
                       : impl_->config.output == AudioOutput::NullDeviceTest ? "null_test"
                                                                             : "device"},
            {"paused", impl_->is_paused},
            {"voices", impl_->voices.size()},
            {"clips", impl_->clips.size()},
            {"decoded_bytes", impl_->cache_bytes},
            {"failed_sources", impl_->failures.size()},
            {"master_volume", impl_->config.master_volume},
            {"channel_volumes", impl_->config.channel_volumes},
            {"tick", impl_->tick}};
}
void AudioRuntime::read_offline(std::span<float> out) {
    impl_->check();
    if (impl_->config.output != AudioOutput::Offline || out.size() % 2)
        throw std::runtime_error("PCM reads require explicit offline stereo output");
    checked(ma_engine_read_pcm_frames(&impl_->engine, out.data(), out.size() / 2, nullptr),
            "Offline audio read");
}
const asset_detail::AudioClipData& AudioPreviewAccess::clip(AudioRuntime& runtime, EntityRef ref) {
    runtime.impl_->check();
    const auto resolved = runtime.impl_->world.resolve(ref);
    const auto found = runtime.impl_->voices.find(resolved.entity);
    if (resolved.state != WorldContext::ResolveState::Available ||
        found == runtime.impl_->voices.end() || !found->second->ready ||
        runtime.impl_->voices.size() != 1 || found->second->authored.spatialized ||
        found->second->authored.pitch != 1 || found->second->authored.bus != 0)
        throw std::runtime_error("Preview transport requires one live nonspatial unpitched source");
    return *found->second->clip;
}
double AudioPreviewAccess::elapsed(AudioRuntime& runtime, EntityRef ref) {
    (void)clip(runtime, ref);
    const auto& voice = runtime.impl_->voices.at(runtime.impl_->world.resolve(ref).entity);
    // Atomic output-node clock, not the mutable source cursor. Pitch is fixed at1.
    return double(ma_sound_get_time_in_pcm_frames(&voice->sound)) /
           ma_engine_get_sample_rate(&runtime.impl_->engine);
}
double AudioPreviewAccess::seek(AudioRuntime& runtime, EntityRef ref, double seconds) {
    const auto& data = clip(runtime, ref);
    if (!std::isfinite(seconds) || seconds < 0 || seconds > data.duration())
        throw std::runtime_error("Preview position must be within the clip duration");
    const auto frame = std::min<std::uint64_t>(std::uint64_t(seconds * data.rate), data.frames - 1);
    auto& impl = *runtime.impl_;
    auto& voice = *impl.voices.at(impl.world.resolve(ref).entity);
    // Pinned miniaudio uninit detaches/waits graph readers before returning.
    // Recreate the node to discard resampler/processing caches, retaining PCM/device.
    ma_sound_uninit(&voice.sound);
    voice.ready = false;
    checked(ma_audio_buffer_ref_seek_to_pcm_frame(&voice.buffer, frame), "Preview seek");
    voice.sound = {};
    checked(
        ma_sound_init_from_data_source(&impl.engine, &voice.buffer, 0, &impl.group, &voice.sound),
        "Preview sound reset");
    voice.ready = true;
    ma_sound_set_spatialization_enabled(&voice.sound, MA_FALSE);
    ma_sound_set_looping(&voice.sound, voice.authored.loop);
    ma_sound_set_volume(&voice.sound, voice.source_volume);
    checked(ma_sound_start(&voice.sound), "Preview sound resume");
    return double(frame) / data.rate;
}
EngineModule audio_module(AudioConfig config) {
    auto result = audio_schema_module();
    result.runtime_roles = role_mask(WorldRole::Runtime);
    result.allowed_services = capability(Capability::Diagnostics) |
                              capability(Capability::Profiling) | capability(Capability::Audio);
    result.provided_services = capability(Capability::Audio);
    result.start = [config](ModuleContext& c) {
        try {
            auto state = std::make_shared<AudioRuntime>(*c.owner, config);
            c.state = state;
            c.services.publish_audio(state);
        } catch (const std::exception& e) {
            if (config.required)
                throw;
            Diagnostic d{Severity::Warning,
                         "audio.unavailable",
                         std::string("Audio unavailable; runtime continues silently: ") + e.what(),
                         {}};
            d.context.module = "forge.audio";
            d.context.world_role = world_role_name(c.role);
            c.services.emit(std::move(d));
        }
    };
    result.stop = [](ModuleContext& c) {
        if (c.state)
            std::static_pointer_cast<AudioRuntime>(c.state)->shutdown();
        c.services.publish_audio({});
    };
    return result;
}
} // namespace forge
