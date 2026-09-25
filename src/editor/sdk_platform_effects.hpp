#pragma once
#include "bounded_diagnostic.hpp"
#include "game_input.hpp"
#include "play.hpp"
#include "runtime_ui_host.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace forge {
// Narrow adapter that consumes PlaySession::sdk_platform_effects and
// turns each offered tuple into exactly one physical/native action
// via existing owners (GameInput, RuntimeUiHost). The adapter does
// NOT own the existing owners; it only borrows references.
//
// Source-confirmed wire schema (sdk_play_runtime.cpp90-91):
//   effect entry: {token, sequence, epoch, kind, value}
//   no `session` field on a per-entry basis — the enclosing snapshot
//   is correlated via PlaySession::session()
//   `kind` ∈ {"cursor", "navigation"}
//   cursor: value is JSON bool (true = capture, false = release)
//   navigation: value is JSON string (one of the documented
//               navigate directions)
//
// Contract:
//   - Each offered (session, token, sequence, epoch) is executed at
//     most once; identical repeats reuse the recorded result.
//   - The result cache is pruned when an offered tuple disappears
//     from the latest observation, on session change, on stop, or
//     when sdk_play() flips off. Empty offer sets always prune.
//   - Failed actions ship a negative ack with a bounded diagnostic
//     via submit_sdk_platform_acks; the adapter never reports
//     success on failure.
//   - Hidden/unfocused capture rejects and ships a negative ack.
//   - The adapter records the captured input-device enum and the
//     observed epoch only on actual physical changes — acking the
//     same effect MUST NOT bump the epoch.
class SdkPlatformEffects {
  public:
    SdkPlatformEffects(PlaySession& play, GameInput& game, RuntimeUiHost& ui)
        : play_(play), game_(game), ui_(ui) {}
    // The current input device enumeration: KeyboardMouse / Gamepad
    // / EditorHost. Empty when no physical device is currently
    // active. Cached on first pump() call after a state change.
    std::string device() const { return device_; }
    // True after a physical capture/release observation this frame.
    bool observed_change() const { return observed_change_; }
    // Set by the host each frame to indicate the Game panel currently
    // has ImGui keyboard/window focus (not merely visible). The
    // adapter only re-acquires menu routing when the Game panel is
    // focused, so a visible Game panel alongside a focused Scene or
    // Console panel does not silently reclaim routing after the user
    // surrendered it by clicking elsewhere. Cleared on focus loss
    // and on panel close; the host is the single source of truth.
    void set_game_focused(bool focused) { game_focused_ = focused; }
    bool game_focused() const { return game_focused_; }

