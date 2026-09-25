#include "sdk_play_runtime.hpp"
#include "play_scene_admission.hpp"
#include "runtime_io.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <forge/animation.hpp>
#include <forge/build.hpp>
#include <forge/game_control_queue.hpp>
#include <forge/game_session.hpp>
#include <forge/game_settings.hpp>
#include <forge/identity.hpp>
#include <forge/loading_state.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/physics.hpp>
#include <forge/runtime_resource_service.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene.hpp>
#include <forge/services.hpp>
#include <map>
#include <stdexcept>
#include <thread>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kEffectMapLimit = 256;
constexpr std::size_t kEnvelopeBytesLimit = 8 * 1024 * 1024;
constexpr std::size_t kDiagnosticBytesLimit = 8192;
// Reserve a documented small allowance for fields that may change between
// the projected-budget check and the live serialization (clock.tick,
// platform_effects, release_required, diagnostics). The active world body
// (scene, effective_scene, ui, physics, audio, schema) is byte-identical,
// so only the volatile envelope-level fields can drift. 4 KiB is enough for
// any realistic growth in those fields without rounding into megabytes.
constexpr std::size_t kResponseVolatileAllowance = 4096;
bool is_bounded_string(const Json& value, std::size_t limit) {
    return value.is_string() && value.get<std::string>().size() <= limit;
}
bool is_unsigned_int(const Json& value) { return value.is_number_unsigned(); }
bool is_known_input_device(const std::string& device) {
    return device == "KeyboardMouse" || device == "Gamepad" || device == "EditorHost";
}
Json empty_scene_schema() { return Json{{"version", 1}, {"entities", Json::array()}}; }
Json empty_scene_snapshot() {
    return Json{
        {"version", 1}, {"asset_id", AssetId::generate().str()}, {"entities", Json::array()}};
}
bool session_is_empty(const Json& status_value) {
    const auto state = status_value.value("state", std::string{});
    return state == "empty" || state == "faulted";
}
Json loading_to_json(const LoadingState& state) {
    return Json{{"state", state.state},           {"stage", state.stage},
                {"ticket", state.ticket},         {"superseded_ticket", state.superseded_ticket},
                {"completed", state.completed},   {"total", state.total},
                {"error_code", state.error_code}, {"error", state.error},
                {"can_cancel", state.can_cancel}};
}
Json contract() {
    return Json{{"profile", FORGE_NATIVE_SDK_PROFILE},
                {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                {"source_commit", forge::source_commit},
                {"sdk_project", true},
                {"sdk_play", true}};
}
} // namespace

// Per-token effect state machine.
// Pending  -> the queued command is live; the polled adapter returns false.
// Accepted -> the editor confirmed the effect at the current epoch;
//             the polled adapter returns true once and the record is removed.
// Failed   -> the editor rejected the effect; the polled adapter throws the
//             diagnostic once and the record is removed.
enum class EffectState { Pending, Accepted, Failed };
struct SdkPlayRuntime::EffectOffer {
    std::uint64_t token = 0;
    std::uint64_t sequence = 0;
    std::uint64_t epoch = 0;
    std::string kind; // "cursor" | "navigation"
    Json value;       // desired bool for cursor; direction string for navigation
    EffectState state = EffectState::Pending;
    std::string diagnostic;
};

struct SdkPlayRuntime::State {
    Config config;
    std::vector<EngineModule> modules;
    Json defaults;
    std::shared_ptr<GameControlQueue> queue;
    std::unique_ptr<GameStorage> storage;
    std::unique_ptr<PlaySceneAdmission> admission;
    std::unique_ptr<GameSession> game;
    std::unique_ptr<GameHostControls> host;
    GamePlatformControls platform;
    std::thread::id owner_ = std::this_thread::get_id();

    std::string session;
    bool initialized = false;
    // quit is the loop termination condition. quit_latched is the moment the
    // host observed a gameplay Quit; from that point forward the runtime
    // stops advancing simulation/host callbacks AND the next correlated
    // response must advertise quit_requested=true before the editor pipe
    // closes. Without this latch the editor would see an EOF on its pending
    // request and report a process crash.
    bool quit = false;
    bool quit_latched = false;
    bool tick_failed = false;
    std::uint64_t last_id = 0;
    std::uint64_t active_generation = 0;
    std::uint64_t initial_ticket = 0;
    std::uint64_t last_loading_ticket = 0;
    std::uint64_t effect_sequence = 0;
    std::uint64_t editor_epoch_ = 0;

    // Editor-observed physical cursor state. The host never flips these
    // itself — it only mirrors the editor's verified observation through
    // positive cursor acks at the current epoch.
    bool editor_cursor_captured = false;
    bool editor_cursor_known = false; // False until first positive observation.
    std::uint64_t editor_cursor_epoch = 0;
    std::string editor_input_device = "EditorHost";
    std::string last_initial_diagnostic;
    Json settings_error;
    // Pre-captured status / loading snapshots taken outside the
    // GameSession Mutation guard. The publication validator runs inside
    // poll_candidate which holds the guard, so calling game->status()
    // or game->loading_state() from the validator would reentrantly
    // throw. Refresh these caches at the top of each pump before any
    // host / game preparation runs so the projection always reflects
    // authoritative metadata observed in a safe window.
    Json cached_status_snapshot;
    Json cached_loading_snapshot;
    // Snapshot of the display settings admitted by the constructor.
    // The platform.settings adapter admits a no-op when the incoming
    // display portion equals this baseline, so rolling back to the
    // unchanged baseline does not produce a misleading rejection log.
    // Mode / size / window changes are still rejected.
    Json admitted_display_baseline;

    ui_protocol::CommandGate ui_commands;
    std::map<std::uint64_t, EffectOffer> effects; // token -> offer
    // Root-level obligation: at least one superseded cursor capture is
    // awaiting an actual editor release observation at a NEW epoch. The
    // host keeps emitting this signal until the editor reports an
    // observed release. A field on a hidden/deleted record is useless.
    bool release_required_root = false;

