#pragma once
#include "play_transport_worker.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <forge/build.hpp>
#include <forge/game_platform.hpp>
#include <forge/input.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace forge {
// One correlated request in flight. Transport pumps never determine simulation dt.
// Pending activation retains only artifact/checkpoint, never a second loaded DLL.
class PlaySession {
  public:
    ~PlaySession() { stop(); }
    PlaySession() = default;
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    enum class Reload { Idle, Pending, Succeeded, Failed, Cancelled };
    // Mirror the runtime-side 8 KiB diagnostic cap so the editor can
    // reject oversized entries locally before they reach the wire.
    static constexpr std::size_t kSdkPlatformAcksDiagnosticBytes = 8 * 1024;
    bool active() const { return process_ != nullptr; }
    // The transport-published live-active flag for SDK Play (separate
    // from the legacy stage machine). True iff the runtime has actually
    // promoted a candidate world and we hold a matching snapshot. False
    // when activation is "none" or when scene-shaped fields are null —
    // legacy ready() does not detect this and would otherwise mislead a
    // presentation adapter into thinking a healthy world exists.
    bool ready() const {
        if (!active() || stage_ != Stage::Running)
            return false;
        if (sdk_game_)
            return sdk_activation_active_ && !snapshot_.is_null();
        return true;
    }
    bool paused() const { return timing_.value("paused", true); }
    void model_assets_changed() {
        if (active())
            model_assets_changed_ = true;
    }
    bool pending_activation() const { return ready() && transaction_; }
    bool awaiting_activation_input() const {
        return pending_activation() && desired_paused_ && paused();
    }
    bool control_ready() const {
        return ready() && control_.empty() && (!waiting_ || sent_command_ == "snapshot");
    }
    const Json& timing() const { return timing_; }
    Json physics_debug_selection;
    const Json& physics_status() const { return physics_status_; }
    const Json& input_status() const { return input_status_; }
    const Json& ui_snapshot() const { return ui_snapshot_; }
    const Json& ui_ack() const { return ui_ack_; }
    bool submit_ui(const Json& command) {
        if (!ready() || !ui_command_.is_null() || transaction_)
            return false;
        if (command.value("session", "") != session_ || ui_snapshot_.is_null() ||
            command.at("generation") != ui_snapshot_.at("generation"))
            return false;
        ui_command_ = command;
        return true;
    }
    void configure(double hz, InputMap map, Double3 gravity = {0, -9.81, 0},
                   std::filesystem::path project = {}, bool exact_sdk = false,
                   bool sdk_game = false) {
        if (active())
            throw std::runtime_error("Stop Play before configuring input/settings");
        if (!std::isfinite(hz) || hz < 1 || hz > 240)
            throw std::runtime_error("Invalid simulation frequency");
        gravity_ = gravity;
        audio_project_ = path_utf8(project);
        if (exact_sdk && project.empty())
            throw std::runtime_error("Exact SDK Play requires a project root");
        exact_sdk_ = exact_sdk;
        // The SDK Play opt-in profile is a strict superset of exact_sdk.
        // Only enabled when exact_sdk requested it AND a project root exists;
        // the calling site decides policy. No file reads or detection here.
        sdk_game_ = exact_sdk && sdk_game;
        if (sdk_game_ && project.empty())
            throw std::runtime_error("SDK Play requires a project root");
        simulation_hz_ = hz;
        input_map_ = map.source();
    }
    void input_event(InputEvent event) {
        if (!ready())
            return;
        if (input_events_.size() >= 4096) {
            input_events_.clear();
            input_events_.push_back({{}, 0, true});
            notice_ = "Input queue overflow: controls released. ";
            return;
        }
        input_events_.push_back(std::move(event));
    }
    // Transient user-data override. Tests and acceptance harnesses
    // need an isolated writable base so they do not touch the real
    // user's saves. Production callers leave the default (empty)
    // and the runtime receives --user-data game_user_data_base().
    // Guarded while inactive so the next launch picks it up without
    // racing a live SDL_CreateProcess. No persistence; the value
    // resets on stop() through launch bookkeeping.
    void set_user_data_override(const std::filesystem::path& path) {
        if (active())
            throw std::runtime_error("Stop Play before changing user-data override");
        user_data_override_ = path_utf8(path);
    }
    // Explicit offline audio selection for hardware-free acceptance.
    // Distinct from `probe_`: probe_ disables UI for transport-only tests
    // and ALSO selects offline audio; this flag selects offline audio
    // alone while leaving the UI capability untouched. Used only by the
    // CI SDK play fixture where the runtime is launched with `--audio
    // offline` to avoid opening a physical WASAPI device; production
    // callers leave it false and the runtime receives `--audio device`.
    // Guarded while inactive so the next launch picks it up without
    // racing a live SDL_CreateProcess. No persistence.
    void set_headless_audio(bool value) {
        if (active())
            throw std::runtime_error("Stop Play before changing headless audio mode");
        headless_audio_ = value;
    }
    bool headless_audio() const { return headless_audio_; }
    const std::string& user_data_override() const { return user_data_override_; }
    const std::string& session() const { return session_; }
    const std::string& module() const { return module_; } // Last known-good artifact only.
    Reload reload_result() const { return reload_result_; }
    bool can_recover() const { return !active() && recoverable_; }
    void pause() {
        if (control_ready())
            control_ = "pause";
    }
    void resume() {
        if (control_ready())
            control_ = "resume";
    }
    void step() {
        if (control_ready() && paused())
            control_ = "step";
    }
    void recover() {
        if (!can_recover())
            return;
        loading_ = module_;
        transaction_ = false;
        desired_paused_ = paused();
        restoring_ = true;
        launch(snapshot_, recovery_);
    }
    // Caller has executed this immutable artifact in a separate fixed-tick probe.
    void reload(const std::string& path) {
        if (exact_sdk_)
            throw std::runtime_error("Exact SDK registrations require Stop, rebuild, then Play");
        if (!ready() || recovery_.is_null())
            throw std::runtime_error(
                "Play is not ready for reload. Wait for asset loading or resolve Console errors.");
        if (transaction_) {
            // No candidate tick completed: discard its world before superseding.
            // Keep the original known-good artifact/checkpoint and prior run policy.
            loading_ = path;
            notice_ = "Previous pending activation cancelled. ";
            launch(checkpoint_, checkpoint_recovery_);
        } else {
            requested_ = path;
        }
        reload_result_ = Reload::Pending;
    }
    Double3 gravity() const { return gravity_; }
    const Json& recovery() const { return recovery_; }
    const Json& snapshot() const { return snapshot_; }
    const Json& effective_snapshot() const { return effective_; }
    std::uint64_t snapshot_version() const { return snapshot_version_; }
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    const Json& diagnostics() const { return diagnostics_; }
    // ---------------------------------------------------------------------
    // SDK Play opt-in profile accessors. All return transport layer
    // observations from the most recent wire response, never a fabricated
    // scene. Old authored source is untouched; if no live active world
    // exists yet (Pre-activation in Stage::Preparing or a transport
    // empty state) these return the documented default values below.
    // ---------------------------------------------------------------------
    bool sdk_play() const { return sdk_game_; }
    // The currently pending activation generation observed on the runtime
    // snapshot. 0 until the runtime advertises a non-empty world.
    std::uint64_t sdk_activation_generation() const { return sdk_activation_generation_; }
    // True while the runtime is publishing a live activation (state in
    // {ready, active}) with a positive generation. Independent from the
    // legacy stage machine; narrow const accessor for the staged
    // presentation adapter (PlayPresentation) which must distinguish
    // "candidate prepared but not yet live" from "candidate accepted".
    bool sdk_activation_active() const { return sdk_activation_active_; }
    // The loading state JSON (state, stage, ticket, superseded_ticket,
    // completed, total, error_code, error, can_cancel) from the most
    // recent snapshot. Null when the runtime has no loading record.
    const Json& sdk_loading() const { return sdk_loading_; }
    // Live platform effects the editor adapter must reflect (cursor /
    // navigation offers). May be empty when nothing is pending.
    const Json& sdk_platform_effects() const { return sdk_platform_effects_; }
    // True when the runtime needs the editor to acknowledge release of a
    // previously captured resource (e.g. cursor / input device).
    bool sdk_release_required() const { return sdk_release_required_; }
    // The current live editor epoch the runtime has acknowledged as
    // the next-bound observation. 0 means no observation has been
    // sent yet (or stop() reset the transport state).
    std::uint64_t editor_epoch_observed() const { return sdk_editor_epoch_seen_; }
    // The epoch that will be visible on the next snapshot response:
    // max(highest already shipped, any value still queued). The
    // adapter / main callers consult this to avoid executing offered
    // effects whose (token, sequence, epoch) triple no longer matches
    // the observation in flight — without this, a stale offer could
    // be acked and immediately invalidated by the next observation.
    std::uint64_t current_effective_epoch() const {
        const auto queued = sdk_editor_epoch_.is_object()
                                ? sdk_editor_epoch_.value("epoch", std::uint64_t{})
                                : std::uint64_t{};
        return std::max(sdk_editor_epoch_seen_, queued);
    }
    // Allocate the next positive editor epoch value the editor should
    // submit. The result is strictly greater than BOTH the highest
    // already-shipped observation AND any epoch still queued in
    // sdk_editor_epoch_, so a follow-on submit_sdk_editor_epoch cannot
    // silently roll back a previous submission. Returns 0 when
    // overflow would make a fresh positive value impossible — the
    // caller must reset transport state (Stop / restart) in that case.
    std::uint64_t next_editor_epoch() const {
        const auto base = current_effective_epoch();
        if (base == std::numeric_limits<std::uint64_t>::max())
            return 0;
        return base + 1u;
    }
    // Build + submit a fresh editor_epoch observation. The transport
    // validator still enforces strict positivity and known input
    // device, so a malformed `captured` / `input_device` still
    // rejects. Centralized here so every consumer (initial per-session
    // handoff, SDK Escape, F6/F7, root release_required, focus loss,
    // external revocation) sees the same allocation logic and the
    // same overflow handling. Returns true only when the observation
    // is queued for the next snapshot request. `captured` reflects
    // the OBSERVED SDL_GetWindowRelativeMouseMode flag (see
    // SdlGameCursor). Disagreement (setter reports success but
    // getter still reads
    // on) is injected robustness in the fixture, not normal pinned
    // API behaviour.
    bool submit_editor_observation(bool captured, const std::string& input_device) {
        const auto epoch = next_editor_epoch();
        if (epoch == 0)
            return false;
        Json envelope{{"epoch", epoch}, {"captured", captured}, {"input_device", input_device}};
        return submit_sdk_editor_epoch(std::move(envelope));
    }
    // Last fetched candidate ticket for which we hold a frozen envelope.
    // 0 when we have not yet fetched any envelope or after commit / clear.
    std::uint64_t sdk_candidate_ticket() const { return sdk_candidate_fetched_ticket_; }
    // The frozen candidate envelope {session,ticket,scene,ui,...} copied
    // for the presentation adapter. Null when no envelope is currently
    // held (Pre-fetch, after commit, after supersession/reject).
    const Json& sdk_candidate_envelope() const { return sdk_candidate_envelope_; }
    // ---------------------------------------------------------------------
    // SDK Play snapshot-side submission. Returns false (and discards)
    // when not active, when the opt-in profile is disabled, when payload
    // validation fails, or when transport is not in the right state.
    // These NEVER fabricate positive acknowledgement; the editor must
    // decide. They ride ONLY on a subsequent `snapshot` request (the
    // runtime rejects ack/epoch fields on any other command); pending
    // entries are cleared on every stop, restart, candidate supersession
    // and on send.
    // ---------------------------------------------------------------------
    // Acknowledge a runtime candidate (accepted or rejected) for the
    // currently pending frozen envelope. Strict acceptance rules:
    //  * `session` must match the live runtime session AND the envelope's
    //    session field.
    //  * `ticket` must equal the currently-held frozen envelope ticket
    //    (sdk_candidate_ticket()). Foreign, stale, missing or already-
    //    terminal verdicts are rejected (returns false). A rejected
    //    verdict is also final and cannot be re-queued; the editor must
    //    await the next superseding candidate.
    //  * Idempotent identical resubmission of the same still-pending
    //    verdict returns true without flipping queue state, so the
    //    caller can retry safely.
    //  * `diagnostic` is bounded to 8 KiB by the runtime; the editor
    //    should keep it short.
    bool submit_sdk_candidate_ack(bool accepted, const std::string& session, std::uint64_t ticket,
                                  std::string diagnostic = std::string()) {
        if (!sdk_game_ || !active() || session_ != session || ticket == 0)
            return false;
        if (diagnostic.size() > 8 * 1024)
            return false;
        // Require a currently-fetched envelope that matches.
        if (sdk_candidate_envelope_.is_null() || !sdk_candidate_envelope_.is_object())
            return false;
        if (sdk_candidate_fetched_ticket_ != ticket)
            return false;
        const auto envelope_session = sdk_candidate_envelope_.value("session", std::string{});
        if (envelope_session != session)
            return false;
        // Build candidate verdict and compare against the authoritative
        // stored record: either the not-yet-shipped pending buffer
        // (sdk_candidate_ack_) or the already-shipped terminal ledger
        // (sdk_ack_terminal_value_). The ledger wins once the verdict
        // has been written to the wire; resubmissions are idempotent
        // iff identical to that ledger.
        Json tentative{{"session", session}, {"ticket", ticket}, {"accepted", accepted}};
        if (!diagnostic.empty())
            tentative["diagnostic"] = diagnostic;
        auto identical = [&](const Json& prior) {
            return prior.is_object() &&
                   prior.value("session", std::string{}) ==
                       tentative.value("session", std::string{}) &&
                   prior.value("ticket", std::uint64_t{}) ==
                       tentative.value("ticket", std::uint64_t{}) &&
                   prior.value("accepted", false) == tentative.value("accepted", false) &&
                   prior.value("diagnostic", std::string{}) ==
                       tentative.value("diagnostic", std::string{});
        };
        // Already shipped terminal record: idempotent identical OK,
        // anything else rejected.
        if (sdk_ack_terminal_ticket_ == ticket && sdk_ack_terminal_value_.is_object())
            return identical(sdk_ack_terminal_value_);
        // Pending but unshipped: identical resubmission OK, contradiction
        // rejected (the editor must cancel and requeue with the new
        // payload, not silently overwrite).
        if (sdk_pending_candidate_ack_)
            return identical(sdk_candidate_ack_);
        sdk_candidate_ack_ = std::move(tentative);
        sdk_pending_candidate_ack_ = true;
        // Only a positive prepared verdict becomes the authoritative
        // expected commitment ticket. A negative verdict (rejection)
        // intentionally does NOT latch sdk_expected_ticket_ — the
        // runtime will dispose of that record and we should not commit
        // against a ticket we just refused.
        if (accepted)
            sdk_expected_ticket_ = ticket;
        return true;
    }
    // SDK-only exact-ticket loading cancel. One pending cancel is
    // bound to the live `loading` observation's `ticket` and rides on
    // the next correlated request. Stale or foreign tickets are
    // rejected and the live world is preserved. Returns true only
    // when the cancel was queued for the next snapshot request.
    //
    // Cancellation takes priority over an unsent positive verdict
    // for the SAME ticket: the queued candidate ack is invalidated so
    // the cancel is the next message the runtime sees. The user
    // pressed Cancel; honour that intent instead of racing the
    // candidate ack past the runtime's activation gate.
    bool submit_sdk_cancel_loading(std::uint64_t ticket) {
        if (!sdk_game_ || !active() || ticket == 0)
            return false;
        if (sdk_loading_.is_null() || !sdk_loading_.is_object())
            return false;
        const auto offered = sdk_loading_.value("ticket", std::uint64_t{});
        if (offered != ticket)
            return false;
        if (!sdk_loading_.value("can_cancel", false))
            return false;
        if (sdk_pending_cancel_loading_ticket_ != 0 && sdk_pending_cancel_loading_ticket_ != ticket)
            return false;
        // Cancel wins over an unsent matching positive verdict: drop
        // the pending ack so the cancel is the next correlated
        // message (otherwise the snapshot would ship the positive ack
        // first, the runtime would activate, and the cancel would
        // arrive too late and tear down a healthy world).
        if (sdk_pending_candidate_ack_ && sdk_candidate_ack_.is_object() &&
            sdk_candidate_ack_.value("ticket", std::uint64_t{}) == ticket &&
            sdk_candidate_ack_.value("accepted", false)) {
            sdk_pending_candidate_ack_ = false;
            sdk_candidate_ack_ = nullptr;
            sdk_expected_ticket_ = 0;
        }
        sdk_pending_cancel_loading_ticket_ = ticket;
        return true;
    }
    // Public observation of the exact loading ticket that the next
    // correlated request will cancel, or 0 when no cancel is pending.
    std::uint64_t sdk_pending_cancel_loading_ticket() const {
        return sdk_pending_cancel_loading_ticket_;
    }
    // Acknowledge a batch of platform effects. Bounded (max 64 entries)
    // and merged by exact `(token, sequence, epoch)` triple so a fresh
    // batch never obliterates an unsent one — overlapping entries
    // replace prior state and new tokens are appended. Returns false
    // only on shape / active / opt-in violations.
    //
    // Validation is performed against a temporary bounded candidate
    // copy. The entire batch — diagnostic ≤ 8 KiB, current session,
    // actually-offered token/sequence/epoch, and exact-live current
    // editor epoch — must validate before we mutate the prior pending
    // queue. Identical (token, sequence, epoch, accepted, diagnostic)
    // entries merge idempotently; contradictions or overflow are
    // rejected WITHOUT mutating sdk_platform_acks_. We never silently
    // truncate successful submissions.
    bool submit_sdk_platform_acks(Json acks) {
        if (!sdk_game_ || !active() || !acks.is_array() || acks.empty() || acks.size() > 64)
            return false;
        // Validate every entry up front. Diagnostic bound + session +
        // shape, plus the live current editor epoch (any pending
        // platform_acks is published only on the next snapshot with the
        // editor_epoch_ observation attached; we therefore require the
        // submitted batch to match that observation's epoch, otherwise
        // the wire would reject it).
        const auto live_epoch = sdk_editor_epoch_.is_object()
                                    ? sdk_editor_epoch_.value("epoch", std::uint64_t{})
                                    : sdk_editor_epoch_seen_;
        for (const auto& a : acks) {
            if (!a.is_object())
                return false;
            if (!a.contains("session") || !a.at("session").is_string())
                return false;
            if (!a.contains("token") || !a.at("token").is_number_unsigned())
                return false;
            if (!a.contains("sequence") || !a.at("sequence").is_number_unsigned())
                return false;
            if (!a.contains("epoch") || !a.at("epoch").is_number_unsigned())
                return false;
            if (!a.contains("accepted") || !a.at("accepted").is_boolean())
                return false;
            if (a.at("session").get<std::string>() != session_)
                return false;
            if (a.value("diagnostic", std::string{}).size() > kSdkPlatformAcksDiagnosticBytes)
                return false;
            if (a.at("epoch").get<std::uint64_t>() != live_epoch)
                return false;
        }
        // Build a bounded candidate copy. Validate it locally before
        // mutating sdk_platform_acks_: identical entries merge
        // idempotently; contradictory entries (same token + new
        // accepted/diagnostic) reject the entire batch; overflow
        // (>64) also rejects the entire batch — never silently
        // truncate.
        Json candidate = sdk_pending_platform_acks_ && sdk_platform_acks_.is_array()
                             ? Json(sdk_platform_acks_)
                             : Json::array();
        for (auto& a : acks) {
            std::size_t existing = candidate.size();
            for (std::size_t i = 0; i < candidate.size(); ++i) {
                if (candidate[i].value("token", std::uint64_t{}) ==
                        a.at("token").get<std::uint64_t>() &&
                    candidate[i].value("sequence", std::uint64_t{}) ==
                        a.at("sequence").get<std::uint64_t>() &&
                    candidate[i].value("epoch", std::uint64_t{}) ==
                        a.at("epoch").get<std::uint64_t>()) {
                    existing = i;
                    break;
                }
            }
            if (existing < candidate.size()) {
                // Idempotent merge: identical (accepted + diagnostic)
                // is silently accepted; anything else rejects the
                // whole batch (we do not silently overwrite prior
                // pending verdict state).
                const auto& prior = candidate[existing];
                if (prior.value("accepted", false) != a.at("accepted").get<bool>() ||
                    prior.value("diagnostic", std::string{}) !=
                        a.value("diagnostic", std::string{}))
                    return false;
            } else {
                candidate.push_back(std::move(a));
                if (candidate.size() > 64)
                    return false;
            }
        }
        // Validate against currently-known offered effects. Effects are
        // keyed by (token, sequence, epoch) so a stale ack that no
        // longer matches anything live is rejected wholesale.
        if (sdk_offered_effects_) {
            for (const auto& entry : candidate) {
                const auto token = entry.at("token").get<std::uint64_t>();
                const auto sequence = entry.at("sequence").get<std::uint64_t>();
                const auto epoch = entry.at("epoch").get<std::uint64_t>();
                auto found = sdk_offered_effects_->find({token, sequence, epoch});
                if (found == sdk_offered_effects_->end())
                    return false;
            }
        }
        sdk_platform_acks_ = std::move(candidate);
        sdk_pending_platform_acks_ = true;
        return true;
    }
    // Observe a strict-positive editor epoch handoff. The runtime
    // rejects epochs that are not strictly greater than the prior
    // observation, are zero, or carry an unknown input device; the
    // transport shape-checks here so a malformed submission is caught
    // locally before the next snapshot request, and the supplied epoch
    // is recorded (in flight) so a follow-on platform_acks is scoped
    // against the same epoch token.
    //
    // Runtime EXACT allowed enum = KeyboardMouse, Gamepad, EditorHost
    // (validated by the SDK runtime; do not invent Touch/Virtual/Mixed).
    // Epoch must exceed BOTH the highest epoch already shipped
    // (sdk_editor_epoch_seen_) AND any epoch currently sitting in the
    // unsent queue (sdk_editor_epoch_) so we never silently roll back a
    // queued observation.
    //
    // A new epoch invalidates any unsent acknowledgements from an older
    // epoch — those are now physically inapplicable and must be
    // reported as such rather than silently dropped. The runtime tracks
    // offers via their (token, sequence, epoch) triple and will reject
    // any ack carrying an epoch that no longer matches the current
    // editor observation, so discarding them here keeps editor and
    // runtime honest.
    bool submit_sdk_editor_epoch(Json epoch) {
        if (!sdk_game_ || !active() || !epoch.is_object())
            return false;
        if (!epoch.contains("epoch") || !epoch.at("epoch").is_number_unsigned())
            return false;
        const auto new_epoch = epoch.at("epoch").get<std::uint64_t>();
        if (new_epoch == 0)
            return false;
        if (sdk_editor_epoch_seen_ != 0 && new_epoch <= sdk_editor_epoch_seen_)
            return false;
        // Also reject an epoch that does not exceed a previously queued
        // but unsent observation — silently overwriting the queued value
        // would roll back the editor's own observation history.
        if (sdk_editor_epoch_.is_object() &&
            new_epoch <= sdk_editor_epoch_.value("epoch", std::uint64_t{}))
            return false;
        if (!epoch.contains("captured") || !epoch.at("captured").is_boolean())
            return false;
        if (!epoch.contains("input_device") || !epoch.at("input_device").is_string())
            return false;
        static const std::unordered_set<std::string> known = {"KeyboardMouse", "Gamepad",
                                                              "EditorHost"};
        if (!known.count(epoch.at("input_device").get<std::string>()))
            return false;
        // A new epoch supersedes any pending platform_acks whose epoch
        // no longer matches the upcoming observation. The editor cannot
        // truthfully confirm or deny those effects now; the actual
        // verdict will be reported on the next observation. Drop them so
        // the runtime never receives a stale ack that it would
        // otherwise reject wholesale on the wire.
        if (sdk_pending_platform_acks_ && sdk_platform_acks_.is_array()) {
            Json fresh = Json::array();
            for (auto& entry : sdk_platform_acks_) {
                const auto e = entry.value("epoch", std::uint64_t{});
                if (e == new_epoch)
                    fresh.push_back(std::move(entry));
            }
            sdk_platform_acks_ = std::move(fresh);
            sdk_pending_platform_acks_ =
                !sdk_platform_acks_.is_array() || !sdk_platform_acks_.empty();
            if (!sdk_pending_platform_acks_)
                sdk_platform_acks_ = nullptr;
        }
        sdk_editor_epoch_ = std::move(epoch);
        sdk_pending_editor_epoch_ = true;
        return true;
    }
    void stop() {
        close_process();
        if (transaction_ || !requested_.empty())
            reload_result_ = Reload::Cancelled;
        transaction_ = false;
        requested_.clear();
        recoverable_ = false;
        status_ = "Stopped. Authored scene preserved.";
        // SDK Play transport state is reset by close_process(); stop()
        // additionally clears any pending snapshot-side acks/epochs.
        sdk_pending_candidate_ack_ = false;
        sdk_candidate_ack_ = nullptr;
        sdk_pending_platform_acks_ = false;
        sdk_platform_acks_ = nullptr;
        sdk_pending_editor_epoch_ = false;
        sdk_editor_epoch_ = nullptr;
        sdk_pending_cancel_loading_ticket_ = 0;
    }
    void start(const std::string& executable, const Json& scene, const std::string& module = {},
               bool probe = false, const Json& recovery = Json()) {
        if (exact_sdk_ && (!module.empty() || !recovery.is_null()))
            throw std::runtime_error("Exact SDK Play starts from authored state; ABI1 reload and "
                                     "partial custom-state recovery are unavailable");
        const auto initial = scene;
        stop();
        executable_ = executable;
        loading_ = module;
        module_.clear();
        previous_.clear();
        checkpoint_ = initial;
        checkpoint_recovery_ = recovery;
        probe_ = probe;
        desired_paused_ = probe;
        prior_paused_ = probe;
        transaction_ = !module.empty();
        restoring_ = false;
        reload_result_ = Reload::Idle;
        notice_.clear();
        launch(initial, recovery);
    }
    void pump() {
        if (!process_)
            return;
        const auto diag_now = SDL_GetTicks();
        const auto diag_gap = last_diag_ticks_ ? (diag_now - last_diag_ticks_) : Uint64{0};
        last_diag_ticks_ = diag_now;
        const bool diag = sdk_diag_open();
        try {
            // Drain stderr first (cheap, bounded). Worker owns the
            // SDL_IOStream*; main thread only ever sees the bytes.
            std::string stderr_chunk;
            const auto stderr_bytes = worker_.drain_stderr(stderr_chunk);
            if (stderr_bytes > 0) {
                log_.append(stderr_chunk);
                if (log_.size() > 65536)
                    log_.erase(0, log_.size() - 65536);
            }
            // Drain at most one complete receipt (matching the
            // original at-most-one-response-per-pump semantics).
            // Each receipt carries the worker's monotonic
            // `received_at_ms` (the moment the NEWLINE was read).
            // We drain BEFORE checking the worker failure so a clean
            // final Quit response is not discarded by a process-exit
            // failure that arrived in the same loop pass.
            PlayTransportWorker::Receipt receipt;
            const bool have_receipt = worker_.drain_one_line(receipt);
            if (have_receipt) {
                const Json response = Json::parse(receipt.payload);
                if (!waiting_ || response.value("protocol", 0) != 2 ||
                    response.value("id", std::uint64_t{}) != request_id_)
                    throw std::runtime_error("Invalid/stale runtime response");
                if (stage_ == Stage::Hello)
                    session_ = response.at("session").get<std::string>();
                if (session_.empty() || response.value("session", "") != session_)
                    throw std::runtime_error("Stale runtime session");
                // Enforce the 5 s deadline BEFORE clearing waiting_.
                // The deadline is measured from the worker's
                // monotonic clock at send to the worker's monotonic
                // clock at RECEIPT (not at APPLICATION). A response
                // received on time but applied late is NOT a late
                // receipt (UI stall does not extend the deadline).
                // A late receipt IS rejected even if we are about
                // to apply it.
                if (receipt.received_at_ms != 0 &&
                    receipt.received_at_ms - sent_at_monotonic_ms_ > 5000) {
                    throw std::runtime_error(
                        "Runtime late receipt during " + sent_command_ + " (request " +
                        std::to_string(request_id_) + ", monotonic wait_ms=" +
                        std::to_string(receipt.received_at_ms - sent_at_monotonic_ms_) + ")");
                }
                waiting_ = false;
                if (diag)
                    sdk_diag_log(
                        "[sdk_diag editor t=%llu stage=response_received "
                        "cmd=%s id=%llu response_bytes=%zu "
                        "received_at_ms=%llu]",
                        static_cast<unsigned long long>(SDL_GetTicks()), sent_command_.c_str(),
                        static_cast<unsigned long long>(request_id_), receipt.payload.size(),
                        static_cast<unsigned long long>(receipt.received_at_ms));
                // Branch BEFORE normal-snapshot parsing: a `candidate`
                // response carries ONLY {protocol, session, id, ok,
                // runtime_contract, candidate} — no scene, no timing, no
                // recovery, no activation. The reference-driven fetch
                // must not be confused with a healthy active world.
                if (sent_command_ == "candidate") {
                    if (!response.value("ok", false)) {
                        // The expected benign race: the runtime
                        // cancelled/replaced the referenced candidate
                        // between the reference snapshot and our fetch.
                        // Treat as expected: discard the stale ref and
                        // any queued verdict, do NOT close the active
                        // world or surface this as a runtime fault. Any
                        // genuine runtime failure from `candidate`
                        // carries a non-`not available` error string and
                        // is propagated.
                        const auto err = response.value("error", std::string{"unknown"});
                        if (err.find("not available") != std::string::npos ||
                            err.find("no longer live") != std::string::npos ||
                            err.find("unknown candidate") != std::string::npos ||
                            err.find("unknown ticket") != std::string::npos) {
                            sdk_candidate_ticket_ = 0;
                            sdk_candidate_fetched_ticket_ = 0;
                            sdk_candidate_envelope_ = nullptr;
                            sdk_pending_candidate_ack_ = false;
                            sdk_candidate_ack_ = nullptr;
                            sdk_ack_terminal_ticket_ = 0;
                            sdk_ack_terminal_value_ = nullptr;
                            return;
                        }
                        throw std::runtime_error("Candidate envelope retrieval failed: " + err);
                    }
                    const auto envelope = response.value("candidate", Json());
                    if (!envelope.is_object())
                        throw std::runtime_error("Candidate envelope is not a JSON object");
                    const auto ticket = envelope.value("ticket", std::uint64_t{});
                    const auto env_session = envelope.value("session", std::string{});
                    if (ticket == 0 || env_session != session_)
                        throw std::runtime_error("Candidate envelope has invalid ticket/session");
                    if (ticket != sdk_candidate_ticket_ && sdk_candidate_ticket_ != 0)
                        throw std::runtime_error(
                            "Candidate envelope ticket drifted from requested reference");
                    sdk_candidate_envelope_ = envelope;
                    sdk_candidate_fetched_ticket_ = ticket;
                    // NOTE: do NOT set sdk_expected_ticket_ here. A
                    // successful fetch only proves the runtime has a
                    // frozen envelope; GPU admission is the editor's
                    // prepared verdict and is recorded on
                    // submit_sdk_candidate_ack(accepted=true). The
                    // expected ticket is the authoritative ticket for
                    // which the editor has shipped a positive prepared
                    // ack, retained until activation.generation
                    // matches, and cleared on supersession / commit /
                    // process close.
                    return;
                }
                if (response.value("quit_requested", false)) {
                    // Forward-compatible: runtime-initiated graceful stop.
                    // Treated as a normal stop (not an exception/recovery),
                    // so the editor authored source remains untouched.
                    const std::string diagnostic =
                        response.value("diagnostic", std::string{"Runtime requested quit"});
                    close_process();
                    status_ = diagnostic + ". Authored scene is safe; press Play to restart.";
                    return;
                }
                if (!response.value("ok", false)) {
                    const auto error = response.value("error", "Runtime rejected request");
                    if (transaction_ && stage_ == Stage::Load &&
                        error.starts_with("Play restart required:")) {
                        notice_ =
                            "Schema changed; restarted play with compatible host-owned values. ";
                        launch(checkpoint_, checkpoint_recovery_);
                        return;
                    }
                    // Too-late SDK loading cancel: the runtime
                    // reports the cancel ticket no longer matches
                    // the prepared scene. This is a nonfatal race
                    // (the user pressed Cancel just as the runtime
                    // committed, either before or after the editor
                    // observed publication), NOT a transport fault.
                    //
                    // Two valid outcomes depending on whether
                    // publication actually won:
                    //   * Editor has NOT observed activation
                    //     (sdk_activation_active_ == false): the
                    //     runtime may have published AFTER our last
                    //     successful response and BEFORE processing
                    //     the cancel. Refresh the snapshot now so the
                    //     next pump observes the live state. Preserve
                    //     sdk_expected_ticket_ so a follow-on
                    //     publication can commit. Do NOT claim the
                    //     world is retained until we actually observe
                    //     it.
                    //   * Editor HAS observed activation
                    //     (sdk_activation_active_ == true): the
                    //     published world is already ours to retain.
                    //     Surface an actionable notice and keep Play
                    //     alive.
                    //
                    // Either way, do NOT throw — throwing would tear
                    // down a healthy world. Do NOT relax runtime
                    // exact-ticket validation or fabricate a rollback.
                    if (sent_command_ == "cancel" && sdk_game_ &&
                        (error.find("cancel ticket does not match") != std::string::npos ||
                         error.find("no longer live") != std::string::npos ||
                         error.find("unknown ticket") != std::string::npos)) {
                        if (sdk_activation_active_) {
                            notice_ = "SDK loading cancel arrived after activation; "
                                      "published world retained. ";
                        } else {
                            notice_ = "SDK loading cancel no longer applies; "
                                      "refreshing snapshot to observe live state. ";
                            // Force the next pump to issue a snapshot
                            // immediately instead of waiting the
                            // documented 8 ms cadence.
                            sent_at_ = 0;
                        }
                        return;
                    }
                    throw std::runtime_error(error);
                }
                if (stage_ == Stage::Hello && (exact_sdk_ || sdk_game_)) {
                    const auto info = response.value("runtime_contract", Json::object());
                    if (info.value("profile", "") != "shared-native-sdk" ||
                        !info.value("sdk_project", false) ||
                        info.value("source_commit", "") != forge::source_commit)
                        throw std::runtime_error("SDK runtime does not match this editor source "
                                                 "or shared SDK profile. Select the matching "
                                                 "Native SDK installation in Gameplay Code.");
                    if (sdk_game_ && !info.value("sdk_play", false))
                        throw std::runtime_error("SDK Play runtime does not advertise the "
                                                 "sdk_play profile. Rebuild against the matching "
                                                 "Native SDK installation and retry.");
                }
                const auto diagnostics = response.value("diagnostics", Json::array());
                if (diagnostics != diagnostics_) {
                    for (const auto& d : diagnostics)
                        if (std::find(diagnostics_.begin(), diagnostics_.end(), d) ==
                            diagnostics_.end())
                            log_ +=
                                d.value("category", "runtime") + ": " + d.value("text", "") + "\n";
                    if (log_.size() > 131072)
                        log_.erase(0, log_.size() - 131072);
                    diagnostics_ = diagnostics;
                }
                snapshot_ = response.at("scene");
                recovery_ = response.at("recovery");
                effective_ = response.at("effective_scene");
                timing_ = response.at("timing");
                input_status_ = response.value("input", Json::object());
                physics_status_ = response.value("physics", Json::object());
                // SDK Play transport observations. Stage/none responses
                // carry these as null or empty values; do not assume a
                // healthy active world exists.
                if (sdk_game_) {
                    sdk_loading_ = response.value("loading", Json::object());
                    sdk_platform_effects_ = response.value("platform_effects", Json::array());
                    // Rebuild the offered-effects set from the latest
                    // snapshot. Entries older than the new observation
                    // are dropped along with any unsent acks that
                    // referenced them.
                    {
                        std::unordered_set<EffectKey, EffectKeyHash> live;
                        const auto live_epoch =
                            sdk_editor_epoch_.is_object()
                                ? sdk_editor_epoch_.value("epoch", std::uint64_t{})
                                : sdk_editor_epoch_seen_;
                        for (const auto& entry : sdk_platform_effects_) {
                            if (!entry.is_object())
                                continue;
                            const auto token = entry.value("token", std::uint64_t{});
                            const auto sequence = entry.value("sequence", std::uint64_t{});
                            const auto epoch = entry.value("epoch", std::uint64_t{});
                            if (!token || !sequence || epoch != live_epoch)
                                continue;
                            live.insert({token, sequence, epoch});
                        }
                        sdk_offered_effects_ = std::move(live);
                    }
                    sdk_release_required_ = response.value("release_required", false);
                    const auto& activation_obs = response.at("activation");
                    const auto observed_gen = activation_obs.value("generation", std::uint64_t{});
                    const auto activation_state = activation_obs.value("state", std::string{});
                    sdk_activation_active_ =
                        (activation_state == "ready" || activation_state == "active") &&
                        observed_gen != 0;
                    if (sdk_activation_active_)
                        sdk_activation_generation_ = observed_gen;
                    const auto& candidate_ref = response.value("candidate", Json());
                    if (candidate_ref.is_object()) {
                        const auto ticket = candidate_ref.value("ticket", std::uint64_t{});
                        if (ticket != sdk_candidate_ticket_) {
                            // New pending reference: remember it. We do
                            // NOT retire sdk_expected_ticket_ here — it
                            // persists across the mid-prepare snapshot
                            // where the ref may be cleared, so the
                            // Preparing -> Running commit check has
                            // authoritative evidence. Once the editor
                            // has actually fetched a frozen envelope for
                            // a positive ticket (handled in the
                            // candidate-response branch above), that
                            // ticket becomes sdk_expected_ticket_.
                            sdk_candidate_ticket_ = ticket;
                            sdk_candidate_fetched_ticket_ = 0;
                            // The previously-fetched envelope (if any)
                            // belongs to the prior world; drop it so a
                            // later sdk_candidate_ticket() reflects the
                            // current ref.
                            sdk_candidate_envelope_ = nullptr;
                            // Already-acks stale to the new ticket.
                            sdk_pending_candidate_ack_ = false;
                            sdk_candidate_ack_ = nullptr;
                            sdk_ack_terminal_ticket_ = 0;
                            sdk_ack_terminal_value_ = nullptr;
                        }
                    } else if (sdk_candidate_ticket_ != 0) {
                        // Runtime cleared its reference (post-initial
                        // admission / supersession). Drop the current
                        // ref + fetched + envelope + un-shipped verdict,
                        // but NEVER touch sdk_expected_ticket_ — that
                        // is the editor-prepared ticket preserved
                        // across this transition for the commit check.
                        sdk_candidate_ticket_ = 0;
                        sdk_candidate_fetched_ticket_ = 0;
                        sdk_candidate_envelope_ = nullptr;
                        sdk_pending_candidate_ack_ = false;
                        sdk_candidate_ack_ = nullptr;
                        sdk_ack_terminal_ticket_ = 0;
                        sdk_ack_terminal_value_ = nullptr;
                    }
                }
                auto next_ui = response.value("ui", Json());
                if (!next_ui.is_null() && !ui_snapshot_.is_null() &&
                    next_ui.at("generation") != ui_snapshot_.at("generation"))
                    ui_command_ = ui_ack_ =
                        nullptr; // Old queued requests cannot cross replacement.
                ui_snapshot_ = std::move(next_ui);
                if (response.contains("ui_ack"))
                    ui_ack_ = response.at("ui_ack");
                ++snapshot_version_;
                if (stage_ == Stage::Hello) {
                    stage_ = Stage::Replace;
                    Json replacement = {{"command", "replace"}, {"scene", initial_}};
                    if (!initial_recovery_.is_null()) {
                        replacement["recovery"] = initial_recovery_;
                        replacement["recovery_session"] = initial_recovery_.at("session");
                        replacement["recovery_tick"] = initial_recovery_.at("tick");
                    }
                    send(std::move(replacement));
                } else if (stage_ == Stage::Replace) {
                    if (sdk_game_) {
                        // SDK Play initial opt-in: do NOT begin_running.
                        // Poll additional snapshots until activation is
                        // observed live. sdk_expected_ticket_ is set on
                        // successful frozen-envelope fetch (in the
                        // candidate-response branch), not here — the
                        // initial replace response often publishes
                        // candidate:null and the runtime prepares the
                        // candidate afterwards.
                        sdk_activation_active_ = false;
                        sdk_activation_generation_ = 0;
                        stage_ = Stage::Preparing;
                    } else if (!loading_.empty()) {
                        stage_ = Stage::Load;
                        send({{"command", "load_module"}, {"path", loading_}});
                    } else
                        begin_running();
                } else if (stage_ == Stage::Boundary) {
                    if (recovery_.is_null()) {
                        requested_.clear();
                        reload_result_ = Reload::Failed;
                        notice_ = "Reload postponed: Play has no complete recovery snapshot. "
                                  "Previous module retained. ";
                        stage_ = Stage::Running;
                        if (!prior_paused_)
                            control_ = "resume";
                        return;
                    }
                    checkpoint_ = snapshot_;
                    checkpoint_recovery_ = recovery_;
                    previous_ = module_;
                    transaction_ = true;
                    loading_ = requested_;
                    requested_.clear();
                    stage_ = Stage::Load;
                    send({{"command", "load_module"}, {"path", loading_}});
                } else if (stage_ == Stage::Load) {
                    activation_generation_ = response.at("activation").at("generation");
                    if (probe_) {
                        stage_ = Stage::ProbeTick;
                        send({{"command", "step"}});
                    } else
                        begin_running();
                } else if (stage_ == Stage::Preparing) {
                    const auto& activation_value = response.at("activation");
                    const auto state = activation_value.value("state", std::string{});
                    const auto gen = activation_value.value("generation", std::uint64_t{});
                    sdk_activation_generation_ = gen;
                    const auto loading_value = response.value("loading", Json::object());
                    const auto loading_state_str = loading_value.value("state", std::string{});
                    if (loading_state_str == "failed" || loading_state_str == "cancelled") {
                        // Initial opt-in has no healthy old world to
                        // retain; surface a clean diagnostic, stop
                        // cleanly without raising a process fault.
                        sdk_candidate_ticket_ = 0;
                        sdk_candidate_fetched_ticket_ = 0;
                        sdk_candidate_envelope_ = nullptr;
                        sdk_pending_candidate_ack_ = false;
                        sdk_candidate_ack_ = nullptr;
                        sdk_ack_terminal_ticket_ = 0;
                        sdk_ack_terminal_value_ = nullptr;
                        sdk_expected_ticket_ = 0;
                        sdk_activation_active_ = false;
                        status_ = "Initial SDK Play candidate " + loading_state_str + ": " +
                                  loading_value.value("error", std::string{"unknown"});
                        close_process();
                        return;
                    }
                    // Commit only when activation.generation matches
                    // sdk_expected_ticket_ — the authoritative ticket
                    // captured on the editor-prepared frozen envelope
                    // (set in the candidate-response branch above).
                    // sdk_expected_ticket_ is preserved exactly here
                    // even if sdk_candidate_ticket_ has been zeroed
                    // by an intervening null reference.
                    if (state == "ready" && sdk_expected_ticket_ != 0 &&
                        gen == sdk_expected_ticket_) {
                        sdk_candidate_envelope_ = nullptr;
                        sdk_candidate_ticket_ = 0;
                        sdk_candidate_fetched_ticket_ = 0;
                        sdk_pending_candidate_ack_ = false;
                        sdk_candidate_ack_ = nullptr;
                        sdk_ack_terminal_ticket_ = 0;
                        sdk_ack_terminal_value_ = nullptr;
                        sdk_expected_ticket_ = 0;
                        activation_generation_ = gen;
                        sdk_activation_active_ = true;
                        begin_running();
                    }
                } else if (stage_ == Stage::ProbeTick) {
                    transaction_ = false;
                    module_ = loading_;
                    stage_ = Stage::Running;
                    status_ = "Probe passed one fixed tick.";
                }
                if (stage_ == Stage::Running && transaction_ &&
                    response.at("activation").value("state", "") == "active" &&
                    response.at("activation").value("generation", std::uint64_t{}) ==
                        activation_generation_) {
                    transaction_ = false;
                    module_ = loading_;
                    reload_result_ = Reload::Succeeded;
                    notice_ = "Reload committed after first live fixed tick. " + notice_;
                }
                if (stage_ == Stage::Running && !probe_)
                    update_status();
            }
            // Post-dispatch failure take. The worker is the
            // AUTHORITATIVE deadline owner: a receipt is only
            // published when its `received_at_ms <= sent_at_ms +
            // kDeadlineMs`, and any worker-set failure already
            // represents the worker's own conclusion (timeout /
            // EOF / process exit / protocol violation). We do
            // NOT re-check the deadline here — doing so against
            // a freshly-sampled `monotonic_ms()` can false-
            // positive when the main thread is slow between
            // drain_one_line and this check, even though the
            // receipt the worker published was on time.
            //
            // Throw on any worker failure surfaced AFTER dispatch
            // UNLESS the dispatch returned via the Quit handling
            // path (close_process + return — see response.value
            // ("quit_requested") branch above). That branch is
            // the only "graceful exit" special case; an
            // unexpected exit after an ordinary reply is a real
            // session fault.
            if (auto post_dispatch_failure = worker_.take_failure();
                !post_dispatch_failure.empty()) {
                throw std::runtime_error(post_dispatch_failure);
            }
            if (!waiting_) {
                if (sdk_game_ && (stage_ == Stage::Preparing || stage_ == Stage::Running)) {
                    // Single SDK Play scheduler. Priority order:
                    //  (1) observation/acks snapshot — anything pending
                    //      rides the next `snapshot` request before any
                    //      other wire traffic, so acks never starve
                    //      against continuous controls.
                    //  (2) pending candidate fetch — exactly once per
                    //      frozen ticket (we already hold the envelope
                    //      if sdk_candidate_fetched_ticket_ matches).
                    //  (3) controls / UI / model refresh.
                    //  (4) ordinary snapshot poll (8 ms cadence) to keep
                    //      the runtime warm.
                    const bool have_pending = sdk_pending_candidate_ack_ ||
                                              sdk_pending_platform_acks_ ||
                                              sdk_pending_editor_epoch_;
                    if (have_pending) {
                        send({{"command", "snapshot"}});
                    } else if (sdk_pending_cancel_loading_ticket_ != 0) {
                        // Bounded one-shot loading cancel. Sent as its
                        // own top-level command because the runtime's
                        // cancel dispatcher is keyed on `command`. The
                        // ack rides the next snapshot.
                        const auto cancel_ticket = sdk_pending_cancel_loading_ticket_;
                        sdk_pending_cancel_loading_ticket_ = 0;
                        send({{"command", "cancel"}, {"ticket", cancel_ticket}});
                    } else if (sdk_candidate_ticket_ != 0 &&
                               sdk_candidate_fetched_ticket_ != sdk_candidate_ticket_) {
                        send({{"command", "candidate"}, {"ticket", sdk_candidate_ticket_}});
                    } else if (!control_.empty()) {
                        const auto command = control_;
                        control_.clear();
                        send({{"command", command}});
                    } else if (model_assets_changed_) {
                        model_assets_changed_ = false;
                        send({{"command", "refresh_model_assets"}});
                    } else if (!ui_command_.is_null()) {
                        auto command = std::move(ui_command_);
                        ui_command_ = nullptr;
                        send({{"command", "ui"}, {"ui_command", std::move(command)}});
                    } else if (SDL_GetTicks() - sent_at_ >= 8)
                        send({{"command", "snapshot"}});
                } else if (!sdk_game_ && stage_ == Stage::Running) {
                    if (!requested_.empty()) {
                        prior_paused_ = paused();
                        desired_paused_ = prior_paused_;
                        stage_ = Stage::Boundary;
                        control_.clear();
                        send({{"command", "pause"}});
                    } else if (!control_.empty()) {
                        const auto command = control_;
                        control_.clear();
                        send({{"command", command}});
                    } else if (model_assets_changed_) {
                        model_assets_changed_ = false;
                        send({{"command", "refresh_model_assets"}});
                    } else if (!ui_command_.is_null()) {
                        auto command = std::move(ui_command_);
                        ui_command_ = nullptr;
                        send({{"command", "ui"}, {"ui_command", std::move(command)}});
                    } else if (!probe_ && SDL_GetTicks() - sent_at_ >= 8)
                        send({{"command", "snapshot"}});
                }
            }
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (transaction_ && !probe_ && !restoring_) {
                transaction_ = false;
                reload_result_ = Reload::Failed;
                loading_ = previous_;
                module_ = previous_;
                desired_paused_ = prior_paused_;
                restoring_ = true;
                notice_ =
                    "Reload failed; restored previous module and checkpoint: " + diagnostic + ". ";
                launch(checkpoint_, checkpoint_recovery_);
            } else {
                const bool recover = !exact_sdk_ && !probe_ && !restoring_ &&
                                     stage_ == Stage::Running && !recovery_.is_null();
                close_process();
                recoverable_ = recover;
                status_ = diagnostic + ". Authored scene is safe; " +
                          (recover ? "Recover resumes the last completed checkpoint."
                                   : "press Play to restart.");
            }
        }
    }
    // Bounded timing wrapper for unaccounted main-loop sections. Emits
    // SLOW sections only (>50 ms) to the existing sdk_diag_log. Reuses
    // the FORGE_SDK_DIAGNOSTIC_DIR env gate and 256 KiB cap; no new
    // logger. Accepts double elapsed_ms so existing frame-section
    // timings (SDL_GetTicks diff, integer ms) and Performance bucket
    // values (steady_clock, fractional ms) share one threshold.
    void sdk_diag_slow(const char* label, double elapsed_ms) const {
        if (elapsed_ms <= 50.0)
            return;
        sdk_diag_log("SLOW t=%llu label=%s elapsed_ms=%.3f req=%llu snap=%llu cmd=%s",
                     static_cast<unsigned long long>(SDL_GetTicks()), label, elapsed_ms,
                     static_cast<unsigned long long>(request_id_),
                     static_cast<unsigned long long>(snapshot_version_), sent_command_.c_str());
    }

