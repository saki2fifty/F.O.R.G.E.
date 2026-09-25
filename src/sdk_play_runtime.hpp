#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <forge/audio.hpp>
#include <forge/game_host_controls.hpp>
#include <forge/game_storage.hpp>
#include <forge/native_sdk.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_protocol.hpp>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace forge {
// Private one-process host assembly for the Editor Play opt-in path.
//
// The host owns exactly one PlaySceneAdmission, one GameControlQueue, one
// GameStorage, one GameSession and one GameHostControls. No second world,
// clock, queue, renderer or platform adapter is introduced. Process
// interaction with the editor happens exclusively over the existing
// protocol2 envelope and never touches the legacy ABI1 reload path.
//
// --- Wire contract (sdk_play) -----------------------------------------
//
// Normal `snapshot` response (ok=true): {protocol, session, id,
// runtime_contract, loading, platform_effects, release_required, module,
// recovery, timing, activation{state, generation, ticket}, diagnostics,
// initial_diagnostic?, candidate, scene?, effective_scene?, schema?,
// input?, physics?, audio?, ui?}. When no active world exists
// (activation.state == "none"), scene-shaped fields (scene,
// effective_scene, schema, input) are explicitly null.
//
// `candidate` field on a normal snapshot is a lightweight reference
// {session, ticket, ready} or null when no candidate is pending. The
// frozen envelope is NEVER inlined on the normal snapshot. To fetch the
// frozen envelope the editor sends a separate `candidate` request:
//
//   {"protocol":2,"id":N,"command":"candidate","ticket":K}
//
//   → {"protocol":2,"session",...,"candidate":{...envelope...},"ok":true}
//
// Editor-produced side-effects (`editor_epoch`, `platform_acks`,
// `candidate_ack`) ride ONLY on `snapshot`, never on `candidate`.
//
// --- Direction of authority --------------------------------------------
//
// The EDITOR adapter is the sole producer of physical cursor and
// navigation effects AND the sole consumer of the runtime's `pending
// effect` references (the polled adapters). The RUNTIME consumes
// `platform_acks`, `candidate_ack` and `editor_epoch` observations from
// the editor and never dispatches physical effects itself. A release
// request supersedes older unacknowledged capture offers; the runtime
// tracks the editor's observed focus/cursor epoch and treats offers
// observed under an older epoch as terminal failures rather than silent
// drops.
//
// --- Bounds ------------------------------------------------------------
//
// The full response (including trailing newline) must fit the wire limit
// (RuntimeIo::limit, currently 16 MiB). Snapshots that grow past the
// bound after activation return a correlated error; candidate envelopes
// whose projected post-activation response would not fit are rejected by
// the admission validator BEFORE the active world is disturbed.
class SdkPlayRuntime {
  public:
    // Caller-supplied values. `modules` is taken by value to preserve the
    // native code lease the caller has already loaded; `defaults` is the
    // shared project.game document. `user_base` must be an absolute OS
    // directory the editor forwards from `game_user_data_base()`.
    struct Config {
        std::filesystem::path project;
        std::filesystem::path user_base;
        RuntimeConfig runtime{};
        InputMap input{};
        PhysicsConfig physics{};
        std::vector<EngineModule> modules;
        Json defaults;
        std::optional<AudioConfig> audio;
        bool ui = false;
    };
    struct EffectOffer;
    explicit SdkPlayRuntime(Config);
    ~SdkPlayRuntime();
    SdkPlayRuntime(const SdkPlayRuntime&) = delete;
    SdkPlayRuntime& operator=(const SdkPlayRuntime&) = delete;

    // One host tick. Advances the active scene and pumps the queued game
    // requests through GameHostControls. Initial and gameplay-replacement
    // candidates are advanced exactly once per pump; tick faults stop
    // further tick work and surface a diagnostic.
    void pump(RuntimeClock::Time now);

    // One request handler used by both the in-process tests and the wire
    // loop. Returns a full protocol2 response with the supplied request id,
    // session correlation and bounded payload. Never touches the pipe.
    // `now` is the owner-thread clock sample used for pause/resume; tests
    // may pass a deterministic value, production passes `Clock::now()`.
    Json handle(const Json& request, RuntimeClock::Time now = RuntimeClock::Time{});

    // Process entry. Constructs a local RuntimeIo and drives the receive/
    // handle/send loop until the editor closes the pipe or `quit` is set.
    void process();

    // Read-only inspection helpers used by tests and the editor adapter.
    std::optional<Json> pending_candidate() const;
    Json live_effects() const;
    std::uint64_t editor_epoch() const;
    Json status() const;
    Json loading() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
    // Unchecked inner handler; `handle()` wraps it with a final
    // size-enforcement pass before returning. Kept private so the wire
    // and the test path share one boundary.
    Json handle_inner(const Json& request, RuntimeClock::Time now);
};
} // namespace forge
