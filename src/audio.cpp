#include "audio_preview_access.hpp"
#include "audio_resource.hpp"
#include "audio_reverb.hpp"
#include "audio_source_access.hpp"
#include "audio_stream_source.hpp"
#include "audio_values.hpp"
#include "builtins.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <forge/application_state_activation.hpp>
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <miniaudio.h>
#include <optional>
#include <set>
#include <thread>
namespace forge {
namespace {
constexpr std::size_t max_voices = 256, max_commands = 1024, max_source_shots = 16;
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
bool same_failed_config(const AudioSource& a, const AudioSource& b) {
    if (a == b)
        return true;
    if (a.clip != b.clip || a.play_on_start != b.play_on_start || a.loop != b.loop ||
        a.spatialized != b.spatialized || a.attenuation != b.attenuation ||
        a.cone_enabled != b.cone_enabled || a.bus != b.bus || a.custom_bus != b.custom_bus ||
        a.pause_with_game != b.pause_with_game || a.low_pass_enabled != b.low_pass_enabled ||
        a.high_pass_enabled != b.high_pass_enabled || a.echo_enabled != b.echo_enabled ||
        a.reverb_enabled != b.reverb_enabled)
        return false;
    const std::array left{a.gain,          a.pitch,        a.minimum_distance, a.maximum_distance,
                          a.rolloff,       a.inner_angle,  a.outer_angle,      a.outer_gain,
                          a.doppler,       a.low_pass_hz,  a.high_pass_hz,     a.echo_seconds,
                          a.echo_feedback, a.reverb_decay, a.reverb_damping,   a.reverb_wet};
    const std::array right{b.gain,          b.pitch,        b.minimum_distance, b.maximum_distance,
                           b.rolloff,       b.inner_angle,  b.outer_angle,      b.outer_gain,
                           b.doppler,       b.low_pass_hz,  b.high_pass_hz,     b.echo_seconds,
                           b.echo_feedback, b.reverb_decay, b.reverb_damping,   b.reverb_wet};
    for (unsigned i = 0; i < left.size(); ++i)
        if (!(left[i] == right[i] || (std::isnan(left[i]) && std::isnan(right[i]))))
            return false;
    return true;
}
struct AudioMotion {
    Double3 position{};
    Double3 last_velocity{};
    std::uint64_t tick = 0;
    bool valid = false;
    Double3 observe(Double3 next, std::uint64_t next_tick, float fixed_delta,
                    bool loading_only = false) {
        if (loading_only && valid && next_tick == tick && next == position)
            return last_velocity;
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
        last_velocity = velocity;
        return velocity;
    }
};
} // namespace
struct AudioRuntime::Impl {
    WorldContext& world;
    AudioConfig config;
    AudioRoutingProjection routing;
    Json routing_document;
    AssetCatalog catalog;
    ma_engine engine{};
    ma_sound_group group{};
    ma_sound_group pausable_group{};
    bool engine_ready = false, group_ready = false, pausable_group_ready = false;
    bool stopped = false, is_paused = true, is_game_paused = false;
    std::uint64_t tick = 0;
    std::atomic<unsigned> device_events{0};
    std::atomic<std::uint64_t> mixed_frames{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::thread::id owner = std::this_thread::get_id();
    ResourcePool<AudioClipAsset> clips{{2, max_voices, 4096, max_cache}};
    std::unique_ptr<asset_detail::AudioRetirement> stream_retirement;
    std::shared_ptr<asset_detail::AudioStreamBudget> stream_backing;
    std::shared_ptr<asset_detail::AudioStreamSlots> stream_slots;
    std::map<AssetId, ResourceTicket> requests;
    std::map<std::uint64_t, AssetId> pending_sources;
    struct ReverbNode : ma_node_base {
        unsigned channels;
        detail::AudioReverb signal;
        ReverbNode(ma_engine& engine, const AudioSource& source)
            : ma_node_base{}, channels(ma_engine_get_channels(&engine)),
              signal(ma_engine_get_sample_rate(&engine), channels, source.reverb_decay,
                     source.reverb_damping, source.reverb_wet) {
            static const ma_node_vtable table{process, nullptr, 1, 1,
                                              MA_NODE_FLAG_CONTINUOUS_PROCESSING |
                                                  MA_NODE_FLAG_ALLOW_NULL_INPUT};
            auto config = ma_node_config_init();
            config.vtable = &table;
            config.pInputChannels = &channels;
            config.pOutputChannels = &channels;
            checked(ma_node_init(ma_engine_get_node_graph(&engine), &config, nullptr,
                                 static_cast<ma_node_base*>(this)),
                    "Source reverb");
        }
        ReverbNode(const ReverbNode&) = delete;
        ReverbNode& operator=(const ReverbNode&) = delete;
        ~ReverbNode() { ma_node_uninit(static_cast<ma_node_base*>(this), nullptr); }
        static void process(ma_node* node, const float** inputs, ma_uint32* input_frames,
                            float** outputs, ma_uint32* output_frames) noexcept {
            auto& self = *static_cast<ReverbNode*>(static_cast<ma_node_base*>(node));
            const auto frames = *output_frames;
            // Continuous nodes receive a padded input frame count even for
            // null silence. Consume that count so native input caches advance.
            const auto taken = std::min(*input_frames, frames);
            self.signal.process(inputs ? inputs[0] : nullptr, outputs[0], taken);
            self.signal.process(nullptr, outputs[0] + std::size_t(taken) * self.channels,
                                frames - taken);
            *input_frames = taken;
        }
    };
    struct EffectMemory {
        std::uint64_t echo = 0, reverb = 0;
        EffectMemory& operator+=(EffectMemory other) {
            echo += other.echo;
            reverb += other.reverb;
            return *this;
        }
    };
    struct SourceEffects {
        ma_lpf_node low{};
        ma_hpf_node high{};
        ma_delay_node echo{};
        std::unique_ptr<ReverbNode> reverb;
        float reverb_decay = 0, reverb_damping = 0, reverb_wet = 0;
        std::uint64_t echo_tail_frames = 0;
        bool low_ready = false, high_ready = false;
        bool echo_ready = false;
        float low_hz = 0, high_hz = 0;
        float echo_seconds = 0, echo_feedback = 0;
        std::uint64_t tail_frames = 0;
        std::uint64_t echo_buffer_bytes = 0;
        static bool has_echo(const AudioSource& source) {
            return source.echo_enabled && source.echo_feedback > 0;
        }
        static bool has_reverb(const AudioSource& source) {
            return source.reverb_enabled && source.reverb_wet > 0;
        }
        EffectMemory memory() const {
            return {echo_buffer_bytes, reverb ? reverb->signal.bytes() : 0};
        }
        // Build detached nodes; never reinitialize DSP state on the device thread.
        void initialize(ma_engine& engine, const AudioSource& source) {
            auto* graph = ma_engine_get_node_graph(&engine);
            const auto channels = ma_engine_get_channels(&engine);
            const auto rate = ma_engine_get_sample_rate(&engine);
            if (source.low_pass_enabled) {
                const auto config = ma_lpf_node_config_init(channels, rate, source.low_pass_hz, 2);
                checked(ma_lpf_node_init(graph, &config, nullptr, &low), "Low-pass filter");
                low_ready = true;
                low_hz = source.low_pass_hz;
            }
            if (source.high_pass_enabled) {
                const auto config = ma_hpf_node_config_init(channels, rate, source.high_pass_hz, 2);
                checked(ma_hpf_node_init(graph, &config, nullptr, &high), "High-pass filter");
                high_ready = true;
                high_hz = source.high_pass_hz;
            }
            if (high_ready && low_ready)
                checked(ma_node_attach_output_bus(&high, 0, &low, 0), "Audio filter chain");
            if (has_echo(source)) {
                const auto frames =
                    static_cast<ma_uint32>(std::llround(source.echo_seconds * rate));
                auto config =
                    ma_delay_node_config_init(channels, rate, frames, source.echo_feedback);
                // Native's zero-decay default selects a delayed start. FORGE's
                // echo always keeps immediate dry signal plus feedback repeats.
                config.delay.delayStart = MA_FALSE;
                checked(ma_delay_node_init(graph, &config, nullptr, &echo), "Source echo");
                echo_ready = true;
                echo_seconds = source.echo_seconds;
                echo_feedback = source.echo_feedback;
                echo_buffer_bytes = std::uint64_t(frames) * channels * sizeof(float);
                echo_tail_frames = frames * static_cast<std::uint64_t>(std::ceil(
                                                std::log(.0001) / std::log(double(echo_feedback))));
                if (low_ready || high_ready)
                    checked(ma_node_attach_output_bus(low_ready ? static_cast<ma_node*>(&low)
                                                                : static_cast<ma_node*>(&high),
                                                      0, &echo, 0),
                            "Audio echo input");
            }
            tail_frames = echo_tail_frames;
            if (has_reverb(source)) {
                reverb = std::make_unique<ReverbNode>(engine, source);
                reverb_decay = source.reverb_decay;
                reverb_damping = source.reverb_damping;
                reverb_wet = source.reverb_wet;
                tail_frames += reverb->signal.tail_frames();
                ma_node* upstream = echo_ready   ? static_cast<ma_node*>(&echo)
                                    : low_ready  ? static_cast<ma_node*>(&low)
                                    : high_ready ? static_cast<ma_node*>(&high)
                                                 : nullptr;
                if (upstream)
                    checked(ma_node_attach_output_bus(upstream, 0,
                                                      static_cast<ma_node_base*>(reverb.get()), 0),
                            "Audio reverb input");
            }
        }
        bool matches(const AudioSource& source) const {
            return low_ready == source.low_pass_enabled && high_ready == source.high_pass_enabled &&
                   (!low_ready || low_hz == source.low_pass_hz) &&
                   (!high_ready || high_hz == source.high_pass_hz) &&
                   echo_ready == has_echo(source) &&
                   (!echo_ready || (echo_seconds == source.echo_seconds &&
                                    echo_feedback == source.echo_feedback)) &&
                   bool(reverb) == has_reverb(source) &&
                   (!reverb ||
                    (reverb_decay == source.reverb_decay &&
                     reverb_damping == source.reverb_damping && reverb_wet == source.reverb_wet));
        }
        ma_node* input() {
            return high_ready   ? static_cast<ma_node*>(&high)
                   : low_ready  ? static_cast<ma_node*>(&low)
                   : echo_ready ? static_cast<ma_node*>(&echo)
                                : static_cast<ma_node_base*>(reverb.get());
        }
        ma_node* output() {
            if (reverb)
                return static_cast<ma_node_base*>(reverb.get());
            return echo_ready  ? static_cast<ma_node*>(&echo)
                   : low_ready ? static_cast<ma_node*>(&low)
                               : static_cast<ma_node*>(&high);
        }
        ~SourceEffects() {
            // Native node uninit detaches and waits for in-flight processing.
            reverb.reset();
            if (echo_ready)
                ma_delay_node_uninit(&echo, nullptr);
            if (high_ready)
                ma_hpf_node_uninit(&high, nullptr);
            if (low_ready)
                ma_lpf_node_uninit(&low, nullptr);
        }
    };
    struct Voice {
        AudioSource authored;
        EntityRef ref;
        ResourceLease<AudioClipAsset> clip;
        ma_audio_buffer_ref buffer{};
        asset_detail::AudioStreamSource::Pointer stream;
        ma_sound sound{};
        bool ready = false, buffer_ready = false;
        bool source_paused = false, start_pending = false;
        bool play_when_ready = false, one_shot = false, stream_loop = false;
        bool stream_starvation_reported = false, stream_error_reported = false;
        std::uint64_t stream_initial_frame = 0;
        bool follows_game_pause = true;
        float source_volume = 1;
        AudioMotion motion;
        std::unique_ptr<SourceEffects> effects;
        std::optional<std::uint64_t> effects_tail_start;
        bool effects_drained = false;
        EffectMemory effect_memory() const { return effects ? effects->memory() : EffectMemory{}; }
        void effects_running(bool running) {
            if (effects)
                checked(ma_node_set_state(effects->output(),
                                          running ? ma_node_state_started : ma_node_state_stopped),
                        "Audio effect pause/resume");
        }
        ~Voice() {
            if (ready)
                ma_sound_uninit(&sound);
            if (buffer_ready)
                ma_audio_buffer_ref_uninit(&buffer);
        }
    };
    struct SourceVoice : Voice {
        std::vector<std::unique_ptr<Voice>> one_shots;
        EffectMemory effect_memory() const {
            auto memory = Voice::effect_memory();
            for (const auto& shot : one_shots)
                memory += shot->effect_memory();
            return memory;
        }
    };
    static void apply_voice_gain(Voice& voice, const std::array<float, 3>& channels,
                                 const AudioRoutingProjection& routing) {
        if (!voice.ready)
            return;
        const auto channel =
            voice.authored.custom_bus.empty()
                ? (voice.authored.bus == 0 ? 1 : channels.at(voice.authored.bus - 1))
                : routing.gain(voice.authored.custom_bus, voice.authored.bus, channels);
        auto* output =
            voice.effects ? voice.effects->output() : static_cast<ma_node*>(&voice.sound);
        // Source/category faders follow buffered effects, so mute also covers
        // their retained tails. Reading the graph still advances source/DSP time.
        checked(ma_node_set_output_bus_volume(output, 0, voice.source_volume * channel),
                "Audio source output gain");
        if (voice.effects)
            checked(ma_node_set_output_bus_volume(&voice.sound, 0, 1), "Audio effect input gain");
        ma_sound_set_volume(&voice.sound, 1);
    }
    std::map<std::uint64_t, std::unique_ptr<SourceVoice>> voices;
    std::map<std::uint64_t, std::string> failures;
    std::map<std::uint64_t, AudioSource> failed_config;
    struct Command {
        EntityRef ref;
        std::uint64_t generation;
        AssetId clip;
        AudioSourceOperation operation;
    };
    std::vector<Command> commands;
    std::uint64_t configuration_checks = 0;
    std::string listener_error;
    AudioMotion listener_motion;
    std::uint64_t listener_entity = 0;
    bool listener_valid = false;
    Double3 listener_velocity{};
    std::set<std::uint64_t> spatial_errors;
    Impl(WorldContext& w, AudioConfig c)
        : world(w), config(std::move(c)),
          routing(AudioRoutingProjection::compile(config.routing, config.bus_volumes)),
          routing_document(audio_routing_json(config.routing)),
          catalog(AssetCatalog::open_project(config.project)) {
        config.bus_volumes = routing.volumes();
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
            if (std::ranges::any_of(catalog.records(), [](const auto& row) {
                    const auto metadata = row.second.metadata.find("forge.audio");
                    return metadata != row.second.metadata.end() && metadata->is_object() &&
                           metadata->contains("playback_storage") &&
                           metadata->at("playback_storage") == "stream";
                })) {
                stream_retirement = std::make_unique<asset_detail::AudioRetirement>();
                stream_backing = std::make_shared<asset_detail::AudioStreamBudget>(
                    std::filesystem::temp_directory_path() / "forge-runtime-audio",
                    stream_retirement->sink());
                stream_slots = std::make_shared<asset_detail::AudioStreamSlots>();
            }
            checked(ma_sound_group_init(&engine, 0, nullptr, &group), "Gameplay audio group");
            group_ready = true;
            ma_sound_group_set_volume(&group, config.master_volume);
            checked(ma_sound_group_stop(&group), "Pause initial audio group");
            checked(ma_sound_group_init(&engine, 0, &group, &pausable_group),
                    "Pausable audio group");
            pausable_group_ready = true;
            checked(ma_sound_group_start(&pausable_group), "Start pausable audio group");
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
        pending_sources.clear();
        requests.clear();
        clips.close();
        if (stream_retirement)
            stream_retirement->close();
        if (pausable_group_ready) {
            ma_sound_group_uninit(&pausable_group);
            pausable_group_ready = false;
        }
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
    ResourceLease<AudioClipAsset> load(AssetRef<AudioClipAsset> ref) {
        if (!ref.id)
            throw std::runtime_error("AudioSource has no AudioClip assigned");
        if (auto lease = clips.current(ref))
            return lease;
        auto scope = world.services().profile("audio", "AudioAssetResolve", tick);
        const auto record = catalog.records().find(ref.id);
        if (record == catalog.records().end())
            throw std::runtime_error("AudioClip is not registered");
        auto found = requests.find(ref.id);
        if (found == requests.end())
            found = requests
                        .emplace(ref.id, request_audio_clip(clips, config.project, record->second,
                                                            stream_backing))
                        .first;
        const auto info = found->second.inspect();
        if (resource_detail::terminal(info.state) && info.state != ResourceState::Ready)
            throw std::runtime_error(info.diagnostic.empty() ? "Audio clip load failed"
                                                             : info.diagnostic);
        return clips.acquire(found->second);
    }
    void queue(EntityRef ref, AudioSourceOperation operation, bool unpaused_only = false) {
        check();
        if (std::uint32_t(operation) > std::uint32_t(AudioSourceOperation::OneShot))
            throw std::runtime_error("Unknown audio source operation");
        const auto r = world.resolve(ref);
        if (r.state != WorldContext::ResolveState::Available ||
            !world.world().entity(r.entity).has<AudioSource>())
            throw std::runtime_error("Audio command references missing, ambiguous or stale source");
        if (commands.size() >= max_commands)
            throw std::runtime_error("Audio command limit reached");
        const auto& source = world.world().entity(r.entity).get<AudioSource>();
        if (unpaused_only) {
            const auto voice = voices.find(r.entity);
            if (is_paused || source.pause_with_game ||
                (voice != voices.end() && voice->second->authored.pause_with_game))
                throw std::runtime_error(
                    "Control-domain audio requires active output and Pause with game disabled");
        }
        const auto selected_clip = source.clip.id;
        if (operation == AudioSourceOperation::OneShot) {
            std::size_t queued = 0;
            for (auto it = commands.rbegin(); it != commands.rend(); ++it) {
                if (it->generation != r.entity || it->clip != selected_clip)
                    continue;
                if (it->operation == AudioSourceOperation::Stop)
                    break;
                queued += it->operation == AudioSourceOperation::OneShot;
            }
            if (queued >= max_source_shots)
                throw std::runtime_error("Queued one-shot limit is 16 per source");
        }
        commands.push_back({ref, r.entity, selected_clip, operation});
    }
    std::size_t shot_count() const {
        std::size_t count = 0;
        for (const auto& [id, source] : voices)
            count += source->one_shots.size();
        return count;
    }
    std::size_t voice_count() const { return voices.size() + shot_count(); }
    std::size_t outstanding_voice_count() const {
        return voice_count() +
               std::count_if(pending_sources.begin(), pending_sources.end(),
                             [&](const auto& row) { return !voices.contains(row.first); });
    }
    void update_output_pause() {
        // Preserve full-root silence/cursor behavior for ordinary scenes. Keep
        // the root active during game pause only when independent sources need it.
        const bool independent =
            !is_paused && is_game_paused && std::ranges::any_of(voices, [](const auto& entry) {
                return !entry.second->authored.pause_with_game;
            });
        const bool should_play = !is_paused && (!is_game_paused || independent);
        if (bool(ma_sound_group_is_playing(&group)) != should_play)
            checked(should_play ? ma_sound_group_start(&group) : ma_sound_group_stop(&group),
                    "Audio output pause/resume");
    }
    bool voice_finished(Voice& voice) {
        if (!voice.ready || voice.start_pending || voice.source_paused ||
            !ma_sound_at_end(&voice.sound))
            return false;
        if (!voice.effects || !voice.effects->tail_frames || voice.effects_drained)
            return true;
        const auto time = ma_node_get_time(voice.effects->output());
        if (!voice.effects_tail_start)
            voice.effects_tail_start = time;
        if (time - *voice.effects_tail_start < voice.effects->tail_frames)
            return false;
        voice.effects_running(false);
        voice.effects_drained = true;
        return true;
    }
    static bool effects_tail(const Voice& voice) {
        return voice.ready && voice.effects && voice.effects->tail_frames &&
               !voice.effects_drained && ma_sound_at_end(&voice.sound);
    }
    static bool echo_tail(const Voice& voice) {
        if (!effects_tail(voice) || !voice.effects->echo_ready)
            return false;
        return !voice.effects_tail_start ||
               ma_node_get_time(voice.effects->output()) - *voice.effects_tail_start <
                   voice.effects->echo_tail_frames;
    }
    static bool reverb_tail(const Voice& voice) {
        return effects_tail(voice) && bool(voice.effects->reverb);
    }
    void reap_finished() {
        for (auto& [id, source] : voices) {
            const auto observe = [&](Voice& voice) {
                if (!voice.stream)
                    return;
                if (voice.stream->underruns() && !voice.stream_starvation_reported) {
                    diagnostic(
                        "audio.stream_starved",
                        "Audio stream pages were not ready in time; output may contain silence. "
                        "Reduce simultaneous streams or use Resident playback storage.",
                        voice.ref, voice.authored.clip.id);
                    voice.stream_starvation_reported = true;
                }
                if (const auto error = voice.stream->read_error();
                    error != MA_SUCCESS && !voice.stream_error_reported) {
                    diagnostic("audio.stream_read",
                               std::string("Audio stream read failed: ") +
                                   ma_result_description(error),
                               voice.ref, voice.authored.clip.id);
                    voice.stream_error_reported = true;
                }
            };
            observe(*source);
            for (auto& shot : source->one_shots)
                observe(*shot);
            if (source->one_shots.empty() && (!source->effects || !source->effects->tail_frames))
                continue;
            const auto resolved = world.resolve(source->ref);
            if (resolved.state != WorldContext::ResolveState::Available || resolved.entity != id ||
                !world.world().entity(id).has<AudioSource>() ||
                world.world().entity(id).get<AudioSource>().clip != source->authored.clip) {
                source->one_shots.clear();
                source->effects_running(false);
                source->effects_drained = true;
                continue;
            }
            (void)voice_finished(*source);
            std::erase_if(source->one_shots,
                          [&](const auto& shot) { return voice_finished(*shot); });
        }
    }
    bool initialize_voice(Voice& voice) {
        if (voice.ready)
            return true;
        ma_data_source* source = nullptr;
        if (const auto* resident = voice.clip->resident()) {
            checked(ma_audio_buffer_ref_init(ma_format_f32, resident->channels,
                                             resident->pcm.data(), resident->frames, &voice.buffer),
                    "Audio buffer");
            voice.buffer_ready = true;
            voice.buffer.sampleRate = resident->rate;
            source = &voice.buffer;
        } else {
            if (!stream_retirement || !stream_slots)
                throw std::runtime_error("Audio streaming storage is unavailable");
            const bool loop = !voice.one_shot && voice.authored.loop;
            if (voice.stream && voice.stream_loop != loop)
                voice.stream.reset();
            if (!voice.stream) {
                voice.stream = asset_detail::AudioStreamSource::create(
                    *ma_engine_get_resource_manager(&engine), voice.clip->stream_backing(),
                    stream_slots, stream_retirement->sink(), loop, voice.stream_initial_frame);
                voice.stream_loop = loop;
            }
            if (!voice.stream || !voice.stream->ready())
                return false;
            source = voice.stream->data_source();
        }
        auto* parent = voice.authored.pause_with_game ? &pausable_group : &group;
        checked(ma_sound_init_from_data_source(&engine, source, 0, parent, &voice.sound),
                "Audio voice");
        voice.ready = true;
        voice.follows_game_pause = voice.authored.pause_with_game;
        // Source sample rate is supplied before attaching the buffer to the engine.
        ma_sound_set_pitch(&voice.sound, voice.authored.pitch);
        return true;
    }
    void reprepare_stream(Voice& voice, std::uint64_t frame, bool play) {
        voice.effects_running(false);
        if (voice.ready)
            ma_sound_uninit(&voice.sound);
        voice.effects.reset();
        voice.sound = {};
        voice.ready = false;
        voice.stream.reset();
        voice.stream_initial_frame = frame;
        voice.play_when_ready = play;
        voice.source_paused = false;
        voice.effects_tail_start.reset();
        voice.effects_drained = false;
        voice.stream_starvation_reported = false;
        voice.stream_error_reported = false;
    }
    void configure_effects(Voice& v, bool reset = false) {
        const auto& config_value = v.authored;
        auto* parent = config_value.pause_with_game ? &pausable_group : &group;
        const bool filtered = config_value.low_pass_enabled || config_value.high_pass_enabled ||
                              SourceEffects::has_echo(config_value) ||
                              SourceEffects::has_reverb(config_value);
        if (reset || (v.effects ? !v.effects->matches(config_value) : filtered)) {
            std::unique_ptr<SourceEffects> next;
            if (filtered) {
                next = std::make_unique<SourceEffects>();
                next->initialize(engine, config_value);
                checked(ma_node_set_state(next->output(), !v.source_paused &&
                                                                  ma_sound_is_playing(&v.sound) &&
                                                                  !ma_sound_at_end(&v.sound)
                                                              ? ma_node_state_started
                                                              : ma_node_state_stopped),
                        "Audio effect initial state");
                checked(ma_node_set_output_bus_volume(next->output(), 0, 0),
                        "Audio detached effect gain");
                checked(ma_node_attach_output_bus(next->output(), 0, parent, 0),
                        "Audio effect output");
            }
            checked(ma_node_attach_output_bus(
                        &v.sound, 0, next ? next->input() : static_cast<ma_node*>(parent), 0),
                    "Audio effect input");
            v.effects = std::move(next);
            v.effects_tail_start.reset();
            v.effects_drained = ma_sound_at_end(&v.sound);
            apply_voice_gain(v, config.channel_volumes, routing);
        } else if (v.follows_game_pause != config_value.pause_with_game) {
            checked(ma_node_attach_output_bus(v.effects ? v.effects->output()
                                                        : static_cast<ma_node*>(&v.sound),
                                              0, parent, 0),
                    "Audio pause routing");
        }
        v.follows_game_pause = config_value.pause_with_game;
    }
    void configure_voice(Voice& v, flecs::entity e, float fixed_delta, bool loading_only,
                         bool one_shot = false) {
        if (!v.ready)
            return;
        if (v.stream)
            checked(v.stream->read_error(), "Audio stream read");
        const auto& config_value = v.authored;
        configure_effects(v);
        ma_sound_set_looping(&v.sound, (one_shot ? false : config_value.loop));
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
        v.source_volume = (!config_value.spatialized || spatial_valid) ? config_value.gain : 0;
        apply_voice_gain(v, config.channel_volumes, routing);
        if (config_value.spatialized && !spatial_valid) {
            if (spatial_errors.insert(e.id()).second)
                diagnostic("audio.spatial_unavailable",
                           "Spatial sound requires resolved source and listener transforms "
                           "within +/-1e9 meters, exactly one enabled listener, and a "
                           "nonzero finite emitter forward for directional sound",
                           v.ref, config_value.clip.id);
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
            &v.sound, config_value.cone_enabled ? config_value.inner_angle * radians : full_opening,
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
                source_velocity = v.motion.observe(p, tick, fixed_delta, loading_only);
                const double speed = std::max(
                    std::hypot(source_velocity[0], source_velocity[1], source_velocity[2]),
                    std::hypot(listener_velocity[0], listener_velocity[1], listener_velocity[2]));
                // Selected native Doppler divides by c - factor*radial_velocity.
                // Bound both scaled speeds to c/2: finite ratio in [1/3, 3].
                constexpr double half_speed_of_sound = 343.3 / 2;
                doppler = float(
                    speed > 0 ? std::min(double(config_value.doppler), half_speed_of_sound / speed)
                              : config_value.doppler);
            } else
                v.motion.valid = false;
        } else
            v.motion.valid = false;
        ma_sound_set_velocity(&v.sound, float(source_velocity[0]), float(source_velocity[1]),
                              float(source_velocity[2]));
        ma_sound_set_doppler_factor(&v.sound, doppler);
    }
    void add_shot(SourceVoice& source, std::uint64_t id) {
        if (source.one_shots.size() >= max_source_shots ||
            outstanding_voice_count() >= max_voices) {
            diagnostic("audio.one_shot_limit",
                       "One-shot rejected: 16 per source or 256 total voices", source.ref,
                       source.authored.clip.id);
            return;
        }
        auto shot = std::make_unique<Voice>();
        shot->ref = source.ref;
        shot->authored = source.authored;
        shot->one_shot = true;
        shot->clip = source.clip;
        shot->motion = source.motion;
        if (!initialize_voice(*shot))
            pending_sources[id] = source.authored.clip.id;
        configure_voice(*shot, world.world().entity(id), 0, true, true);
        shot->source_paused = source.source_paused;
        shot->start_pending = !shot->source_paused;
        source.one_shots.push_back(std::move(shot));
    }
    void sync(std::uint64_t next_tick, float fixed_delta,
              std::span<const std::uint64_t> discontinuities, bool loading_only = false) {
        check();
        reap_finished();
        clips.pump();
        pending_sources.clear();
        std::set<std::uint64_t> created_voices;
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
                if (affected(id)) {
                    voice->motion.valid = false;
                    for (auto& shot : voice->one_shots)
                        shot->motion.valid = false;
                }
        }
        tick = next_tick;
        const auto notifications = device_events.exchange(0, std::memory_order_relaxed);
        if (notifications & ((1u << unsigned(ma_device_notification_type_stopped)) |
                             (1u << unsigned(ma_device_notification_type_rerouted))))
            diagnostic("audio.device_changed", "Audio output stopped or changed. Backend may "
                                               "recover; restart Play if sound does not return.");
        auto scope = world.services().profile("audio", "AudioSync", tick);
        // Read on owner thread only; audio callback sees exclusively miniaudio-owned node state.
        ApplicationStateActivation conditions(world);
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
        std::erase_if(sources, [&](auto entity) { return !conditions.active(entity.id()); });
        std::erase_if(listeners, [&](auto entity) { return !conditions.active(entity.id()); });
        std::sort(sources.begin(), sources.end(), [](auto a, auto b) { return a.id() < b.id(); });
        std::sort(listeners.begin(), listeners.end(),
                  [](auto a, auto b) { return a.id() < b.id(); });
        listener_valid = listeners.size() == 1 && usable_pose(listeners.front());
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
        listener_velocity = {};
        if (listener_valid) {
            if (listener_entity != listeners.front().id())
                listener_motion.valid = false;
            listener_entity = listeners.front().id();
            const auto& t = listeners.front().get<WorldTransform>();
            const auto p = t.affine.point({0, 0, 0});
            listener_velocity = listener_motion.observe(p, next_tick, fixed_delta, loading_only);
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
                detail::validate_audio_source(e, config_value);
                if (!config_value.custom_bus.empty())
                    (void)routing.gain(config_value.custom_bus, config_value.bus,
                                       config.channel_volumes);
                auto found = voices.find(e.id());
                if (found != voices.end() && (found->second->authored.clip != config_value.clip ||
                                              found->second->ref != ref)) {
                    voices.erase(found);
                    found = voices.end();
                }
                const bool created = found == voices.end();
                if (created) {
                    if (outstanding_voice_count() >= max_voices)
                        throw std::runtime_error("Audio voice limit is 256");
                    auto voice = std::make_unique<SourceVoice>();
                    voice->ref = ref;
                    voice->authored = config_value;
                    voice->play_when_ready = config_value.play_on_start;
                    voice->clip = load(config_value.clip);
                    if (!voice->clip) {
                        pending_sources.emplace(e.id(), config_value.clip.id);
                        failures.erase(e.id());
                        failed_config.erase(e.id());
                        continue;
                    }
                    found = voices.emplace(e.id(), std::move(voice)).first;
                }
                auto& v = *found->second;
                v.authored = config_value;
                if (!v.ready) {
                    if (initialize_voice(v))
                        created_voices.insert(e.id());
                    else
                        pending_sources.emplace(e.id(), config_value.clip.id);
                }
                configure_voice(v, e, fixed_delta, loading_only);
                for (auto shot = v.one_shots.begin(); shot != v.one_shots.end();) {
                    try {
                        (*shot)->authored = config_value;
                        if (!(*shot)->ready && !initialize_voice(**shot))
                            pending_sources.emplace(e.id(), config_value.clip.id);
                        configure_voice(**shot, e, fixed_delta, loading_only, true);
                        ++shot;
                    } catch (const std::exception& error) {
                        diagnostic("audio.one_shot", error.what(), ref, config_value.clip.id);
                        shot = v.one_shots.erase(shot);
                    }
                }
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
        // Cancel unused in-flight loads. Shared requests survive while a current
        // source still needs them; admitted cache revisions retain existing policy.
        for (auto it = requests.begin(); it != requests.end();) {
            const bool pending_use = std::ranges::any_of(
                pending_sources, [&](const auto& p) { return p.second == it->first; });
            const bool playing_use = std::ranges::any_of(
                voices, [&](const auto& p) { return p.second->authored.clip.id == it->first; });
            const auto clip = clips.current(AssetRef<AudioClipAsset>{it->first});
            // Resident clips keep the normal byte-bounded cache policy. Unused
            // stream snapshots leave this same pool rather than accumulating
            // zero-resident-byte entries against the temporary-file quota.
            const bool unused_stream = clip && !clip->resident() && !playing_use && !pending_use;
            if ((!resource_detail::terminal(it->second.inspect().state) && !pending_use) ||
                unused_stream) {
                clips.unload(AssetRef<AudioClipAsset>{it->first});
                it = requests.erase(it);
            } else
                ++it;
        }
        clips.collect();
        flush_commands(created_voices);
        update_output_pause();
    }
    void flush_commands(const std::set<std::uint64_t>& created = {}) {
        reap_finished();
        auto pending = std::move(commands);
        commands.clear();
        std::map<std::uint64_t, std::pair<bool, bool>> initial;
        for (auto id : created)
            if (voices.contains(id))
                initial.emplace(
                    id, std::pair{voices.at(id)->play_when_ready, voices.at(id)->source_paused});
        for (const auto& c : pending) {
            const auto r = world.resolve(c.ref);
            if (r.state != WorldContext::ResolveState::Available || r.entity != c.generation ||
                !world.world().entity(r.entity).has<AudioSource>() ||
                world.world().entity(r.entity).get<AudioSource>().clip.id != c.clip) {
                diagnostic("audio.stale_source", "Audio command source is stale or unavailable",
                           c.ref);
                continue;
            }
            if (!voices.contains(r.entity)) {
                if (pending_sources.contains(r.entity))
                    commands.push_back(c);
                else
                    diagnostic("audio.stale_source", "Audio command source is unavailable", c.ref);
                continue;
            }
            auto& voice = *voices.at(r.entity);
            // Existing one-shots remain controllable while their primary source
            // prepares a restart or seek.
            if (c.operation == AudioSourceOperation::Stop) {
                voice.one_shots.clear();
                voice.effects_running(false);
                voice.effects_drained = true;
                voice.effects_tail_start.reset();
            }
            for (auto& shot : voice.one_shots) {
                if (!shot->ready) {
                    if (c.operation == AudioSourceOperation::Pause && shot->start_pending) {
                        shot->start_pending = false;
                        shot->source_paused = true;
                    } else if (c.operation == AudioSourceOperation::Resume && shot->source_paused) {
                        shot->source_paused = false;
                        shot->start_pending = true;
                    }
                    continue;
                }
                if (c.operation == AudioSourceOperation::Pause &&
                    (shot->start_pending ||
                     (ma_sound_is_playing(&shot->sound) && !ma_sound_at_end(&shot->sound)) ||
                     effects_tail(*shot))) {
                    checked(ma_sound_stop(&shot->sound), "Pause one-shot");
                    shot->effects_running(false);
                    shot->start_pending = false;
                    shot->source_paused = true;
                } else if (c.operation == AudioSourceOperation::Resume && shot->source_paused) {
                    shot->source_paused = false;
                    shot->start_pending = true;
                }
            }
            if (!voice.ready) {
                if (c.operation == AudioSourceOperation::OneShot) {
                    commands.push_back(c);
                    continue;
                }
                if (c.operation == AudioSourceOperation::Stop ||
                    c.operation == AudioSourceOperation::Restart) {
                    voice.play_when_ready = c.operation == AudioSourceOperation::Restart;
                    voice.source_paused = false;
                    if (voice.stream_initial_frame) {
                        voice.stream.reset();
                        voice.stream_initial_frame = 0;
                    }
                } else if (c.operation == AudioSourceOperation::Pause && voice.play_when_ready) {
                    voice.play_when_ready = false;
                    voice.source_paused = true;
                } else if (c.operation == AudioSourceOperation::Resume && voice.source_paused) {
                    voice.play_when_ready = true;
                    voice.source_paused = false;
                }
                continue;
            }
            if (c.operation == AudioSourceOperation::OneShot) {
                try {
                    add_shot(voice, r.entity);
                } catch (const std::exception& error) {
                    diagnostic("audio.one_shot", error.what(), c.ref, c.clip);
                }
                continue;
            }
            if (auto it = initial.find(r.entity); it != initial.end()) {
                auto& [playing, paused] = it->second;
                switch (c.operation) {
                case AudioSourceOperation::Restart:
                    playing = true;
                    paused = false;
                    break;
                case AudioSourceOperation::Stop:
                    playing = false;
                    paused = false;
                    break;
                case AudioSourceOperation::Pause:
                    if (playing) {
                        playing = false;
                        paused = true;
                    }
                    break;
                case AudioSourceOperation::OneShot:
                    break;
                case AudioSourceOperation::Resume:
                    if (paused) {
                        playing = true;
                        paused = false;
                    }
                    break;
                }
                voice.source_paused = paused;
                continue;
            }
            auto& sound = voice.sound;
            switch (c.operation) {
            case AudioSourceOperation::Restart:
                if (voice.stream) {
                    reprepare_stream(voice, 0, true);
                    pending_sources[r.entity] = c.clip;
                    break;
                }
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Restart source");
                configure_effects(voice, true);
                checked(ma_sound_start(&sound), "Play source");
                voice.effects_running(true);
                voice.effects_drained = false;
                voice.source_paused = false;
                break;
            case AudioSourceOperation::Stop:
                if (voice.stream) {
                    reprepare_stream(voice, 0, false);
                    pending_sources[r.entity] = c.clip;
                    break;
                }
                checked(ma_sound_stop(&sound), "Stop source");
                checked(ma_sound_seek_to_pcm_frame(&sound, 0), "Reset source");
                voice.source_paused = false;
                break;
            case AudioSourceOperation::Pause:
                if ((ma_sound_is_playing(&sound) && !ma_sound_at_end(&sound)) ||
                    effects_tail(voice)) {
                    checked(ma_sound_stop(&sound), "Pause source");
                    voice.effects_running(false);
                    voice.source_paused = true;
                }
                break;
            case AudioSourceOperation::OneShot:
                break;
            case AudioSourceOperation::Resume:
                if (voice.source_paused) {
                    if (!ma_sound_at_end(&sound))
                        checked(ma_sound_start(&sound), "Resume source");
                    voice.effects_running(true);
                    voice.source_paused = false;
                }
                break;
            }
        }
        for (const auto& [id, intent] : initial) {
            auto& voice = *voices.at(id);
            voice.source_paused = intent.second;
            if (intent.first) {
                checked(ma_sound_start(&voice.sound), "Start prepared source");
                voice.effects_running(true);
                voice.effects_drained = false;
            }
        }
        for (auto& [id, source] : voices)
            for (auto& shot : source->one_shots)
                if (shot->ready && shot->start_pending) {
                    // Native start restarts an EOS clip. Resume only its retained
                    // effect when the clip has already finished.
                    if (!ma_sound_at_end(&shot->sound))
                        checked(ma_sound_start(&shot->sound), "Play one-shot");
                    shot->effects_running(true);
                    shot->start_pending = false;
                }
    }
    bool wait_sources(std::chrono::milliseconds timeout, bool loading_only);
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
    impl_->is_paused = value;
    impl_->update_output_pause();
}
void AudioRuntime::play(EntityRef ref) { impl_->queue(ref, AudioSourceOperation::Restart); }
void AudioRuntime::game_paused(bool value) {
    impl_->check();
    if (value == impl_->is_game_paused)
        return;
    checked(value ? ma_sound_group_stop(&impl_->pausable_group)
                  : ma_sound_group_start(&impl_->pausable_group),
            "Gameplay audio pause/resume");
    impl_->is_game_paused = value;
    impl_->update_output_pause();
}
void AudioRuntime::play_one_shot(EntityRef ref) {
    impl_->queue(ref, AudioSourceOperation::OneShot);
}
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
void AudioRuntime::control_unpaused_source(EntityRef ref, AudioSourceOperation operation) {
    impl_->queue(ref, operation, true);
    impl_->flush_commands();
}
void AudioSourceAccess::validate_route(AudioRuntime& runtime, std::string_view key) {
    runtime.impl_->check();
    (void)runtime.impl_->routing.gain(key, 0, runtime.impl_->config.channel_volumes);
}
Json AudioSourceAccess::status(AudioRuntime& runtime, EntityRef ref) {
    runtime.impl_->check();
    const auto resolved = runtime.impl_->world.resolve(ref);
    if (resolved.state != WorldContext::ResolveState::Available)
        throw std::runtime_error("AudioSource has no admitted voice in the current world");
    const auto found = runtime.impl_->voices.find(resolved.entity);
    const auto entity = runtime.impl_->world.world().entity(resolved.entity);
    const bool known = found != runtime.impl_->voices.end() && found->second->ref == ref &&
                       entity.has<AudioSource>() &&
                       entity.get<AudioSource>().clip == found->second->authored.clip;
    unsigned playing_shots = 0, paused_shots = 0;
    unsigned echo_tails = 0, effects_tails = 0, reverb_tails = 0;
    AudioRuntime::Impl::EffectMemory memory;
    if (known) {
        echo_tails = AudioRuntime::Impl::echo_tail(*found->second) ? 1 : 0;
        effects_tails = AudioRuntime::Impl::effects_tail(*found->second) ? 1 : 0;
        reverb_tails = AudioRuntime::Impl::reverb_tail(*found->second) ? 1 : 0;
        memory = found->second->effect_memory();
        for (const auto& shot : found->second->one_shots) {
            playing_shots +=
                shot->ready && ma_sound_is_playing(&shot->sound) && !ma_sound_at_end(&shot->sound);
            paused_shots += shot->source_paused;
            echo_tails += AudioRuntime::Impl::echo_tail(*shot);
            effects_tails += AudioRuntime::Impl::effects_tail(*shot);
            reverb_tails += AudioRuntime::Impl::reverb_tail(*shot);
        }
    }
    if ((found == runtime.impl_->voices.end() || !found->second->ready) &&
        (found == runtime.impl_->voices.end() || found->second->ref == ref) &&
        entity.has<AudioSource>() && runtime.impl_->pending_sources.contains(resolved.entity) &&
        entity.get<AudioSource>().clip.id == runtime.impl_->pending_sources.at(resolved.entity))
        return {
            {"source", ref},
            {"available", false},
            {"pending", true},
            {"one_shots", known ? found->second->one_shots.size() : 0},
            {"playing_one_shots", playing_shots},
            {"paused_one_shots", paused_shots},
            {"playing", false},
            {"source_paused", found != runtime.impl_->voices.end() && found->second->source_paused},
            {"paused", runtime.impl_->is_paused || (runtime.impl_->is_game_paused &&
                                                    entity.get<AudioSource>().pause_with_game)},
            {"pause_with_game", entity.get<AudioSource>().pause_with_game},
            {"bus", entity.get<AudioSource>().bus},
            {"custom_bus", entity.get<AudioSource>().custom_bus},
            {"low_pass_enabled", entity.get<AudioSource>().low_pass_enabled},
            {"high_pass_enabled", entity.get<AudioSource>().high_pass_enabled},
            {"low_pass_hz", entity.get<AudioSource>().low_pass_hz},
            {"high_pass_hz", entity.get<AudioSource>().high_pass_hz},
            {"echo_enabled", entity.get<AudioSource>().echo_enabled},
            {"echo_seconds", entity.get<AudioSource>().echo_seconds},
            {"echo_feedback", entity.get<AudioSource>().echo_feedback},
            {"reverb_enabled", entity.get<AudioSource>().reverb_enabled},
            {"reverb_decay", entity.get<AudioSource>().reverb_decay},
            {"reverb_damping", entity.get<AudioSource>().reverb_damping},
            {"reverb_wet", entity.get<AudioSource>().reverb_wet},
            {"echo_tail", false},
            {"effects_tail", false},
            {"effects_tails", effects_tails},
            {"reverb_tail", false},
            {"reverb_tails", reverb_tails},
            {"echo_tails", echo_tails},
            {"echo_buffer_bytes", memory.echo},
            {"reverb_buffer_bytes", memory.reverb},
            {"duration",
             found != runtime.impl_->voices.end() ? found->second->clip->info().duration() : 0}};
    if (found == runtime.impl_->voices.end() || !found->second->ready ||
        found->second->ref != ref || !entity.has<AudioSource>() ||
        entity.get<AudioSource>().clip != found->second->authored.clip)
        throw std::runtime_error("AudioSource has no admitted voice in the current world");
    return {{"source", ref},
            {"one_shots", found->second->one_shots.size()},
            {"playing_one_shots", playing_shots},
            {"paused_one_shots", paused_shots},
            {"available", true},
            {"pending", false},
            {"playing", bool(ma_sound_is_playing(&found->second->sound))},
            {"source_paused", found->second->source_paused},
            {"paused", runtime.impl_->is_paused || (runtime.impl_->is_game_paused &&
                                                    found->second->authored.pause_with_game)},
            {"pause_with_game", found->second->authored.pause_with_game},
            {"bus", found->second->authored.bus},
            {"custom_bus", found->second->authored.custom_bus},
            {"low_pass_enabled", found->second->authored.low_pass_enabled},
            {"high_pass_enabled", found->second->authored.high_pass_enabled},
            {"low_pass_hz", found->second->authored.low_pass_hz},
            {"high_pass_hz", found->second->authored.high_pass_hz},
            {"echo_enabled", found->second->authored.echo_enabled},
            {"echo_seconds", found->second->authored.echo_seconds},
            {"echo_feedback", found->second->authored.echo_feedback},
            {"reverb_enabled", found->second->authored.reverb_enabled},
            {"reverb_decay", found->second->authored.reverb_decay},
            {"reverb_damping", found->second->authored.reverb_damping},
            {"reverb_wet", found->second->authored.reverb_wet},
            {"echo_tail", AudioRuntime::Impl::echo_tail(*found->second)},
            {"echo_tails", echo_tails},
            {"effects_tail", AudioRuntime::Impl::effects_tail(*found->second)},
            {"effects_tails", effects_tails},
            {"reverb_tail", AudioRuntime::Impl::reverb_tail(*found->second)},
            {"reverb_tails", reverb_tails},
            {"echo_buffer_bytes", memory.echo},
            {"reverb_buffer_bytes", memory.reverb},
            {"loop", found->second->authored.loop},
            {"duration", found->second->clip->info().duration()},
            {"playback_storage", found->second->clip->resident() ? "resident" : "stream"}};
}
void AudioSourceAccess::control(AudioRuntime& runtime, EntityRef ref,
                                AudioSourceOperation operation) {
    (void)status(runtime, ref); // Admit identity/voice before queuing any mutation.
    runtime.impl_->queue(ref, operation);
    runtime.impl_->flush_commands(); // Owner boundary; no simulation tick or pose resampling.
}
void AudioRuntime::mix_volumes(float master, const std::array<float, 3>& channels) {
    mix_volumes(master, channels, impl_->routing.volumes());
}
void AudioRuntime::mix_volumes(float master, const std::array<float, 3>& channels,
                               const AudioBusVolumes& buses) {
    impl_->check();
    for (float value : {master, channels[0], channels[1], channels[2]})
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Audio mix volumes must be finite and between zero and one");
    auto routing = AudioRoutingProjection::compile(impl_->config.routing, buses, true);
    ma_sound_group_set_volume(&impl_->group, master);
    for (const auto& [id, voice] : impl_->voices) {
        (void)id;
        Impl::apply_voice_gain(*voice, channels, routing);
        for (auto& shot : voice->one_shots)
            Impl::apply_voice_gain(*shot, channels, routing);
    }
    impl_->config.master_volume = master;
    impl_->config.channel_volumes = channels;
    impl_->config.bus_volumes = routing.volumes();
    impl_->routing = std::move(routing);
}
Json AudioRuntime::status() const {
    impl_->check();
    unsigned playing = 0, streams = 0, preparing_streams = 0;
    std::uint64_t stream_underruns = 0;
    std::uint64_t stream_pages = 0;
    Impl::EffectMemory memory;
    const auto stream_memory = [&](const Impl::Voice& voice) {
        if (voice.stream) {
            ++streams;
            preparing_streams += !voice.ready;
            stream_pages += voice.stream->page_bytes();
            stream_underruns += voice.stream->underruns();
        }
    };
    for (const auto& [id, voice] : impl_->voices) {
        (void)id;
        playing += voice->ready && ma_sound_is_playing(&voice->sound);
        memory += voice->effect_memory();
        stream_memory(*voice);
        for (const auto& shot : voice->one_shots)
            stream_memory(*shot);
    }
    const auto resources = impl_->clips.statistics();
    Json result{
        {"mixed_frames", impl_->mixed_frames.load(std::memory_order_relaxed)},
        {"playing_sources", playing},
        {"configuration_checks", impl_->configuration_checks},
        {"output", impl_->config.output == AudioOutput::Offline          ? "offline"
                   : impl_->config.output == AudioOutput::NullDeviceTest ? "null_test"
                                                                         : "device"},
        {"paused", impl_->is_paused || impl_->is_game_paused},
        {"suspended", impl_->is_paused},
        {"voices", impl_->voice_count()},
        {"source_voices", impl_->voices.size()},
        {"one_shots", impl_->shot_count()},
        {"clips", resources.selected},
        {"decoded_bytes", resources.memory.cpu_asset},
        {"stream_voices", streams},
        {"preparing_streams", preparing_streams},
        {"stream_page_bytes", stream_pages},
        {"stream_reserved_page_bytes",
         impl_->stream_slots ? impl_->stream_slots->page_budget_bytes.load() : 0},
        {"stream_underruns", stream_underruns},
        {"stream_backing_files", impl_->stream_backing ? impl_->stream_backing->files.load() : 0},
        {"stream_backing_bytes", impl_->stream_backing ? impl_->stream_backing->bytes.load() : 0},
        {"reserved_streams", impl_->stream_slots ? impl_->stream_slots->reserved.load() : 0},
        {"retiring_streams", impl_->stream_slots ? impl_->stream_slots->retiring.load() : 0},
        {"echo_buffer_bytes", memory.echo},
        {"reverb_buffer_bytes", memory.reverb},
        {"pending_sources", impl_->pending_sources.size()},
        {"pending_clips", resources.pending},
        {"failed_sources", impl_->failures.size()},
        {"master_volume", impl_->config.master_volume},
        {"channel_volumes", impl_->config.channel_volumes},
        {"tick", impl_->tick}};
    if (!impl_->routing.buses().empty()) {
        result["routing"] = impl_->routing_document;
        result["bus_volumes"] = impl_->routing.volumes();
    }
    return result;
}
bool AudioRuntime::wait_for_sources(std::chrono::milliseconds timeout) {
    return impl_->wait_sources(timeout, false);
}
bool AudioRuntime::wait_for_loading(std::chrono::milliseconds timeout) {
    if (impl_->config.output != AudioOutput::Offline)
        throw std::runtime_error("Blocking audio loading requires explicit offline output");
    return impl_->wait_sources(timeout, true);
}
bool AudioRuntime::Impl::wait_sources(std::chrono::milliseconds timeout, bool loading_only) {
    check();
    if (timeout.count() < 0)
        throw std::runtime_error("Audio preparation timeout must be nonnegative");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        sync(tick, 0, {}, loading_only);
        if (!failures.empty())
            return false;
        if (pending_sources.empty())
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        for (const auto& [id, ticket] : requests) {
            (void)id;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0)
                return false;
            (void)clips.wait(ticket, remaining);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void AudioRuntime::poll_loading() {
    impl_->check();
    impl_->reap_finished();
    if (!impl_->pending_sources.empty() || impl_->clips.statistics().pending)
        impl_->sync(impl_->tick, 0, {}, true);
}
void AudioRuntime::refresh_sources() {
    impl_->check();
    impl_->sync(impl_->tick, 0, {}, true);
}
void AudioRuntime::read_offline(std::span<float> out) {
    impl_->check();
    if (impl_->config.output != AudioOutput::Offline || out.size() % 2)
        throw std::runtime_error("PCM reads require explicit offline stereo output");
    const bool streams = std::ranges::any_of(impl_->voices, [](const auto& row) {
        return bool(row.second->stream) ||
               std::ranges::any_of(row.second->one_shots,
                                   [](const auto& shot) { return bool(shot->stream); });
    });
    // Only the explicit no-device consumer may wait for pages. Native device
    // callbacks use the asynchronous resource manager without this tooling gate.
    auto prebuffer = [&](const Impl::Voice& voice) {
        if (!voice.ready || !voice.stream || impl_->is_paused ||
            (impl_->is_game_paused && voice.authored.pause_with_game) ||
            !ma_sound_is_playing(&voice.sound) || ma_sound_at_end(&voice.sound))
            return;
        const auto info = voice.clip->info();
        const auto remaining = info.frames - std::min(info.frames, voice.stream->cursor());
        const auto required =
            voice.authored.loop && !voice.one_shot
                ? std::uint64_t(asset_detail::audio_stream_sample_rate)
                : std::min<std::uint64_t>(asset_detail::audio_stream_sample_rate, remaining);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (voice.stream->available_frames() < required) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Offline audio stream page preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    for (std::size_t offset = 0; offset < out.size();) {
        if (streams)
            for (const auto& [id, voice] : impl_->voices) {
                (void)id;
                prebuffer(*voice);
                for (const auto& shot : voice->one_shots)
                    prebuffer(*shot);
            }
        // A page covers this quantum at the supported pitch and bounded native
        // Doppler ratio, including the mixer's input-processing cache.
        const auto count =
            streams ? std::min<std::size_t>(512, out.size() - offset) : out.size() - offset;
        checked(ma_engine_read_pcm_frames(&impl_->engine, out.data() + offset, count / 2, nullptr),
                "Offline audio read");
        offset += count;
    }
    impl_->reap_finished();
}
asset_detail::AudioClipInfo AudioPreviewAccess::clip(AudioRuntime& runtime, EntityRef ref) {
    runtime.impl_->check();
    const auto resolved = runtime.impl_->world.resolve(ref);
    const auto found = runtime.impl_->voices.find(resolved.entity);
    if (resolved.state != WorldContext::ResolveState::Available ||
        found == runtime.impl_->voices.end() || !found->second->clip ||
        runtime.impl_->voices.size() != 1 || found->second->authored.spatialized ||
        found->second->authored.pitch != 1 || found->second->authored.bus != 0)
        throw std::runtime_error("Preview transport requires one live nonspatial unpitched source");
    return found->second->clip->info();
}
double AudioPreviewAccess::elapsed(AudioRuntime& runtime, EntityRef ref) {
    (void)clip(runtime, ref);
    const auto& voice = runtime.impl_->voices.at(runtime.impl_->world.resolve(ref).entity);
    if (!voice->ready)
        return 0;
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
    if (!voice.clip->resident()) {
        impl.reprepare_stream(voice, frame, true);
        impl.pending_sources[impl.world.resolve(ref).entity] = voice.authored.clip.id;
        return double(frame) / data.rate;
    }
    // Pinned miniaudio uninit detaches/waits graph readers before returning.
    // Recreate the node to discard resampler/processing caches, retaining PCM/device.
    ma_sound_uninit(&voice.sound);
    voice.ready = false;
    checked(ma_audio_buffer_ref_seek_to_pcm_frame(&voice.buffer, frame), "Preview seek");
    voice.sound = {};
    auto* parent = voice.authored.pause_with_game ? &impl.pausable_group : &impl.group;
    checked(ma_sound_init_from_data_source(&impl.engine, &voice.buffer, 0, parent, &voice.sound),
            "Preview sound reset");
    voice.ready = true;
    ma_sound_set_spatialization_enabled(&voice.sound, MA_FALSE);
    ma_sound_set_looping(&voice.sound, voice.authored.loop);
    impl.apply_voice_gain(voice, impl.config.channel_volumes, impl.routing);
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
