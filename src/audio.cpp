#include <algorithm>
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
constexpr std::size_t max_voices = 256, max_commands = 1024, max_file = 16 * 1024 * 1024;
constexpr std::size_t max_clip = 64 * 1024 * 1024, max_cache = 256 * 1024 * 1024;
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
void validate(const AudioSource& s) {
    if (!std::isfinite(s.gain) || s.gain < 0 || s.gain > 4 || !std::isfinite(s.pitch) ||
        s.pitch < .25f || s.pitch > 4 || !std::isfinite(s.minimum_distance) ||
        !std::isfinite(s.maximum_distance) || s.minimum_distance < .001f ||
        s.maximum_distance < s.minimum_distance || s.maximum_distance > 10000)
        throw std::runtime_error("Invalid audio gain, pitch or distance range");
}
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
    struct Clip {
        std::vector<float> pcm;
        ma_uint64 frames{};
        ma_uint32 channels{}, rate{};
    };
    struct Voice {
        AudioSource authored;
        EntityRef ref;
        std::shared_ptr<Clip> clip;
        ma_audio_buffer_ref buffer{};
        ma_sound sound{};
        bool ready = false, buffer_ready = false;
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
        bool play;
    };
    std::vector<Command> commands;
    std::size_t cache_bytes = 0;
    std::string listener_error;
    std::set<std::uint64_t> spatial_errors;
    Impl(WorldContext& w, AudioConfig c)
        : world(w), config(std::move(c)), catalog(AssetCatalog::open_project(config.project)) {
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
        const auto resolved = catalog.resolve(ref);
        if (resolved.state != AssetState::Available)
            throw std::runtime_error(resolved.diagnostic);
        const auto path = ProjectPaths(config.project).resolve(resolved.record->source);
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".wav")
            throw std::runtime_error("Audio supports WAV clips only");
        const auto size = std::filesystem::file_size(path);
        if (size > max_file || size < 12)
            throw std::runtime_error("WAV source must be 12 bytes to 16 MiB");
        std::ifstream stream(path, std::ios::binary);
        std::vector<char> bytes(static_cast<std::size_t>(size));
        if (!stream.read(bytes.data(), static_cast<std::streamsize>(size)))
            throw std::runtime_error("Cannot read AudioClip source");
        ma_decoder decoder{};
        auto dc = ma_decoder_config_init(ma_format_f32, 0, 0);
        checked(ma_decoder_init_memory(bytes.data(), bytes.size(), &dc, &decoder), "WAV decode");
        struct End {
            ma_decoder* d;
            ~End() { ma_decoder_uninit(d); }
        } end{&decoder};
        auto clip = std::make_shared<Clip>();
        clip->channels = decoder.outputChannels;
        clip->rate = decoder.outputSampleRate;
        checked(ma_decoder_get_length_in_pcm_frames(&decoder, &clip->frames), "WAV length");
        if (!clip->frames || !clip->rate || clip->channels < 1 || clip->channels > 2 ||
            clip->frames > max_clip / (sizeof(float) * clip->channels))
            throw std::runtime_error("WAV must be mono/stereo and decode to at most 64 MiB");
        const auto n = std::size_t(clip->frames * clip->channels);
        if (n * sizeof(float) > max_cache - cache_bytes)
            throw std::runtime_error(
                "Audio decoded cache exceeds 256 MiB; restart Play with fewer clips");
        clip->pcm.resize(n);
        ma_uint64 got = 0;
        checked(ma_decoder_read_pcm_frames(&decoder, clip->pcm.data(), clip->frames, &got),
                "WAV samples");
        if (got != clip->frames)
            throw std::runtime_error("Truncated WAV samples");
        for (float sample : clip->pcm)
            if (!std::isfinite(sample))
                throw std::runtime_error("Nonfinite WAV samples");
        cache_bytes += n * sizeof(float);
        clips.emplace(ref.id, clip);
        return clip;
    }
    void queue(EntityRef ref, bool play) {
        check();
        const auto r = world.resolve(ref);
        if (r.state != WorldContext::ResolveState::Available ||
            !world.world().entity(r.entity).has<AudioSource>())
            throw std::runtime_error("Audio command references missing, ambiguous or stale source");
        if (commands.size() >= max_commands)
            throw std::runtime_error("Audio command limit reached");
        commands.push_back({ref, r.entity, play});
    }
    void sync(std::uint64_t next_tick) {
        check();
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
        if (listener_valid) {
            const auto& t = listeners.front().get<WorldTransform>();
            const auto p = t.affine.point({0, 0, 0});
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
        }
        std::set<std::uint64_t> present;
        for (auto e : sources) {
            present.insert(e.id());
            const auto config_value = e.get<AudioSource>();
            const auto ref = world.reference(e.id()).value();
            if (failed_config.contains(e.id()) && failed_config.at(e.id()) == config_value)
                continue;
            try {
                validate(config_value);
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
                const bool spatial_valid = listener_valid && usable_pose(e);
                ma_sound_set_volume(
                    &v.sound, (!config_value.spatialized || spatial_valid) ? config_value.gain : 0);
                if (config_value.spatialized && !spatial_valid) {
                    if (spatial_errors.insert(e.id()).second)
                        diagnostic("audio.spatial_unavailable",
                                   "Spatial sound requires resolved source and listener transforms "
                                   "within +/-1e9 meters and exactly one enabled listener",
                                   ref, config_value.clip.id);
                } else
                    spatial_errors.erase(e.id());
                ma_sound_set_attenuation_model(&v.sound, ma_attenuation_model_inverse);
                ma_sound_set_min_distance(&v.sound, config_value.minimum_distance);
                ma_sound_set_max_distance(&v.sound, config_value.maximum_distance);
                ma_sound_set_doppler_factor(&v.sound, 0);
                if (usable_pose(e)) {
                    const auto p = e.get<WorldTransform>().affine.point({0, 0, 0});
                    ma_sound_set_position(&v.sound, float(p[0]), float(p[1]), float(p[2]));
                }
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
            auto& sound = voices.at(r.entity)->sound;
            if (c.play) {
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Restart source");
                checked(ma_sound_start(&sound), "Play source");
            } else {
                checked(ma_sound_stop(&sound), "Stop source");
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Reset source");
            }
        }
    }
};
AudioRuntime::AudioRuntime(WorldContext& w, AudioConfig c)
    : impl_(std::make_unique<Impl>(w, std::move(c))) {}
