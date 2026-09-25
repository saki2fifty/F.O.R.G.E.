#pragma once
#include <cstdint>
#include <forge/game_session.hpp>
#include <forge/runtime_world.hpp>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace forge {
// Private Editor Play candidate admission channel.
//
// Runtime half only: the adapter publishes a copied envelope (session, ticket,
// scene, ui) once runtime-only readiness passes. Activation is gated by an
// explicit acknowledgement from the editor adapter; transport and editor-side
// GPU preparation remain unfinished and are out of scope here.
//
// The channel binds the calling thread as its owner and constructs adapters
// for one session string. It holds the session string, a monotonic nonzero
// ticket counter, and at most one pending record. No second world, queue,
// storage, or renderer exists here. Adapter lifetimes are owned by the
// caller; the channel only retains small metadata and copied snapshots.
//
// Owner-thread enforcement throws std::runtime_error on violation. Methods
// that return a bool (acknowledge, has_pending) translate the same violation
// into false. The destructor never throws.
class PlaySceneAdmission {
  public:
    // Optional callback invoked once on the owner thread immediately before
    // the candidate envelope is published. The callback receives the live
    // RuntimeWorld and the ticket. It may throw to reject the candidate; the
    // thrown message is surfaced through the GameSession loading state and
    // the active world is not affected.
    using PublicationValidator = std::function<void(RuntimeWorld& world, std::uint64_t ticket)>;

    // Session must be 1..kPlaySceneAdmissionSessionBytes bytes. Captures
    // the calling thread as the owner; every later public method and adapter
    // poll must be invoked on the same thread.
    explicit PlaySceneAdmission(std::string session);
    ~PlaySceneAdmission() noexcept;
    PlaySceneAdmission(const PlaySceneAdmission&) = delete;
    PlaySceneAdmission& operator=(const PlaySceneAdmission&) = delete;

    // Builds an adapter that publishes its snapshot via this channel.
    // Throws when a previous candidate is still pending, the ticket is zero
    // or not strictly greater than the previous one, or the caller is not on
    // the owner thread. The caller owns the returned adapter. The optional
    // `validator` runs once on the owner thread before the envelope is
    // frozen; throwing from it prevents publication.
    std::unique_ptr<GameScenePreparation> prepare(std::uint64_t ticket,
                                                  PublicationValidator validator = {});

    // Returns a detached copy of the current pending envelope, or null when
    // no candidate is currently exposed (idle, exported-then-committed, or
    // retired). The returned object is independent; mutating it does not
    // affect the gate. Throws on owner-thread violation.
    std::optional<Json> snapshot();

    // Returns a detached copy of the pending envelope for the requested
    // (session, ticket), or null when the channel has no exported record for
    // it (no candidate, candidate not yet exported, supersedure, commit, or
    // close). The session must match the channel's session and the ticket
    // must match the currently pending exported record; mismatches return
    // null so callers can map them to documented diagnostics.
    std::optional<Json> fetch_envelope(const std::string& session, std::uint64_t ticket);

    // Lightweight metadata reference for the currently pending exported
    // candidate, or null when there is none. Always returns just
    // {"session", "ticket", "ready"} so normal snapshots never inline the
    // full envelope.
    std::optional<Json> reference() const;

    // Records an acknowledgement for the current pending candidate. Returns
    // false for wrong session/ticket, foreign threads, zero ticket,
    // contradictory ack, ack before export, oversized diagnostic, or after
    // the candidate has been superseded/committed/closed. Repeated calls
    // with identical verdict and diagnostic return true; contradictory or
    // changed calls return false. Successful accepted ack lifts the gate;
    // successful rejected ack marks the candidate so its adapter's next poll
    // throws and the candidate is discarded. The rejected record remains
    // reachable (still uncommitted) until the adapter polls again, is
    // superseded, or is cancelled. The channel does not perform any actual
    // remote or GPU readiness validation; it trusts the explicit ack.
    bool acknowledge(const std::string& session, std::uint64_t ticket, bool accepted,
                     const std::string& diagnostic = {});

    // True while an uncommitted adapter record exists for this channel
    // (includes records after acknowledgement but before activation, and
    // rejected records retained until the next poll or cancellation).
    // Foreign thread returns false.
    bool has_pending() const noexcept;

  private:
    struct State;
    std::shared_ptr<State> state_;
};

// Half the legacy standalone host's process budget (16 MiB); keeps runtime
// payloads bounded without limiting the future graphics backend. All limits
// below are byte counts.
inline constexpr std::size_t kPlaySceneAdmissionEnvelopeBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kPlaySceneAdmissionDiagnosticBytes = 8192;
inline constexpr std::size_t kPlaySceneAdmissionSessionBytes = 128;
} // namespace forge