    explicit State(Config c) : config(std::move(c)) {
        if (config.project.empty())
            throw std::runtime_error("sdk-play: Project root is required");
        if (config.user_base.empty() || !config.user_base.is_absolute())
            throw std::runtime_error("sdk-play: User-data base must be an absolute directory");
        if (!config.defaults.is_object() || !config.defaults.contains("application_id"))
            throw std::runtime_error("sdk-play: Project game defaults must include application_id");
        config.runtime.validate();
        if (config.input.source().dump().size() > 1024 * 1024)
            throw std::runtime_error("sdk-play: Input map exceeds 1 MiB");
        modules = std::move(config.modules);
        defaults = config.defaults;
        storage = std::make_unique<GameStorage>(config.user_base,
                                                defaults.at("application_id").get<std::string>());
        queue = std::make_shared<GameControlQueue>();
        session = AssetId::generate().str();
        admission = std::make_unique<PlaySceneAdmission>(session);

        GameSessionConfig session_config;
        session_config.clock = config.runtime;
        session_config.input = config.input;
        session_config.physics = config.physics;
        session_config.audio = config.audio;
        session_config.content_root = config.project;
        session_config.ui = config.ui;
        session_config.modules = modules;
        session_config.controls = queue;
        session_config.preparation = [this](std::uint64_t ticket) {
            return admission->prepare(ticket, [this](RuntimeWorld& world, std::uint64_t t) {
                validate_candidate_publication(world, t);
            });
        };
        game = std::make_unique<GameSession>(std::move(session_config));
        configure_platform();

        Json user_settings = Json::object();
        Json resolved_settings;
        std::string load_error;
        try {
            user_settings = storage->load_settings([&](const Json& overrides) {
                (void)resolve_game_settings(defaults, overrides, config.input);
            });
            resolved_settings = resolve_game_settings(defaults, user_settings, config.input);
        } catch (const std::exception& e) {
            load_error =
                std::string("Saved settings could not be loaded; using defaults. ") + e.what();
            user_settings = Json::object();
            // Fallback MUST go through resolve_game_settings so the result
            // shape (input_map, audio.master_volume) is consistent.
            resolved_settings = resolve_game_settings(defaults, Json::object(), config.input);
        }
        // Apply resolved settings BEFORE the first scene. The methods are
        // safe to call when no world is active; they stage values for
        // the next activation. resolve_game_settings writes the
        // normalized map under "input_map" and the audio preferences
        // under "audio.master_volume".
        if (resolved_settings.contains("input_map") &&
            resolved_settings.at("input_map").is_object())
            game->input_map(InputMap(resolved_settings.at("input_map")));
        if (resolved_settings.contains("audio") && resolved_settings.at("audio").is_object() &&
            resolved_settings.at("audio").contains("master_volume") &&
            resolved_settings.at("audio").at("master_volume").is_number())
            game->master_volume(resolved_settings.at("audio").at("master_volume").get<float>());
        host = std::make_unique<GameHostControls>(*game, queue, *storage, config.project, defaults,
                                                  config.input, user_settings, platform);
        if (!load_error.empty())
            host->diagnostic(load_error);
        // The admitted baseline mirrors the resolved display portion of
        // the constructor settings with vsync removed on both sides —
        // matching GameHostControls::apply_settings which intentionally
        // excludes vsync from the physical window comparison. Without
        // this normalisation a VSync-only change would look like a
        // display change to the rollback path and produce a misleading
        // rejection log when the user later attempts a forbidden mode
        // change.
        if (resolved_settings.contains("display") && resolved_settings.at("display").is_object()) {
            admitted_display_baseline = resolved_settings.at("display");
            admitted_display_baseline.erase("vsync");
        }
        ui_commands.reset(session, 0);
        last_loading_ticket = 0;
        // Seed the cached status / loading snapshots so the very first
        // projection (before any pump) has authoritative metadata to
        // count, rather than a fabricated placeholder.
        cached_status_snapshot = game->status();
        cached_loading_snapshot = loading_to_json(game->loading_state());
    }
    void check_owner() const {
        if (std::this_thread::get_id() != owner_)
            throw std::runtime_error("sdk-play: Mutation must occur on the owner thread");
    }
    void configure_platform() {
        platform.cursor_poll = [this](std::uint64_t token, bool desired) {
            return poll_effect(token, "cursor", Json(desired));
        };
        platform.navigation_poll = [this](std::uint64_t token, const std::string& direction) {
            return poll_effect(token, "navigation", Json(direction));
        };
        // Synchronous platform.cursor(false) only confirms an acknowledged
        // release whose physical observation is already known. It never
        // flips captured true -> false itself; the editor's positive ack
        // does that. Any outstanding release obligation raised by a
        // superseded capture must be cleared by the editor first.
        platform.cursor = [this](bool desired) {
            if (desired)
                throw std::runtime_error(
                    "Synchronous cursor capture is not allowed on the SDK Play host");
            if (release_required_root)
                throw std::runtime_error(
                    "An outstanding cursor release is required before this confirmation");
            if (!editor_cursor_known || editor_cursor_epoch != editor_epoch_ ||
                editor_cursor_captured)
                throw std::runtime_error("Synchronous cursor release requires an acknowledged "
                                         "release at the current epoch");
            for (const auto& [token, offer] : effects)
                if (offer.kind == "cursor" && offer.value.get<bool>() &&
                    offer.state == EffectState::Pending)
                    throw std::runtime_error(
                        "Pending cursor capture must be acknowledged or cancelled before release");
        };
        platform.input_device = [this]() -> std::string { return editor_input_device; };
        platform.settings = [this](const Json& value) {
            // Admit a no-op when the incoming display portion (with
            // vsync excluded) equals the baseline we already accepted at
            // construction. The rollback path replays settings_ which
            // carries the SAME baseline (with vsync excluded on its
            // side), so this avoids a misleading rejection log when
            // nothing physical changed. VSync is intentionally excluded
            // on both sides to mirror GameHostControls' rule.
            if (!value.contains("display") || !value.at("display").is_object())
                return;
            Json physical = value.at("display");
            physical.erase("vsync");
            if (!admitted_display_baseline.is_object() || physical != admitted_display_baseline)
                throw std::runtime_error(
                    "Embedded Game view cannot change the editor window mode or resolution");
        };
        platform.navigation = [](const std::string&) {
            throw std::runtime_error(
                "Synchronous navigation is not supported on the SDK Play host");
        };
    }
    // Direct protocol pause / step and the runtime UI's Pause / Step builtin
    // must not mutate the active world unless the editor has actually
    // reported a physical cursor release at the CURRENT epoch. The earlier
    // path skipped this check and let SDK commands suspend a still-captured
    // gameplay cursor, which leaks physical effects across the protocol.
    // Resume is intentionally exempt: the runtime may resume on request
    // without first proving physical release, mirroring the prior host
    // behaviour. We REUSE the existing synchronous host release path so
    // every guard the source owns (current epoch observed release, no
    // outstanding root release obligation, no pending capture offer, and
    // the owner's pending-capture cleanup) is exercised in the source's
    // own order. The host's release_cursor() calls platform.cursor(false)
    // — the synchronous adapter the source already wires — and that is
    // the contract we honour. We do not invent a second queue or another
    // release check; we just call it once before the clock mutation.
    void perform_released_for_clock_pause() { host->release_cursor(); }
    // Inspect EVERY record against the queue's authoritative state. A
    // record is removed only when the underlying queue token is no longer
    // pending. Terminal records (Accepted / Failed) are preserved while
    // the queued callback is still consuming so the callback can return
    // or throw exactly once. Releasing or revoking a pending capture
    // raises the root release obligation and clears physical observation.
    void prune_dead_effects() {
        std::vector<std::uint64_t> erase;
        for (const auto& [token, offer] : effects) {
            if (queue->pending(token))
                continue;
            // The queue no longer owns the token. The offer must leave the
            // map; if it was a pending or accepted cursor CAPTURE, the
            // physical state may already have happened, so raise the root
            // release obligation and clear the observation.
            if (offer.kind == "cursor" && offer.value.get<bool>()) {
                release_required_root = true;
                editor_cursor_known = false;
            }
            erase.push_back(token);
        }
        for (auto token : erase)
            effects.erase(token);
    }
    // A new cursor RELEASE(false) supersedes older pending CAPTURE(true)
    // offers. Navigation offers and pending releases are not cancelled.
    // Each cancelled capture is marked Failed with a diagnostic; the
    // record stays in the map so the queued callback can throw exactly
    // once on the next poll. Erasing the record would let the same
    // queued callback recreate a fresh offer forever.
    void supersede_older_capture_offers() {
        bool any = false;
        for (auto& [token, offer] : effects) {
            if (offer.kind != "cursor")
                continue;
            if (offer.state != EffectState::Pending)
                continue;
            if (!offer.value.get<bool>())
                continue;
            offer.state = EffectState::Failed;
            offer.diagnostic =
                "Capture superseded by a release request; report an actual release at a new epoch";
            any = true;
        }
        if (any)
            release_required_root = true;
    }
    // Create a new offer only after we know the queue actually owns the
    // token. Returns false if the offer is still pending (no terminal ack).
    bool poll_effect(std::uint64_t token, const std::string& kind, const Json& value) {
        prune_dead_effects();
        auto found = effects.find(token);
        if (found == effects.end()) {
            if (!queue->pending(token))
                return false;
            if (effects.size() >= kEffectMapLimit)
                throw std::runtime_error("SDK Play effect record limit exceeded");
            if (kind == "cursor" && !value.get<bool>())
                supersede_older_capture_offers();
            if (effect_sequence >= std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("SDK Play effect sequence overflow");
            const auto seq = ++effect_sequence;
            EffectOffer offer;
            offer.token = token;
            offer.sequence = seq;
            offer.epoch = editor_epoch_;
            offer.kind = kind;
            offer.value = value;
            offer.state = EffectState::Pending;
            effects.emplace(token, offer);
            return false;
        }
        auto& offer = found->second;
        if (offer.state == EffectState::Failed) {
            // Terminal rejection. Copy the diagnostic, erase, throw once.
            const auto diagnostic = offer.diagnostic;
            effects.erase(found);
            throw std::runtime_error(diagnostic);
        }
        if (offer.state == EffectState::Accepted) {
            // Terminal success. Erase and return true.
            effects.erase(found);
            return true;
        }
        // Tuple mismatch on a still-pending record means the queued
        // callback is asking for a different effect than we recorded.
        // Throw so the caller stops polling forever.
        if (offer.kind != kind || offer.value != value)
            throw std::runtime_error("Polled adapter asked for a different effect than recorded");
        return false; // Still pending.
    }
    void apply_editor_epoch(const Json& value) {
        if (!value.is_object())
            throw std::runtime_error("editor_epoch entry must be a JSON object");
        if (!value.contains("epoch") || !is_unsigned_int(value.at("epoch")))
            throw std::runtime_error("editor_epoch.epoch must be an unsigned integer");
        const auto epoch = value.at("epoch").get<std::uint64_t>();
        if (epoch == 0)
            throw std::runtime_error("editor_epoch must be strictly positive");
        if (epoch <= editor_epoch_)
            throw std::runtime_error(
                "editor_epoch must be strictly greater than the current value");
        if (!value.contains("captured") || !value.at("captured").is_boolean())
            throw std::runtime_error("editor_epoch.captured must be a boolean");
        const auto captured = value.at("captured").get<bool>();
        if (!value.contains("input_device") || !value.at("input_device").is_string())
            throw std::runtime_error("editor_epoch.input_device must be a string");
        const auto input_device = value.at("input_device").get<std::string>();
        if (!is_known_input_device(input_device))
            throw std::runtime_error("editor_epoch.input_device is not in the allowed enum");
        // All fields valid. Apply the explicit observed-state handoff.
        editor_epoch_ = epoch;
        // Only a positive observed release at the new epoch clears the
        // root release_required obligation. A negative observation (i.e.
        // captured == false at a brand-new epoch) does not prove the
        // physical state of any cancelled capture.
        editor_cursor_captured = captured;
        editor_cursor_known = true;
        editor_cursor_epoch = epoch;
        editor_input_device = input_device;
        if (!captured)
            release_required_root = false;
        // Mark every offer recorded under an older epoch as terminal failed.
        // The editor will not reapply them; the next polled adapter call
        // throws the diagnostic.
        for (auto& [token, offer] : effects) {
            if (offer.epoch < epoch && offer.state == EffectState::Pending) {
                offer.state = EffectState::Failed;
                offer.diagnostic = "Editor focus changed before effect was acknowledged";
            }
        }
    }
    void apply_platform_acks(const Json& acks) {
        if (!acks.is_array())
            throw std::runtime_error("platform_acks must be a JSON array");
        // Build a local candidate copy of bounded state. Validate the
        // entire batch against the local snapshot. Publish only after the
        // last entry validates so a malformed later entry cannot
        // partially commit earlier entries.
        std::map<std::uint64_t, EffectOffer> candidate_effects = effects;
        bool candidate_cursor_known = editor_cursor_known;
        bool candidate_cursor_captured = editor_cursor_captured;
        std::uint64_t candidate_cursor_epoch = editor_cursor_epoch;
        for (const auto& ack : acks) {
            if (!ack.is_object())
                throw std::runtime_error("Each platform_acks entry must be a JSON object");
            if (!ack.contains("session") || !ack.at("session").is_string())
                throw std::runtime_error("platform_acks.session must be a string");
            if (ack.at("session").get<std::string>() != session)
                throw std::runtime_error("platform_acks session mismatch");
            if (!ack.contains("token") || !is_unsigned_int(ack.at("token")))
                throw std::runtime_error("platform_acks.token must be unsigned");
            if (!ack.contains("sequence") || !is_unsigned_int(ack.at("sequence")))
                throw std::runtime_error("platform_acks.sequence must be unsigned");
            if (!ack.contains("epoch") || !is_unsigned_int(ack.at("epoch")))
                throw std::runtime_error("platform_acks.epoch must be unsigned");
            if (!ack.contains("accepted") || !ack.at("accepted").is_boolean())
                throw std::runtime_error("platform_acks.accepted must be a boolean");
            const auto token = ack.at("token").get<std::uint64_t>();
            const auto sequence = ack.at("sequence").get<std::uint64_t>();
            const auto epoch = ack.at("epoch").get<std::uint64_t>();
            const auto accepted = ack.at("accepted").get<bool>();
            const auto diagnostic = ack.value("diagnostic", std::string{});
            if (diagnostic.size() > kDiagnosticBytesLimit)
                throw std::runtime_error("platform_acks diagnostic exceeds 8 KiB");
            if (epoch != editor_epoch_)
                throw std::runtime_error("platform_acks epoch does not match current editor epoch");
            auto found = candidate_effects.find(token);
            if (found == candidate_effects.end())
                throw std::runtime_error(
                    "platform_acks references an effect that is no longer live");
            if (found->second.epoch != epoch || found->second.sequence != sequence)
                throw std::runtime_error("platform_acks token/sequence/epoch mismatch");
            auto& offer = found->second;
            // Idempotent identical ack accepted while record is still
            // retained. Contradictory ack rejected. Mirrors the
            // failed-case below: an already-accepted record must not be
            // overwritten with a different diagnostic, and a
            // re-submission that flips accepted while the record is
            // already accepted is rejected wholesale. The editor is
            // expected to drive a single canonical verdict per offer.
            if (offer.state == EffectState::Accepted) {
                if (!accepted || offer.diagnostic != diagnostic)
                    throw std::runtime_error("Contradictory ack for accepted effect");
                continue;
            }
            if (offer.state == EffectState::Failed) {
                if (accepted || offer.diagnostic != diagnostic)
                    throw std::runtime_error("Contradictory ack for failed effect");
                continue;
            }
            if (accepted) {
                offer.state = EffectState::Accepted;
                offer.diagnostic = diagnostic;
                if (offer.kind == "cursor") {
                    candidate_cursor_captured = offer.value.get<bool>();
                    candidate_cursor_known = true;
                    candidate_cursor_epoch = epoch;
                }
            } else {
                offer.state = EffectState::Failed;
                offer.diagnostic = diagnostic;
                if (offer.kind == "cursor")
                    candidate_cursor_known = false;
            }
        }
        // Publish the validated candidate copy.
        effects = std::move(candidate_effects);
        editor_cursor_known = candidate_cursor_known;
        editor_cursor_captured = candidate_cursor_captured;
        editor_cursor_epoch = candidate_cursor_epoch;
    }
    Json live_effects_payload() const {
        Json list = Json::array();
        for (const auto& [token, offer] : effects) {
            if (offer.state != EffectState::Pending)
                continue;
            Json entry{{"token", offer.token},
                       {"sequence", offer.sequence},
                       {"epoch", offer.epoch},
                       {"kind", offer.kind},
                       {"value", offer.value}};
            list.push_back(std::move(entry));
        }
        return list;
    }
    struct WorldBody {
        Json body;
        std::string failure;
        bool failed = false;
    };
    // Build the active-world portion of a snapshot response. Used both
    // by the live `snapshot` command and by the candidate publication
    // validator, so the projected post-activation size is computed with
    // the exact same helpers the wire response will use.
    WorldBody build_world_body(RuntimeWorld& world, std::uint64_t generation,
                               const std::string& snapshot_session,
                               const Json& status_value) const {
        WorldBody out;
        try {
            out.body["scene"] = world.scene.snapshot();
            out.body["effective_scene"] =
                world.simulation.presentation(status_value.at("clock").value("alpha", 1.0));
            out.body["schema"] = world.scene.schema();
            out.body["input"] = world.simulation.input_status();
        } catch (const std::exception& e) {
            out.body["scene"] = nullptr;
            out.body["effective_scene"] = nullptr;
            out.body["schema"] = nullptr;
            out.body["input"] = nullptr;
            out.failure = std::string("Active scene unavailable: ") + e.what();
            out.failed = true;
            return out;
        }
        try {
            if (world.engine.services().available(Capability::Physics)) {
                auto physics =
                    std::static_pointer_cast<PhysicsRuntime>(world.engine.services().physics());
                out.body["physics"] = physics->status();
            } else
                out.body["physics"] = Json{{"state", "disabled"}};
        } catch (const std::exception& e) {
            out.body["physics"] = Json{{"state", "error"}, {"error", e.what()}};
        }
        try {
            if (world.engine.services().available(Capability::Audio)) {
                auto audio =
                    std::static_pointer_cast<AudioRuntime>(world.engine.services().audio());
                out.body["audio"] = audio->status();
            } else
                out.body["audio"] = Json{{"output", "disabled"}};
        } catch (const std::exception& e) {
            out.body["audio"] = Json{{"output", "error"}, {"error", e.what()}};
        }
        try {
            const auto state = status_value.value("state", std::string{});
            if (config.ui && world.engine.services().available(Capability::Ui)) {
                auto ui = std::static_pointer_cast<UiRuntime>(world.engine.services().ui());
                out.body["ui"] = ui->snapshot(
                    world.scene, snapshot_session, generation,
                    status_value.at("clock").value("tick", std::uint64_t{}), state == "paused");
            } else
                out.body["ui"] = nullptr;
        } catch (const std::exception& e) {
            // A UI snapshot failure is a correlated failure that
            // never invents a healthy scene.
            out.body["ui"] = nullptr;
            out.failure = std::string("UI snapshot failed: ") + e.what();
            out.failed = true;
        }
        return out;
    }
    Json projected_active_response(RuntimeWorld& world, std::uint64_t generation) const {
        // Use the candidate world's diagnostics, not the active world's.
        // The candidate world is the one the wire response will publish
        // after activation; counting the active world's diagnostics would
        // let the budget slip through unbounded candidates. Must not call
        // back into game->status() — the validator runs while
        // GameSession::poll_candidate holds the Mutation guard. Reuse
        // the cached metadata captured before entering the host pump.
        Json loading = cached_loading_snapshot;
        loading["ticket"] = generation;
        loading["state"] = "ready";
        loading["stage"] = "ready";
        Json status_value = cached_status_snapshot;
        // Candidate worlds are presented at their initial state: tick=0,
        // alpha=1, paused — exactly the prepared-RuntimeWorld shape the
        // first post-activation snapshot will publish.
        status_value["state"] = "paused";
        if (status_value.contains("clock") && status_value.at("clock").is_object())
            status_value["clock"]["tick"] = 0;
        else
            status_value["clock"] = {{"tick", 0}, {"alpha", 1.0}};
        status_value["clock"]["alpha"] = 1.0;
        Json envelope{{"protocol", 2},
                      {"session", session},
                      {"id", std::uint64_t{0}},
                      {"runtime_contract", contract()},
                      {"loading", loading},
                      {"platform_effects", Json::array()},
                      {"release_required", release_required_root},
                      {"module", ""},
                      {"recovery", nullptr},
                      {"timing", status_value.value("clock", Json::object())}};
        envelope["activation"] = {
            {"state", "ready"}, {"generation", generation}, {"ticket", generation}};
        auto world_body = build_world_body(world, generation, session, status_value);
        for (auto it = world_body.body.begin(); it != world_body.body.end(); ++it)
            envelope[it.key()] = it.value();
        try {
            envelope["diagnostics"] = world.engine.services().diagnostics();
        } catch (...) {
            envelope["diagnostics"] = Json::array();
        }
        envelope["ok"] = !world_body.failed;
        if (world_body.failed)
            envelope["error"] = world_body.failure;
        return envelope;
    }
    // Reject a candidate whose projected post-activation response would
    // not fit the protocol bound, or whose body serialization failed.
    // Throwing here is caught by the PlaySceneAdmission adapter,
    // surfaces as a failed preparation in GameSession, and is recorded
    // as the initial diagnostic. The active world is never touched
    // because activation has not happened.
    void validate_candidate_publication(RuntimeWorld& world, std::uint64_t ticket) const {
        const auto projected = projected_active_response(world, ticket);
        if (projected.value("ok", true) == false)
            throw std::runtime_error("Scene preparation: candidate body serialization failed: " +
                                     projected.value("error", std::string{"unknown"}));
        const auto serialized = projected.dump().size() + 1; // trailing newline
        if (serialized + kResponseVolatileAllowance > RuntimeIo::limit)
            throw std::runtime_error(
                "Scene preparation: projected post-activation response would exceed 16 MiB; "
                "active world retained, candidate rejected");
    }
    Json snapshot_response(std::uint64_t id) const {
        Json response{
            {"protocol", 2}, {"session", session}, {"id", id}, {"runtime_contract", contract()}};
        // Always publish the quit intent so editors can compare snapshots
        // without testing for key existence. After the host observes a
        // gameplay Quit (or an explicit protocol quit) every subsequent
        // response carries quit_requested=true until the process exits.
        response["quit_requested"] = quit || quit_latched;
        const auto status_value = game->status();
        const auto state = status_value.value("state", std::string{});
        const bool empty = session_is_empty(status_value);
        response["loading"] = loading_to_json(game->loading_state());
        response["platform_effects"] = live_effects_payload();
        // Always present so editors can compare snapshots without
        // testing for key existence.
        response["release_required"] = release_required_root;
        response["module"] = "";
        response["recovery"] = nullptr;
        response["timing"] = status_value.value("clock", Json::object());
        response["activation"] = {{"state", empty ? "none" : "ready"},
                                  {"generation", active_generation},
                                  {"ticket", active_generation}};
        response["diagnostics"] = Json::array();
        if (!last_initial_diagnostic.empty())
            response["initial_diagnostic"] = last_initial_diagnostic;
        // The candidate envelope is NEVER inlined in a normal snapshot.
        // Use the dedicated `candidate` command (or
        // pending_candidate() in tests) to fetch the frozen envelope.
        if (auto reference = admission->reference())
            response["candidate"] = *reference;
        else
            response["candidate"] = nullptr;
        bool healthy = true;
        std::string failure;
        if (tick_failed) {
            healthy = false;
            failure =
                settings_error.is_string()
                    ? std::string("Runtime tick faulted: ") + settings_error.get<std::string>()
                    : std::string(
                          "Runtime tick faulted; restore a valid checkpoint or start clean Play");
        }
        if (state == "faulted") {
            healthy = false;
            if (failure.empty())
                failure = status_value.value("error", std::string{"session faulted"});
            else
                failure =
                    failure + "; " + status_value.value("error", std::string{"session faulted"});
        }
        if (empty) {
            // No active world: scene-shaped fields are explicitly null so
            // editors do not have to test for missing keys, and never
            // advertise a fabricated asset_id for a fictional scene.
            response["scene"] = nullptr;
            response["effective_scene"] = nullptr;
            response["schema"] = nullptr;
            response["input"] = nullptr;
            response["physics"] = Json{{"state", "disabled"}};
            response["audio"] = Json{{"output", "disabled"}};
            response["ui"] = nullptr;
        } else {
            auto world_body =
                build_world_body(game->active(), active_generation, session, status_value);
            for (auto it = world_body.body.begin(); it != world_body.body.end(); ++it)
                response[it.key()] = it.value();
            if (world_body.failed) {
                healthy = false;
                if (failure.empty())
                    failure = world_body.failure;
            }
        }
        try {
            auto diagnostics = game->active().engine.services().diagnostics();
            response["diagnostics"] = std::move(diagnostics);
        } catch (...) {
            response["diagnostics"] = Json::array();
        }
        if (healthy) {
            response["ok"] = true;
        } else {
            response["ok"] = false;
            response["error"] = failure;
        }
        return response;
    }
    Json build_error(std::uint64_t id, const std::string& error) const {
        Json response{{"protocol", 2},
                      {"session", session},
                      {"id", id},
                      {"ok", false},
                      {"runtime_contract", contract()},
                      {"error", error}};
        // Always publish quit_requested so an error response that arrives
        // after latched quit still tells the editor to stop polling.
        response["quit_requested"] = quit || quit_latched;
        return response;
    }
    // The wire carries at most one response per request and the pipe is
    // bounded by RuntimeIo::limit. If a fully built response exceeds the
    // bound (active world grew past the protocol budget AFTER activation),
    // downgrade it to a correlated error so the editor sees a precise
    // fault instead of a half-sent oversized payload. Truncation would
    // hide the diagnostic and lie about a healthy scene.
    Json enforce_response_size(Json response) const {
        const auto bytes = response.dump().size() + 1; // trailing newline
        if (bytes <= RuntimeIo::limit)
            return response;
        const auto id = response.value("id", std::uint64_t{});
        return build_error(id, "Active world response exceeds 16 MiB; restore a smaller scene or "
                               "shrink the catalog before retrying");
    }
    // Reflect the host's own gameplay-replacement activation into the
    // SDK Play host's active_generation. Runs after every host pump so
    // the outgoing generation tracks the actual committed world
    // immediately, not on the next request. GameSession tickets are
    // strictly increasing per session, so the activated ticket IS the
    // authoritative generation; monotonic() against a fake counter would
    // double-bump whenever the host pump also bumps the loading state.
    void detect_gameplay_activation() {
        const auto loading = game->loading_state();
        const auto state = loading.state;
        const auto ticket = loading.ticket;
        if (state == "activated" && ticket != 0 && ticket != last_loading_ticket) {
            active_generation = ticket;
            ui_commands.reset(session, active_generation);
            last_loading_ticket = ticket;
        } else if (state == "idle" && ticket == 0) {
            last_loading_ticket = 0;
        }
    }
};
SdkPlayRuntime::SdkPlayRuntime(Config config)
    : state_(std::make_unique<State>(std::move(config))) {}
SdkPlayRuntime::~SdkPlayRuntime() = default;
std::optional<Json> SdkPlayRuntime::pending_candidate() const {
    state_->check_owner();
    return state_->admission->snapshot();
}
Json SdkPlayRuntime::live_effects() const {
    state_->check_owner();
    return state_->live_effects_payload();
}
Json SdkPlayRuntime::status() const {
    state_->check_owner();
    return state_->game->status();
}
Json SdkPlayRuntime::loading() const {
    state_->check_owner();
    return loading_to_json(state_->game->loading_state());
}
std::uint64_t SdkPlayRuntime::editor_epoch() const {
    state_->check_owner();
    return state_->editor_epoch_;
}
void SdkPlayRuntime::pump(RuntimeClock::Time now) {
    auto& s = *state_;
    s.check_owner();
    // Once gameplay Quit is latched, do NOT advance simulation or host
    // callbacks. Refresh cached metadata only so the next correlated
    // response can still answer with the last honest snapshot. The
    // process loop will send that response carrying quit_requested=true
    // and then close the pipe.
    if (s.quit_latched) {
        s.cached_status_snapshot = s.game->status();
        s.cached_loading_snapshot = loading_to_json(s.game->loading_state());
        return;
    }
    // Capture status / loading metadata BEFORE entering any host or
    // game preparation pump, so the publication validator (which runs
    // inside GameSession's Mutation guard) can reuse a read-only copy
    // instead of reentrantly calling into GameSession getters.
    s.cached_status_snapshot = s.game->status();
    s.cached_loading_snapshot = loading_to_json(s.game->loading_state());
    s.prune_dead_effects();
    // First pump drives the host's own gameplay-replacement candidate.
    s.host->pump(now);
    s.detect_gameplay_activation();
    // The host's gameplay Quit path sets its quit_ flag once a queued
    // `quit` operation has been consumed. Latch immediately so the
    // process loop knows to send the next correlated response with
    // quit_requested=true before closing the pipe.
    if (s.host->quit_requested()) {
        s.quit_latched = true;
        // Stop any further tick work; the next pump call will hit the
        // quit_latched early-return and the process loop will answer
        // exactly one more correlated request with the terminal state.
        return;
    }
    if (s.tick_failed)
        return;
    if (s.initial_ticket) {
        // The host pump already polled preparation and may have discarded
        // the candidate on rejection. Calling poll_preparation again with
        // the discarded ticket would throw "Stale or missing prepared
        // scene" and overwrite the real loading.error. Instead, inspect
        // the GameSession status to learn whether the initial candidate is
        // still pending and ready, was discarded (failed/cancelled), or
        // was superseded by a newer request.
        const auto status_value = s.game->status();
        const auto prepared_ticket = status_value.value("prepared_ticket", std::uint64_t{});
        const auto preparation_ready =
            status_value.value("preparation", Json::object()).value("ready", false);
        if (prepared_ticket == s.initial_ticket && preparation_ready) {
            try {
                s.game->activate(s.initial_ticket, now, false);
                // The activated candidate ticket is the authoritative
                // generation. GameSession tickets are strictly increasing,
                // so the value is published verbatim and the bookkeeping
                // counter is updated so subsequent detect_gameplay_activation
                // calls do not double-publish.
                s.active_generation = s.initial_ticket;
                s.last_loading_ticket = s.initial_ticket;
                s.ui_commands.reset(s.session, s.active_generation);
                s.initial_ticket = 0;
                s.last_initial_diagnostic.clear();
            } catch (const std::exception& e) {
                s.last_initial_diagnostic = e.what();
                s.initial_ticket = 0;
            }
        } else if (prepared_ticket != s.initial_ticket) {
            // The initial ticket is no longer pending. Surface whatever
            // the GameSession loading_state actually recorded. A failed
            // preparation carries the validator's diagnostic in
            // loading.error; a cancelled candidate carries loading.state
            // "cancelled" with no error. Superseded candidates leave the
            // loading record pointing at the successor; the marker is
            // simply cleared without disturbing the new candidate.
            const auto loading = s.game->loading_state();
            std::string reason;
            if (loading.ticket == s.initial_ticket && loading.state == "failed" &&
                !loading.error.empty()) {
                reason = loading.error;
            } else if (loading.ticket == s.initial_ticket && loading.state == "cancelled") {
                reason.clear();
            }
            s.last_initial_diagnostic = std::move(reason);
            s.initial_ticket = 0;
        }
        // Same ticket still pending but not yet ready is a no-op: the
        // host acknowledged the candidate this pump and the validator
        // has not finished preparing the envelope. Keep initial_ticket
        // intact so the next pump re-checks preparation readiness.
    }
    s.detect_gameplay_activation();
    // Second pump drives the host's gameplay-replacement candidate after
    // the active world's fixed tick has updated the loading state. The
    // gameplay Quit latch must fire here too: a Quit request that lands
    // in the second pump must not be followed by an advance or a
    // control_frame call.
    if (s.host->quit_requested()) {
        s.quit_latched = true;
        return;
    }
    s.host->pump(now);
    if (s.host->quit_requested()) {
        s.quit_latched = true;
        return;
    }
    s.detect_gameplay_activation();
    if (s.quit_latched) {
        // The second host->pump did not request quit but a previous
        // request did. Skip the tick path entirely.
        return;
    }
    const auto status_value = s.game->status();
    const auto state = status_value.value("state", std::string{});
    if (state == "empty" || state == "faulted")
        return;
    try {
        if (state == "running")
            s.game->advance(now);
        s.game->control_frame();
    } catch (const std::exception& e) {
        s.tick_failed = true;
        s.settings_error = e.what();
    } catch (...) {
        s.tick_failed = true;
        s.settings_error = "Non-standard native exception in fixed tick";
    }
    // World replacement (gameplay-driven activate) can retire a queue
    // token that was pending at the start of this pump. The pre-pump
    // prune ran against the OLD world's queue ownership, so the
    // retirement must be observed BEFORE any subsequent live_effects /
    // snapshot reply can read the effects map. Without this pass a
    // retired-world pending capture would surface in one post-switch
    // reply and the editor would see an effect for a world that no
    // longer exists.
    s.prune_dead_effects();
}
Json SdkPlayRuntime::handle(const Json& request, RuntimeClock::Time now) {
    return state_->enforce_response_size(handle_inner(request, now));
}
Json SdkPlayRuntime::handle_inner(const Json& request, RuntimeClock::Time now) {
    auto& s = *state_;
    s.check_owner();
    if (!request.is_object())
        return s.build_error(0, "Request must be a JSON object");
    // Protocol discriminator: an integer exactly equal to 2. Rejects
    // floats, booleans, strings, and nulls even though some JSON
    // decoders may coerce them.
    if (!request.contains("protocol") || !request.at("protocol").is_number_integer() ||
        request.at("protocol").get<int>() != 2)
        return s.build_error(0, "Unsupported protocol; runtime requires protocol 2");
    if (!request.contains("id") || !is_unsigned_int(request.at("id")))
        return s.build_error(0, "Positive unsigned request ID required");
    const auto id = request.at("id").get<std::uint64_t>();
    if (id == 0)
        return s.build_error(0, "Request ID must be strictly positive");
    if (!request.contains("command") || !request.at("command").is_string())
        return s.build_error(id, "Request command must be a string");
    const auto command = request.at("command").get<std::string>();
    if (command == "hello") {
        if (s.initialized)
            return s.build_error(id, "Session already initialized");
        s.last_id = id;
        s.initialized = true;
        return s.snapshot_response(id);
    }
    if (!s.initialized)
        return s.build_error(id, "Stale or missing runtime session");
    if (!request.contains("session") || !request.at("session").is_string() ||
        request.at("session").get<std::string>() != s.session)
        return s.build_error(id, "Stale or missing runtime session");
    if (id <= s.last_id)
        return s.build_error(id, "Stale or repeated request ID");
    s.last_id = id;
    // Gameplay Quit is terminal: after the latch, no command may mutate
    // the active world. Transport-level identity checks above still apply
    // so a stale or replayed request cannot be hidden behind the latch;
    // below this point we early-return the terminal snapshot for any
    // correlated request, including those carrying input_events,
    // editor_epoch, platform_acks or candidate_ack payloads.
    if (s.quit_latched || s.quit) {
        if (command == "hello")
            return s.build_error(id, "Session already initialized");
        return s.snapshot_response(id);
    }
    if (request.contains("seconds"))
        return s.build_error(id, "Caller delta is unsupported; Step advances one fixed tick");
    // Side-effect fields ride ONLY on snapshot. Rejecting them on other
    // commands prevents stray acknowledgement payloads from mutating
    // platform/editor state during unknown-command handling.
    if ((request.contains("editor_epoch") || request.contains("platform_acks") ||
         request.contains("candidate_ack")) &&
        command != "snapshot")
        return s.build_error(id, "Ack and epoch fields require a snapshot command");
    try {
        // Input events must accompany a snapshot or clock control command,
        // exactly as runtime_main enforces. Empty arrays are rejected.
        if (request.contains("input_events")) {
            if (command != "snapshot" && command != "pause" && command != "resume" &&
                command != "step" && command != "play")
                return s.build_error(id,
                                     "Input events require a snapshot or clock control request");
            const auto& events_value = request.at("input_events");
            if (!events_value.is_array() || events_value.empty())
                return s.build_error(id, "input_events must be a non-empty array");
            if (events_value.dump().size() > 1024 * 1024)
                return s.build_error(id, "input_events payload exceeds 1 MiB");
            std::vector<InputEvent> events = events_value.get<std::vector<InputEvent>>();
            if (events.empty())
                return s.build_error(id, "input_events must contain at least one event");
            const auto state = s.game->status().value("state", std::string{});
            if (state == "empty" || state == "faulted")
                return s.build_error(id, "Input events require an active world");
            try {
                s.game->active().simulation.input().submit(events);
            } catch (const std::exception& e) {
                return s.build_error(id, std::string("input submit failed: ") + e.what());
            }
        }
        if (command == "quit") {
            // Explicit protocol quit. Latch immediately so subsequent
            // mutations are blocked; the response carries
            // quit_requested=true and the next process() iteration
            // flushes the pipe and exits orderly.
            s.quit_latched = true;
            s.quit = true;
            return s.snapshot_response(id);
        }
        if (command == "ping" || command == "snapshot") {
            // Snapshot consumes ack/epoch side-effects inline.
            if (command == "snapshot") {
                if (request.contains("editor_epoch")) {
                    try {
                        s.apply_editor_epoch(request.at("editor_epoch"));
                    } catch (const std::exception& e) {
                        return s.build_error(id, e.what());
                    }
                }
                if (request.contains("platform_acks")) {
                    try {
                        s.apply_platform_acks(request.at("platform_acks"));
                    } catch (const std::exception& e) {
                        return s.build_error(id, e.what());
                    }
                }
                if (request.contains("candidate_ack")) {
                    const auto ack = request.at("candidate_ack");
                    if (!ack.is_object())
                        return s.build_error(id, "candidate_ack must be a JSON object");
                    const auto session_value = ack.value("session", std::string{});
                    if (!is_bounded_string(ack.value("session", Json()), kEnvelopeBytesLimit))
                        return s.build_error(id, "candidate_ack session must be a string");
                    if (session_value != s.session)
                        return s.build_error(id, "candidate_ack session mismatch");
                    if (!is_unsigned_int(ack.value("ticket", Json())))
                        return s.build_error(id, "candidate_ack ticket must be unsigned");
                    if (!ack.contains("accepted") || !ack.at("accepted").is_boolean())
                        return s.build_error(id, "candidate_ack accepted must be boolean");
                    const auto ticket = ack.at("ticket").get<std::uint64_t>();
                    const auto accepted = ack.at("accepted").get<bool>();
                    const auto diagnostic = ack.value("diagnostic", std::string{});
                    if (diagnostic.size() > kDiagnosticBytesLimit)
                        return s.build_error(id, "candidate_ack diagnostic exceeds 8 KiB");
                    if (!s.admission->acknowledge(session_value, ticket, accepted, diagnostic))
                        return s.build_error(id, "candidate_ack was rejected by the channel");
                }
            }
            return s.snapshot_response(id);
        }
        if (command == "schema") {
            Json response = s.snapshot_response(id);
            if (s.game->status().value("state", std::string{}) != "empty" &&
                s.game->status().value("state", std::string{}) != "faulted") {
                try {
                    response["schema"] = s.game->active().scene.schema();
                } catch (const std::exception& e) {
                    return s.build_error(id, e.what());
                }
            } else
                response["schema"] = empty_scene_schema();
            return response;
        }
        if (command == "candidate") {
            // Correlated retrieval of the currently pending frozen
            // candidate envelope. The normal snapshot response carries
            // only a lightweight `{session,ticket,ready}` reference so
            // unbounded candidates cannot poison bounded active
            // snapshots; editors fetch the full envelope via this
            // dedicated command.
            if (!request.contains("ticket") || !is_unsigned_int(request.at("ticket")))
                return s.build_error(id, "candidate request requires a positive ticket");
            const auto ticket = request.at("ticket").get<std::uint64_t>();
            auto envelope = s.admission->fetch_envelope(s.session, ticket);
            if (!envelope)
                return s.build_error(id, "candidate not available for requested ticket");
            Json response{{"protocol", 2},
                          {"session", s.session},
                          {"id", id},
                          {"runtime_contract", contract()},
                          {"candidate", *envelope},
                          {"ok", true}};
            return s.enforce_response_size(std::move(response));
        }
        if (command == "replace") {
            if (!request.contains("scene") || !request.at("scene").is_object())
                return s.build_error(id, "replace requires a JSON scene object");
            if (request.contains("recovery") && !request.at("recovery").is_null())
                return s.build_error(id, "Recovery is not supported on the SDK Play path");
            try {
                const auto scene = request.at("scene");
                if (scene.dump().size() > kEnvelopeBytesLimit)
                    return s.build_error(id, "replace scene exceeds 8 MiB");
                const auto ticket = s.game->prepare(scene);
                s.initial_ticket = ticket;
                return s.snapshot_response(id);
            } catch (const std::exception& e) {
                return s.build_error(id, std::string("replace failed: ") + e.what());
            }
        }
        if (command == "play" || command == "resume") {
            if (s.game->status().value("state", "") == "paused")
                s.game->resume(now);
            return s.snapshot_response(id);
        }
        if (command == "pause") {
            // Require an actual acknowledged release at the current epoch
            // BEFORE pausing gameplay. The host's release_cursor() walks
            // every guard the source owns (current-epoch observed release,
            // no outstanding root obligation, no pending capture offer)
            // and surfaces the honest diagnostic if any guard rejects.
            if (s.game->status().value("state", "") == "running") {
                try {
                    s.perform_released_for_clock_pause();
                } catch (const std::exception& e) {
                    return s.build_error(id, e.what());
                }
                s.game->pause(now);
            }
            return s.snapshot_response(id);
        }
        if (command == "step") {
            // Step also suspends the cursor via the host's authoritative
            // release path. Reject before the clock advance if the editor
            // has not actually released the cursor at the current epoch.
            try {
                s.perform_released_for_clock_pause();
            } catch (const std::exception& e) {
                return s.build_error(id, e.what());
            }
            s.game->step();
            return s.snapshot_response(id);
        }
        if (command == "ui") {
            if (!s.config.ui)
                return s.build_error(id, "Runtime UI is omitted from this composition");
            if (!request.contains("ui_command") || !request.at("ui_command").is_object())
                return s.build_error(id, "ui request requires a ui_command object");
            auto& world = s.game->active();
            auto ui = std::static_pointer_cast<UiRuntime>(world.engine.services().ui());
            // Track whether the builtin control triggered an actual
            // gameplay clock mutation so we can reject the request BEFORE
            // mutating if the editor has not actually released the
            // cursor. Resume remains exempt — the source exempts resume
            // from the synchronous release guard.
            bool mutated = false;
            auto ack = s.ui_commands.dispatch(request.at("ui_command"), [&](const Json& cmd) {
                ui->command(world.scene, cmd, [&](const std::string& control) {
                    if (control == "Pause") {
                        // The host's release path enforces every guard
                        // the source owns. Surface its honest diagnostic
                        // through ui_ack (CommandGate::dispatch catches
                        // and records it on the ack) so the editor sees
                        // the precise reason rather than a generic
                        // mutation failure. State is unchanged on
                        // rejection.
                        s.perform_released_for_clock_pause();
                        s.game->pause(now);
                        mutated = true;
                    } else if (control == "Resume") {
                        if (s.game->status().value("state", "") == "paused") {
                            s.game->resume(now);
                            mutated = true;
                        }
                    } else if (control == "Step") {
                        s.perform_released_for_clock_pause();
                        s.game->step();
                        mutated = true;
                    }
                });
            });
            (void)mutated;
            Json response = s.snapshot_response(id);
            response["ui_ack"] = std::move(ack);
            return response;
        }
        if (command == "refresh_model_assets") {
            if (s.game->status().value("state", "") == "empty")
                return s.build_error(id, "Refresh requires an active scene");
            auto& world = s.game->active();
            auto animation = animation_runtime(world.engine.world());
            if (animation)
                animation->refresh_assets();
            if (world.engine.services().available(Capability::Physics)) {
                auto physics =
                    std::static_pointer_cast<PhysicsRuntime>(world.engine.services().physics());
                if (physics)
                    physics->refresh_assets();
            }
            if (world.engine.services().available(Capability::Resources))
                world.engine.services().resources()->refresh();
            return s.snapshot_response(id);
        }
        if (command == "cancel") {
            // SDK-only exact-ticket cancel. The candidate currently
            // pending in the GameSession is the ONLY ticket the editor is
            // allowed to cancel; anything else must reject with a precise
            // diagnostic so the active world and any later candidate
            // remain untouched. We delegate to GameSession::cancel so the
            // source owns the discard contract; we only filter stale
            // requests here.
            if (!request.contains("ticket") || !is_unsigned_int(request.at("ticket")) ||
                request.at("ticket").get<std::uint64_t>() == 0)
                return s.build_error(id, "cancel requires a positive unsigned ticket");
            const auto ticket = request.at("ticket").get<std::uint64_t>();
            const auto current_ticket = s.game->status().value("prepared_ticket", std::uint64_t{});
            if (ticket != current_ticket || current_ticket == 0)
                return s.build_error(id, "cancel ticket does not match the current prepared scene");
            try {
                s.game->cancel(ticket);
            } catch (const std::exception& e) {
                return s.build_error(id, std::string("cancel failed: ") + e.what());
            }
            // If this was the host-tracked initial ticket, clear the
            // marker WITHOUT recording a stale "Stale or missing
            // prepared scene" diagnostic in last_initial_diagnostic. A
            // user-initiated cancel is an authoritative outcome, not a
            // fault, so the next snapshot's initial_diagnostic stays
            // empty for this candidate (and any successor candidate
            // keeps its own fresh state). The active world and any
            // other pending candidate remain untouched.
            if (s.initial_ticket == ticket) {
                s.initial_ticket = 0;
                s.last_initial_diagnostic.clear();
            }
            return s.snapshot_response(id);
        }
        if (command == "load_module")
            return s.build_error(id, "load_module is not supported on the SDK Play path");
        return s.build_error(id, "Unknown command");
    } catch (const std::exception& e) {
        return s.build_error(id, e.what());
    }
}
void SdkPlayRuntime::process() {
    auto& s = *state_;
    s.check_owner();
    RuntimeIo io;
    while (!io.closed()) {
        const auto now = RuntimeClock::Clock::now();
        // Advance the simulation/host callbacks. If gameplay Quit was
        // observed, pump() now early-returns after setting quit_latched;
        // we still need one more correlated response sent to the editor
        // so the editor doesn't interpret EOF as a process crash.
        pump(now);
        // The wire carries at most one in-flight response. Flush before
        // pulling a new request and bound serialized size including the
        // trailing newline against RuntimeIo::limit, not a local magic.
        io.flush();
        if (s.quit && !io.pending())
            break;
        // The wire carries at most one in-flight response. If the previous
        // response has not been fully drained yet, wait for it before
        // pulling a new request.
        if (io.pending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        std::string line;
        if (!io.receive(line)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        Json request;
        try {
            request = Json::parse(line);
        } catch (const std::exception& e) {
            // Only an unparseable line carries id=0. The handler will
            // produce a correlated error response for any other parse
            // failure inside an object payload.
            if (!io.pending()) {
                Json bounded{{"protocol", 2},
                             {"session", s.session},
                             {"id", std::uint64_t{0}},
                             {"ok", false},
                             {"error", std::string("Request parse failed: ") + e.what()},
                             {"quit_requested", s.quit || s.quit_latched}};
                const auto serialized = bounded.dump() + "\n";
                if (serialized.size() <= RuntimeIo::limit)
                    io.send(serialized);
            }
            continue;
        }
        try {
            const auto response = handle(request, now);
            const auto serialized = response.dump() + "\n";
            if (serialized.size() > RuntimeIo::limit) {
                Json bounded{{"protocol", 2},
                             {"session", s.session},
                             {"id", response.value("id", std::uint64_t{})},
                             {"ok", false},
                             {"error", "Runtime response exceeds 16 MiB"},
                             {"quit_requested", s.quit || s.quit_latched}};
                const auto cut = bounded.dump() + "\n";
                if (cut.size() <= RuntimeIo::limit)
                    io.send(cut);
            } else
                io.send(serialized);
        } catch (const std::exception& e) {
            // A serious native fault inside handle() reached this scope.
            // The candidate is rejected (initial failure does not abort
            // the world); record the diagnostic and request a controlled
            // shutdown so the editor can disconnect cleanly.
            s.tick_failed = true;
            s.settings_error = e.what();
            s.quit = true;
        }
        io.flush();
        // If a gameplay Quit was observed during the pump above, the
        // response we just sent already carries quit_requested=true.
        // Now is the moment to set s.quit and exit orderly on the next
        // loop check; do NOT break before the response is sent.
        if (s.quit_latched) {
            s.quit = true;
        }
        if (s.quit && !io.pending())
            break;
    }
}
} // namespace forge