AudioRuntime::~AudioRuntime() = default;
void AudioRuntime::shutdown() noexcept { impl_->shutdown(); }
void AudioRuntime::synchronize(std::uint64_t tick) { impl_->sync(tick); }
void AudioRuntime::paused(bool value) {
    impl_->check();
    if (value == impl_->is_paused)
        return;
    checked(value ? ma_sound_group_stop(&impl_->group) : ma_sound_group_start(&impl_->group),
            "Audio pause/resume");
    impl_->is_paused = value;
}
void AudioRuntime::play(EntityRef ref) { impl_->queue(ref, true); }
void AudioRuntime::stop_source(EntityRef ref) { impl_->queue(ref, false); }
Json AudioRuntime::status() const {
    impl_->check();
    unsigned playing = 0;
    for (const auto& [id, voice] : impl_->voices) {
        (void)id;
        playing += ma_sound_is_playing(&voice->sound);
    }
    return {{"mixed_frames", impl_->mixed_frames.load(std::memory_order_relaxed)},
            {"playing_sources", playing},
            {"output", impl_->config.output == AudioOutput::Offline          ? "offline"
                       : impl_->config.output == AudioOutput::NullDeviceTest ? "null_test"
                                                                             : "device"},
            {"paused", impl_->is_paused},
            {"voices", impl_->voices.size()},
            {"clips", impl_->clips.size()},
            {"decoded_bytes", impl_->cache_bytes},
            {"failed_sources", impl_->failures.size()},
            {"tick", impl_->tick}};
}
void AudioRuntime::read_offline(std::span<float> out) {
    impl_->check();
    if (impl_->config.output != AudioOutput::Offline || out.size() % 2)
        throw std::runtime_error("PCM reads require explicit offline stereo output");
    checked(ma_engine_read_pcm_frames(&impl_->engine, out.data(), out.size() / 2, nullptr),
            "Offline audio read");
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