    // One tick of the bounded executor. Walks the runtime-supplied
    // platform_effects entries (taken from the live snapshot), validates
    // each entry's schema, dedupes by {token, sequence, epoch}, runs
    // execute() on each fresh entry, and prunes cached results whose
    // keys are no longer offered. Same bound (kMax=256), same diagnostic
    // semantics, same cache key. Hidden / paused panels still pump so
    // the cache can prune, but no ack is queued. `game_panel_visible`
    // controls whether capture offers may proceed: a capture request
    // against a hidden or unfocused game panel is rejected with a
    // negative ack so the runtime knows the OS refused. Returns true
    // when an ack was successfully queued for the next snapshot request.
    bool pump(bool game_panel_visible) {
        observed_change_ = false;
        device_.clear();
        if (!play_.sdk_play()) {
            cache_.clear();
            return false;
        }
        const auto& wire_session = play_.session();
        if (cached_session_ != wire_session) {
            cache_.clear();
            cached_session_ = wire_session;
        }
        const auto& effects = play_.sdk_platform_effects();
        const auto live_epoch = play_.current_effective_epoch();
        // Empty offers — drop everything live and prune.
        if (!effects.is_array() || effects.empty()) {
            cache_.clear();
            return false;
        }
        std::unordered_set<EffectKey, EffectKeyHash> live_keys;
        // Bound the executor at the runtime's documented effect
        // limit. Source-confirmed at sdk_play_runtime.cpp28
        // (kEffectMapLimit=256) — anything more is a wire bug;
        // ignore the excess and ack the rest. The constant is
        // mirrored here because the runtime's kEffectMapLimit is
        // file-private; the matching limit is the contract.
        const std::size_t kMax = 256;
        std::size_t processed = 0;
        for (const auto& entry : effects) {
            if (processed >= kMax)
                break;
            if (!entry.is_object())
                continue;
            // Strict type guards: token / sequence / epoch must be
            // non-zero unsigned integers, kind must be a non-empty
            // string. A malformed entry is dropped here so it
            // cannot poison the rest of the batch or throw past
            // the adapter. The runtime already validated its own
            // serializer; this is for our defensive copy.
            const auto& token_field = entry.value("token", Json{});
            const auto& sequence_field = entry.value("sequence", Json{});
            const auto& epoch_field = entry.value("epoch", Json{});
            const auto& kind_field = entry.value("kind", Json{});
            if (!token_field.is_number_unsigned() || !sequence_field.is_number_unsigned() ||
                !epoch_field.is_number_unsigned() || !kind_field.is_string())
                continue;
            const auto token = token_field.get<std::uint64_t>();
            const auto sequence = sequence_field.get<std::uint64_t>();
            const auto epoch = epoch_field.get<std::uint64_t>();
            const auto& kind = kind_field.get_ref<const std::string&>();
            // Stale-epoch offers are explicitly dropped: the runtime
            // publishes a fresh observation each `editor_epoch`
            // shipping, so a stale epoch can never match the live
            // effective editor epoch (highest shipped AND any
            // pending queued observation).
            if (token == 0 || sequence == 0 || epoch != live_epoch || kind.empty())
                continue;
            EffectKey key{token, sequence, epoch};
            live_keys.insert(key);
            ++processed;
            auto it = cache_.find(key);
            if (it != cache_.end())
                continue;
            Result r;
            // Session correlation is the enclosing snapshot's
            // session, validated by PlaySession itself.
            r.session = cached_session_;
            execute(entry, kind, r, game_panel_visible);
            if (r.observed_change)
                observed_change_ = true;
            cache_[key] = std::move(r);
        }
        if (device_.empty())
            device_ = "EditorHost";
        // Prune keys no longer offered.
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (live_keys.find(it->first) == live_keys.end())
                it = cache_.erase(it);
            else
                ++it;
        }
        return false;
    }
    // Drain pending acks through the existing transport-side
    // submit_sdk_platform_acks validator. The validator rejects
    // malformed diagnostics, foreign sessions, stale epochs, and
    // not-currently-offered tuples; the adapter relies on that
    // validator and never invents fields.
    bool take_pending_ack(Json& batch) {
        batch = Json::array();
        const auto live_epoch = play_.current_effective_epoch();
        const auto& wire_session = play_.session();
        for (const auto& entry : cache_) {
            const auto& key = entry.first;
            const auto& r = entry.second;
            if (r.queued || !r.session_ok)
                continue;
            if (key.epoch != live_epoch)
                continue;
            if (wire_session.empty())
                continue;
            Json ack = Json::object();
            ack["session"] = wire_session;
            ack["token"] = key.token;
            ack["sequence"] = key.sequence;
            ack["epoch"] = key.epoch;
            ack["accepted"] = r.accepted;
            // Bound the diagnostic before queueing so the transport
            // validator never rejects on size alone. Existing
            // helper; the full original text is also surfaced via
            // the editor Problems path elsewhere.
            ack["diagnostic"] = bound_diagnostic(r.diagnostic);
            batch.push_back(std::move(ack));
        }
        if (batch.empty())
            return false;
        if (!play_.submit_sdk_platform_acks(batch)) {
            // Transport refused the batch. Drop the cache entries
            // (they would otherwise hang waiting forever for a
            // follow-up offer), stop Play cleanly, and surface a
            // clear status. This is the documented behavior when
            // the runtime cannot be reached from this connection.
            const auto refused = cache_.size();
            cache_.clear();
            batch = Json::array();
            stop_requested_ = true;
            (void)refused;
            return false;
        }
        for (auto& entry : batch) {
            EffectKey k{entry.at("token").get<std::uint64_t>(),
                        entry.at("sequence").get<std::uint64_t>(),
                        entry.at("epoch").get<std::uint64_t>()};
            auto it = cache_.find(k);
            if (it != cache_.end())
                it->second.queued = true;
        }
        return true;
    }
    // True when the adapter has decided the host must stop Play to
    // avoid a hang (e.g. transport refused the platform_acks
    // batch). Main drains this once per frame and forwards to
    // play.stop().
    bool stop_requested() const { return stop_requested_; }
    // One-shot acknowledgement so main can resume after handling.
    void clear_stop_request() { stop_requested_ = false; }
    // Hard reset. Called on Stop / project switch / restart.
    void clear() {
        cache_.clear();
        cached_session_.clear();
        device_.clear();
        observed_change_ = false;
        // The host re-asserts focus on the next frame, so dropping
        // a stale focus latch here matches editor state.
        game_focused_ = false;
        // Clear the latched hard-stop flag as well so a hard-reset
        // (Stop button / project switch / restart) cannot leave the
        // adapter asking main to stop on every subsequent pump while
        // the user is just navigating the editor.
        stop_requested_ = false;
    }

  private:
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
    struct Result {
        bool accepted = false;
        std::string diagnostic;
        bool queued = false;
        bool observed_change = false;
        bool session_ok = true;
        std::string session;
    };
    void execute(const Json& entry, const std::string& kind, Result& out, bool game_panel_visible) {
        // Strict schema parse. cursor.value MUST be boolean;
        // navigation.value MUST be string. Foreign types reject
        // with a bounded diagnostic and never throw past the
        // adapter — the runtime's parser is allowed to throw,
        // ours is not.
        const auto& raw_value = entry.value("value", Json{});
        if (kind == "cursor") {
            if (!raw_value.is_boolean()) {
                out.accepted = false;
                out.diagnostic = "play.platform_effects.cursor.bad_value";
                return;
            }
            const bool capture = raw_value.get<bool>();
            // Hidden / unfocused capture requests are rejected: the
            // physical mouse capture belongs to the user, not the
            // runtime. Release may proceed even while hidden so the
            // user can surrender the capture without seeing the
            // Game panel first, and even while unfocused so a
            // runtime-side cursor=true does not silently re-engage
            // relative mouse mode after the user clicked another
            // editor panel. The host's per-frame focus flag
            // (ImGui::IsWindowFocused on the Game tab after Begin())
            // is the same flag the host uses for the cursor=false
            // path's acquire_routing rule below; the rejection here
            // prevents capture=true from bypassing that rule through
            // a relative-mode bypass.
            if (capture && !game_panel_visible) {
                out.accepted = false;
                out.diagnostic = "play.platform_effects.cursor.hidden";
                device_ = "EditorHost";
                return;
            }
            if (capture && !game_focused_) {
                out.accepted = false;
                out.diagnostic = "play.platform_effects.cursor.unfocused";
                device_ = "EditorHost";
                return;
            }
            // Exactly one physical setter per effect, dispatched on
            // current game state to avoid double release/capture
            // pairs that would race the SDL setter:
            //   capture=true (visible)   → capture_checked(play,true)
            //   capture=false + eligible (visible panel, runtime
            //                            ready, !game.captured())
            //                            → capture_checked(play,false)
            //                              establishes initial menu
            //                              routing without engaging
            //                              relative mouse mode.
            //   everything else          → release_checked(play,
            //                              keep_routing=visible).
            //                              Visible release ships the
            //                              neutral edge; hidden release
            //                              drops routing entirely. A
            //                              not-ready initial release
            //                              still succeeds because
            //                              release_checked tolerates the
            //                              absence of a physical target.
            out.observed_change = true;
            const bool captured = game_.captured();
            const bool ready = play_.ready();
            // Initial menu routing acquisition requires the Game panel
            // to actually hold ImGui focus, not merely be visible. A
            // visible Game panel alongside a focused Scene/Console
            // panel must not silently reclaim routing after the user
            // surrendered it by clicking elsewhere; the host restores
            // routing on a fresh focused frame.
            const bool acquire_routing =
                !capture && game_panel_visible && game_focused_ && ready && !captured;
            if (capture) {
                const bool ok = game_.capture_checked(play_, true);
                out.accepted = ok;
                if (!ok) {
                    out.diagnostic = "play.platform_effects.cursor.capture_failed";
                    if (!game_.capture_error().empty()) {
                        out.diagnostic += ": ";
                        out.diagnostic += game_.capture_error();
                    }
                }
                device_ = "KeyboardMouse";
                return;
            }
            if (acquire_routing) {
                const bool ok = game_.capture_checked(play_, false);
                out.accepted = ok;
                if (!ok) {
                    out.diagnostic = "play.platform_effects.cursor.menu_routing_failed";
                    if (!game_.capture_error().empty()) {
                        out.diagnostic += ": ";
                        out.diagnostic += game_.capture_error();
                    }
                }
                device_ = "KeyboardMouse";
                return;
            }
            const bool ok = game_.release_checked(play_, game_panel_visible);
            out.accepted = ok;
            if (!ok) {
                out.diagnostic = "play.platform_effects.cursor.release_failed";
                if (!game_.capture_error().empty()) {
                    out.diagnostic += ": ";
                    out.diagnostic += game_.capture_error();
                }
            }
            device_ = "KeyboardMouse";
            return;
        }
        if (kind == "navigation") {
            if (!raw_value.is_string()) {
                out.accepted = false;
                out.diagnostic = "play.platform_effects.navigation.bad_value";
                return;
            }
            const auto direction = raw_value.get<std::string>();
            if (direction.empty()) {
                out.accepted = false;
                out.diagnostic = "play.platform_effects.navigation.empty";
                return;
            }
            std::string failure;
            ui_.checked_navigate(direction, failure);
            out.accepted = failure.empty();
            out.diagnostic = failure;
            device_ = "KeyboardMouse";
            return;
        }
        out.accepted = false;
        out.diagnostic = "play.platform_effects.unknown_kind";
    }
    PlaySession& play_;
    GameInput& game_;
    RuntimeUiHost& ui_;
    std::string cached_session_;
    std::string device_;
    bool observed_change_ = false;
    bool stop_requested_ = false;
    std::unordered_map<EffectKey, Result, EffectKeyHash> cache_;
    bool game_focused_ = false;
};
} // namespace forge