  private:
    enum class Stage { Hello, Replace, Load, Boundary, ProbeTick, Running, Preparing };
    void close_process() {
        // Stop the background transport worker first. join() waits
        // for the worker thread to finish its current iteration AND
        // perform its SDL_KillProcess / SDL_WaitProcess /
        // SDL_DestroyProcess teardown path (those three calls are
        // NOT thread safe per pinned SDL3.4.16 src/process/SDL_process.c
        // and the worker is the single owner). After join() returns,
        // the SDL_Process* is destroyed and our process_ pointer is
        // null. We can then clear local state.
        worker_.stop();
        worker_.join();
        // Drain any leftover failure string so a later pump() (or
        // the next start() of a new worker on this same PlaySession)
        // doesn't re-read a stale one. The worker also clears
        // failure_ on its next start(), so this is defense in
        // depth.
        (void)worker_.take_failure();
        // The worker has now performed SDL_DestroyProcess on the
        // SDL_Process* and cleared its own internal handle. Mirror
        // that on our local pointer so active() returns false and
        // pump() short-circuits.
        process_ = nullptr;
        if (sdk_diag_file_ref()) {
            std::fclose(sdk_diag_file_ref());
            sdk_diag_file_ref() = nullptr;
            sdk_diag_bytes_ref() = 0;
        }
        control_.clear();
        session_.clear();
        ui_snapshot_ = ui_ack_ = ui_command_ = nullptr;
        model_assets_changed_ = false;
        waiting_ = false;
        // SDK Play transport state. Pending ack/epoch buffers MUST also
        // clear here so a follow-on start() does not surface a stale
        // candidate_ack. do this unconditionally because opt-in can be
        // turned on for one session and off for the next.
        sdk_candidate_envelope_ = nullptr;
        sdk_candidate_ticket_ = 0;
        sdk_candidate_fetched_ticket_ = 0;
        sdk_activation_generation_ = 0;
        sdk_activation_active_ = false;
        sdk_expected_ticket_ = 0;
        sdk_loading_ = Json::object();
        sdk_platform_effects_ = Json::array();
        sdk_release_required_ = false;
        sdk_pending_candidate_ack_ = false;
        sdk_candidate_ack_ = nullptr;
        sdk_pending_platform_acks_ = false;
        sdk_platform_acks_ = nullptr;
        sdk_pending_editor_epoch_ = false;
        sdk_editor_epoch_ = nullptr;
        sdk_editor_epoch_seen_ = 0;
        sdk_ack_terminal_ticket_ = 0;
        sdk_ack_terminal_value_ = nullptr;
        sdk_offered_effects_.reset();
        sdk_pending_cancel_loading_ticket_ = 0;
    }
    void begin_running() {
        stage_ = Stage::Running;
        if (!desired_paused_)
            control_ = "resume";
        restoring_ = false;
    }
    void update_status() {
        status_ = transaction_ ? "Reload pending first tick. Step or Resume to activate. "
                  : paused()   ? "Paused. "
                               : "Playing in isolated runtime. ";
        status_ += notice_;
    }
    void launch(const Json& scene, const Json& recovery = Json()) {
        const auto recovery_copy = recovery;
        const auto initial = scene;
        close_process();
        initial_ = snapshot_ = effective_ = initial;
        initial_recovery_ = recovery_ = recovery_copy;
        timing_ = {{"paused", true}, {"tick", 0}};
        ++snapshot_version_;
        stage_ = Stage::Hello;
        request_id_ = 0;
        input_events_.clear();
        input_status_ = Json::object();
        physics_status_ = Json::object();
        std::vector<const char*> args{executable_.c_str()};
        if (sdk_game_) {
            // SDK Play opt-in: identity via --sdk-project + --sdk-play on
            // and the OS writable base handed to the runtime via
            // --user-data. The user-data path is held through this
            // string so SDL_CreateProcess reads it once via c_str().
            // Audio + UI service flags are shared with the legacy
            // branch so the Reference Game runtime still gets Ui and
            // Audio capabilities. No other flags are duplicated.
            if (!audio_project_.empty()) {
                args.push_back("--sdk-project");
                args.push_back(audio_project_.c_str());
            }
            args.push_back("--audio");
            args.push_back((probe_ || headless_audio_) ? "offline" : "device");
            if (!probe_) {
                args.push_back("--ui");
                args.push_back("on");
            }
            sdk_user_data_path_ = user_data_override_.empty() ? path_utf8(game_user_data_base())
                                                              : user_data_override_;
            args.push_back("--sdk-play");
            args.push_back("on");
            args.push_back("--user-data");
            args.push_back(sdk_user_data_path_.c_str());
        } else if (!audio_project_.empty()) {
            args.push_back(exact_sdk_ ? "--sdk-project" : "--project");
            args.push_back(audio_project_.c_str());
            args.push_back("--audio");
            args.push_back((probe_ || headless_audio_) ? "offline" : "device");
            if (!probe_) {
                args.push_back("--ui");
                args.push_back("on");
            }
        }
        args.push_back(nullptr);
        log_.clear();
        diagnostics_ = Json::array();
        const auto properties = SDL_CreateProperties();
        const bool configured =
            properties &&
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data()) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                                  SDL_PROCESS_STDIO_APP);
        if (configured)
            process_ = SDL_CreateProcessWithProperties(properties);
        if (properties)
            SDL_DestroyProperties(properties);
        if (!process_) {
            status_ = std::string("Cannot start play: ") + SDL_GetError();
            return;
        }
        // Hand the SDL_Process* + its IO streams to the worker thread.
        // After this returns, the worker exclusively owns the process
        // handles (SDL docs forbid two-thread concurrent use of an
        // SDL_IOStream and SDL_DestroyProcess is not thread-safe). The
        // hello request below is queued onto the worker's outbound
        // queue; the worker thread does the actual write.
        auto* stdin_pipe = SDL_GetProcessInput(process_);
        auto* stdout_pipe = SDL_GetProcessOutput(process_);
        auto* stderr_pipe = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
            SDL_GetProcessProperties(process_), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
        if (!worker_.start(process_, stdin_pipe, stdout_pipe, stderr_pipe)) {
            status_ = std::string("Cannot start play: transport worker failed to start");
            // Worker start failed without owning the process — clean
            // up directly so we do not leak the SDL_Process*.
            SDL_DestroyProcess(process_);
            process_ = nullptr;
            return;
        }
        status_ = "Starting play...";
        send({{"command", "hello"},
              {"simulation_hz", simulation_hz_},
              {"input_map", input_map_},
              {"gravity", gravity_}});
    }
    void send(Json request) {
        sent_command_ = request.at("command").get<std::string>();
        if ((sent_command_ == "snapshot" || sent_command_ == "step" || sent_command_ == "resume" ||
             sent_command_ == "pause") &&
            !input_events_.empty()) {
            request["input_events"] = input_events_;
            input_events_.clear();
        }
        if (!physics_debug_selection.is_null())
            request["physics_debug"] = physics_debug_selection;
        // SDK Play: observation/ack payloads ride ONLY on `snapshot`
        // requests (runtime rejects them on every other command). Each
        // pending entry is consumed once. Stale candidate_ack entries
        // (session drifted, or ticket no longer matches the live fetch)
        // are silently discarded at send time so we never publish a
        // stale verdict. The successful wire shipment is recorded
        // (`sdk_ack_terminal_ticket_`) so duplicate identical
        // resubmissions remain idempotent.
        if (sdk_game_ && sent_command_ == "snapshot") {
            if (sdk_pending_candidate_ack_) {
                const auto& ack = sdk_candidate_ack_;
                const auto ack_session = ack.value("session", std::string{});
                const auto ack_ticket = ack.value("ticket", std::uint64_t{});
                if (ack_session == session_ && ack_ticket != 0 &&
                    ack_ticket == sdk_candidate_fetched_ticket_ &&
                    !sdk_candidate_envelope_.is_null()) {
                    request["candidate_ack"] = ack;
                    // Record the SHIPPED verdict as the authoritative
                    // terminal record. Idempotent identical
                    // resubmissions after this will match against
                    // sdk_ack_terminal_value_ in submit_sdk_candidate_ack.
                    sdk_ack_terminal_ticket_ = ack_ticket;
                    sdk_ack_terminal_value_ = ack;
                }
            }
            sdk_pending_candidate_ack_ = false;
            sdk_candidate_ack_ = nullptr;
            if (sdk_pending_platform_acks_) {
                request["platform_acks"] = sdk_platform_acks_;
                sdk_pending_platform_acks_ = false;
                sdk_platform_acks_ = nullptr;
            }
            if (sdk_pending_editor_epoch_) {
                request["editor_epoch"] = sdk_editor_epoch_;
                const auto epoch_value = sdk_editor_epoch_.value("epoch", std::uint64_t{});
                if (epoch_value != 0)
                    sdk_editor_epoch_seen_ = epoch_value;
                sdk_pending_editor_epoch_ = false;
                sdk_editor_epoch_ = nullptr;
            }
        }
        request["protocol"] = 2;
        request["id"] = ++request_id_;
        if (!session_.empty())
            request["session"] = session_;
        const auto line = request.dump() + "\n";
        if (line.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Play scene exceeds 16 MiB transport limit");
        // Worker monotonic clock — the deadline is measured from
        // THIS value to the receipt's received_at_ms. UI stalls
        // between worker RECEIPT and main-thread APPLICATION cannot
        // extend the deadline.
        const auto send_monotonic = PlayTransportWorker::monotonic_ms();
        if (!worker_.submit(line, send_monotonic))
            throw std::runtime_error("Worker rejected submit (not started, stopped, or pending "
                                     "request still in flight)");
        waiting_ = true;
        sent_at_ = SDL_GetTicks();
        sent_at_monotonic_ms_ = send_monotonic;
    }
    Double3 gravity_{0, -9.81, 0};
    Json recovery_, initial_recovery_, checkpoint_recovery_;
    double simulation_hz_ = 60;
    Json physics_status_ = Json::object();
    Json input_map_ = InputMap{}.source(), input_status_ = Json::object();
    Json ui_snapshot_, ui_ack_, ui_command_;
    bool model_assets_changed_ = false;
    std::vector<InputEvent> input_events_;
    SDL_Process* process_ = nullptr;
    Stage stage_ = Stage::Hello;
    Reload reload_result_ = Reload::Idle;
    std::string executable_, module_, loading_, requested_, previous_, notice_, session_, control_;
    Json checkpoint_, initial_, snapshot_, effective_, timing_ = {{"paused", true}, {"tick", 0}};
    Json diagnostics_ = Json::array();
    std::string audio_project_;
    bool transaction_ = false, restoring_ = false, probe_ = false, recoverable_ = false;
    // Explicit offline audio opt-in (set only by the CI SDK play fixture).
    // Independent from probe_ so the UI capability stays untouched.
    bool headless_audio_ = false;
    bool exact_sdk_ = false;
    bool prior_paused_ = false, desired_paused_ = false, waiting_ = false;
    std::uint64_t snapshot_version_ = 0, request_id_ = 0, activation_generation_ = 0;
    std::string sent_command_, log_, status_ = "Stopped. Play uses a copy of your authored scene.";
    Uint64 sent_at_ = 0;
    // Worker-clock sent timestamp used for the receive-deadline. The
    // 5 s timeout is measured from THIS value to the first receipt's
    // received_at_ms (worker monotonic clock); the diagnostic
    // sent_at_ above is kept for correlation with the rest of the
    // editor's SDL_GetTicks clock.
    std::uint64_t sent_at_monotonic_ms_ = 0;
    // Background byte-transport owner (see play_transport_worker.hpp).
    // Owns SDL_Process* + SDL_IOStream* exclusively. Main thread
    // communicates via submit/drain_lines/drain_stderr/take_failure.
    PlayTransportWorker worker_;
    // Diagnostic-only opt-in. The editor inherits FORGE_SDK_DIAGNOSTIC_DIR
    // (a directory path) via SDL3's default env inheritance (pinned SDL
    // src/process/windows/SDL_windowsprocess.c:249). When set, the editor
    // opens a per-process-unique file under that directory. The runtime
    // opens a SEPARATE file in the same directory; no shared FILE
    // ownership. Each file is capped at 256 KiB. The diagnostic does NOT
    // write to stderr — the runtime's stderr is a 4 KiB pipe the editor
    // drains into the existing log_ field. Records are sampled (every
    // 256th pump, slow gap > 100 ms, or any pump that read bytes from
    // the runtime). Default off: every diagnostic call is a no-op when
    // the env var is unset or fopen fails. Capped file I/O can still
    // block on disk — it is bounded by byte count, not by latency.
    // No public SDK surface, no wire protocol change.
    static FILE*& sdk_diag_file_ref() {
        static FILE* f = nullptr;
        return f;
    }
    static std::size_t& sdk_diag_bytes_ref() {
        static std::size_t n = 0;
        return n;
    }
    constexpr static std::size_t kSdkDiagCap = 256 * 1024;
    // Returns true and opens the file on first invocation; subsequent
    // invocations return the cached result without re-checking the env
    // var. The cap is enforced via sdk_diag_bytes_ref().
    static bool sdk_diag_open() {
        FILE*& f = sdk_diag_file_ref();
        if (f || sdk_diag_dir_ref() == nullptr)
            return f != nullptr;
        char path[4096];
        // Per-process unique filename so an editor restart inside the
        // same CI run does not overwrite the previous process's records.
        const long long start_counter =
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
        std::snprintf(path, sizeof(path), "%s/sdk_diag_editor_%lld.log", sdk_diag_dir_ref(),
                      start_counter);
        f = std::fopen(path, "wb");
        if (f)
            std::setvbuf(f, nullptr, _IONBF, 0);
        return f != nullptr;
    }
    static const char* sdk_diag_dir_ref() {
        static const char* dir = []() -> const char* {
            const char* v = SDL_getenv("FORGE_SDK_DIAGNOSTIC_DIR");
            return (v && v[0]) ? v : nullptr;
        }();
        return dir;
    }
    static void sdk_diag_log(const char* fmt, ...) {
        FILE* f = sdk_diag_file_ref();
        if (!f || sdk_diag_bytes_ref() >= kSdkDiagCap)
            return;
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n <= 0)
            return;
        std::size_t to_write = static_cast<std::size_t>(n);
        if (to_write >= sizeof(buf))
            to_write = sizeof(buf) - 1;
        if (sdk_diag_bytes_ref() + to_write + 1 > kSdkDiagCap)
            return;
        std::fwrite(buf, 1, to_write, f);
        std::fputc('\n', f);
        sdk_diag_bytes_ref() += to_write + 1;
    }
    static const char* sdk_diag_io_name(SDL_IOStatus s) {
        switch (s) {
        case SDL_IO_STATUS_READY:
            return "READY";
        case SDL_IO_STATUS_ERROR:
            return "ERROR";
        case SDL_IO_STATUS_EOF:
            return "EOF";
        case SDL_IO_STATUS_NOT_READY:
            return "NOT_READY";
        case SDL_IO_STATUS_READONLY:
            return "READONLY";
        case SDL_IO_STATUS_WRITEONLY:
            return "WRITEONLY";
        default:
            return "?";
        }
    }
    Uint64 last_diag_ticks_ = 0;
    std::uint64_t diag_iter_ = 0;
    // ---- SDK Play opt-in profile transport state -----------------------
    // Set once in configure(); opt-in is exact_sdk && sdk_game. The
    // launch path hands --sdk-play on + --user-data <absolute base> to
    // SDL_CreateProcess; that absolute path is held in this string.
    // user_data_override_ is the optional transient editor-side
    // override; empty means fall back to game_user_data_base().
    bool sdk_game_ = false;
    std::string user_data_override_;
    std::string sdk_user_data_path_;
    // Latest snapshot observations (mirror the wire response; raw JSON
    // copy for the renderer/UI/input adapter). Never fabricated.
    Json sdk_loading_ = Json::object();
    Json sdk_platform_effects_ = Json::array();
    bool sdk_release_required_ = false;
    std::uint64_t sdk_activation_generation_ = 0;
    // True iff the runtime has published a non-empty activation
    // (state ∈ {ready,active}) with a positive generation. Drives
    // ready() and submit_sdk_* acceptance independently from the
    // legacy stage machine — an Unload after Stage::Running must not
    // keep ready() true with a null scene.
    bool sdk_activation_active_ = false;
    // Frozen envelope state. sdk_candidate_ticket_ is the latest
    // reference observed on a snapshot; sdk_candidate_fetched_ticket_
    // is the ticket for which we hold sdk_candidate_envelope_.
    std::uint64_t sdk_candidate_ticket_ = 0;
    std::uint64_t sdk_candidate_fetched_ticket_ = 0;
    Json sdk_candidate_envelope_;
    // The exact ticket captured on the initial Replace that we are
    // waiting to see activated. Preserved across mid-prepare
    // intermediate snapshots whose `candidate` field may go null —
    // the runtime eventually bumps activation.generation to this
    // value when the prepared world is promoted.
    std::uint64_t sdk_expected_ticket_ = 0;
    // Pending ack/epoch side-effects. Each is a one-shot that rides the
    // next `snapshot` request then clears. Never invent positive acks.
    bool sdk_pending_candidate_ack_ = false;
    Json sdk_candidate_ack_;
    bool sdk_pending_platform_acks_ = false;
    Json sdk_platform_acks_;
    bool sdk_pending_editor_epoch_ = false;
    Json sdk_editor_epoch_;
    // Tracker for the highest editor-epoch observation we have SHIPPED
    // (or attempted to ship) on the wire. Used by submit_sdk_editor_epoch
    // to enforce strict positive monotonicity locally before the wire
    // round-trip; the runtime does the same on receipt.
    std::uint64_t sdk_editor_epoch_seen_ = 0;
    // Terminal verdict ledger. Once a candidate_ack has shipped, the
    // ticket is recorded here and subsequent submissions for that same
    // ticket are rejected unless byte-identical (idempotent). Cleared on
    // supersession / reject / commit / process close.
    std::uint64_t sdk_ack_terminal_ticket_ = 0;
    Json sdk_ack_terminal_value_;
    // One pending SDK loading-cancel ticket. Sends a `cancel` command
    // (single bounded request) on the next snapshot round-trip; 0
    // means no cancel is queued.
    std::uint64_t sdk_pending_cancel_loading_ticket_ = 0;
    // Live set of effect offers observed on the wire since the last
    // snapshot. Used by submit_sdk_platform_acks to reject acks that
    // reference (token, sequence, epoch) triples the runtime has not
    // actually offered. Optional so callers that never submit acks do
    // not have to populate it.
    struct EffectKey {
        std::uint64_t token, sequence, epoch;
        bool operator==(const EffectKey& o) const {
            return token == o.token && sequence == o.sequence && epoch == o.epoch;
        }
    };
    struct EffectKeyHash {
        std::size_t operator()(const EffectKey& k) const {
            return std::hash<std::uint64_t>{}(k.token) ^
                   (std::hash<std::uint64_t>{}(k.sequence) << 1) ^
                   (std::hash<std::uint64_t>{}(k.epoch) << 2);
        }
    };
    std::optional<std::unordered_set<EffectKey, EffectKeyHash>> sdk_offered_effects_;
};
} // namespace forge
