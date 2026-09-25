#include "play_presentation.hpp"
#include "../frame_renderer.hpp"
#include "mesh_render_host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace forge {
namespace {
// Bounded valid staged-render dimensions. FrameRenderer::targets()
// still validates the final dimensions against the device profile.
constexpr unsigned kStagedMinWidth = 64;
constexpr unsigned kStagedMinHeight = 64;
constexpr unsigned kStagedMaxWidth = 2048;
constexpr unsigned kStagedMaxHeight = 2048;
// render.camera.missing is the documented computational-editor
// outcome (no cameras in a blank/empty scene); every other Error/Fatal
// disqualifies the candidate. `category` is the documented field on
// Diagnostic (services.hpp24); there is no `code` member.
bool missing_camera_only(const FrameRenderer& renderer) {
    for (const auto& d : renderer.diagnostics()) {
        if (d.category == "render.camera.missing")
            continue;
        if (d.severity == Severity::Error || d.severity == Severity::Fatal)
            return false;
    }
    return true;
}
unsigned clamp_dim(unsigned v, unsigned fallback) {
    if (!v)
        return fallback;
    return std::clamp(v, kStagedMinWidth, kStagedMaxWidth);
}
// Runtime rejects diagnostic strings longer than 8 KiB with `false`,
// so unbounded editor text can silently fail the verdict and leave
// the runtime hanging. Produce a bounded ASCII-only summary that
// stays safely under the cap and preserves a short prefix of the
// original text. Non-ASCII bytes (UTF-8 continuation/lead) are
// dropped so the resulting string is always valid 7-bit ASCII and
// never splits a multi-byte code point.
// Runtime rejects diagnostic strings longer than 8 KiB with `false`,
// so unbounded editor text can silently fail the verdict and leave
// the runtime hanging. For valid short UTF-8 under the documented
// protocol cap we preserve the whole text byte-for-byte (only
// sanitizing CR/LF/Tab to spaces so the wire stays plain ASCII in
// the control bytes); that lets the editor report the exact text
// without claiming truncation falsely. Only when the input truly
// does not fit do we produce a bounded 7-bit ASCII summary under
// the 8 KiB cap. No new Unicode framework, no external dependency:
// this is a single pass over the bytes plus an existing ASCII
// summary for the oversized case.
std::string bound_ascii_diagnostic(const std::string& in) {
    constexpr std::size_t kMax = 8 * 1024 - 64; // leave headroom for any
                                                // editor-prefixed prefix.
    if (in.size() <= kMax) {
        // Short valid UTF-8: preserve every byte (caller keeps the
        // exact text). Only CR/LF/Tab are normalized to space so
        // the wire round-trip stays plain ASCII in the control
        // bytes; multi-byte UTF-8 sequences pass through verbatim.
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in)
            out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : char(c));
        return out;
    }
    // Oversized: fall back to the existing bounded 7-bit ASCII
    // summary. Non-ASCII bytes are dropped (never split a code
    // point) so the result is always valid 7-bit ASCII under the
    // 8 KiB protocol cap. The trailing "...(truncated)" is honest:
    // we only append it when the input was actually too long.
    std::string out;
    out.reserve(kMax + 32);
    for (unsigned char c : in) {
        if (out.size() >= kMax)
            break;
        if (c == '\n' || c == '\r' || c == '\t')
            out.push_back(' ');
        else if (c < 0x80)
            out.push_back(char(c));
    }
    out += "...(truncated)";
    return out;
}
} // namespace

void PlayPresentation::release_staged() {
    if (staged_renderer_) {
        try {
            staged_renderer_->resources({});
        } catch (...) {
        }
        staged_renderer_.reset();
    }
    if (staged_ui_ticket_ != 0 && active_ui_) {
        try {
            active_ui_->cancel_staged(staged_ui_ticket_);
        } catch (...) {
            // Cancellation never rethrows past the helper.
        }
    }
    staged_ui_ticket_ = 0;
    staged_envelope_ = nullptr;
    staged_session_.clear();
    staged_ticket_ = 0;
    state_ = StageState::Idle;
}

void PlayPresentation::clear(PlaySession& play) {
    (void)play;
    release_staged();
    last_seen_generation_ = 0;
    last_committed_session_.clear();
    last_committed_ticket_ = 0;
    last_rejected_session_.clear();
    last_rejected_ticket_ = 0;
}

bool PlayPresentation::prepare_renderer(PlaySession& play) {
    (void)play;
    if (!context_ || !host_ || !presentation_)
        return false;
    if (!staged_renderer_) {
        staged_renderer_ = std::make_unique<FrameRenderer>(*presentation_);
        staged_renderer_->resources(host_);
    }
    return true;
}

void PlayPresentation::advance_renderer(PlaySession& play) {
    if (!staged_renderer_ || !context_ || staged_envelope_.is_null())
        return;
    // Use the staged renderer output dims if available, otherwise a
    // bounded fallback so resource admission does not stall on a
    // zero-sized frame.
    unsigned base_w = 1280, base_h = 720;
    if (auto* out = staged_renderer_->output()) {
        const auto desc = out->GetTexture()->GetDesc();
        if (desc.Width)
            base_w = desc.Width;
        if (desc.Height)
            base_h = desc.Height;
    }
    const auto w = clamp_dim(base_w, 1280u);
    const auto h = clamp_dim(base_h, 720u);
    try {
        staged_renderer_->game(context_, staged_envelope_.at("scene"), w, h);
    } catch (...) {
        // game() may throw on transient scene extraction failures;
        // surface as a local exception in poll() via the outer
        // try/catch so the verdict reflects the real cause.
        throw;
    }
    // Drain the shared mesh host queue so the staged renderer sees
    // mesh readiness even when the editor window is hidden /
    // minimized and main's main loop early-continues before reaching
    // its native resource submit. host_->submit() is the existing
    // backend-neutral Diligent-flushed submission helper.
    if (host_)
        host_->submit();
}

bool PlayPresentation::prepare_ui(PlaySession& play) {
    (void)play;
    if (!active_ui_)
        return false; // explicit failure: no UI host cannot admit UI.
    if (!staged_envelope_.contains("ui") || staged_envelope_.at("ui").is_null())
        return true; // candidate UI-less — clear-on-commit handled later.
    if (staged_ui_ticket_ != 0)
        return active_ui_->prepared_staged(staged_ui_ticket_);
    staged_ui_ticket_ = active_ui_->prepare_staged(staged_envelope_.at("ui"));
    return active_ui_->prepared_staged(staged_ui_ticket_);
}

bool PlayPresentation::candidate_resources_ready() const {
    if (!staged_renderer_ || !host_)
        return false;
    if (staged_renderer_->pending())
        return false;
    if (staged_renderer_->omitted_diagnostics() != 0)
        return false;
    if (!missing_camera_only(*staged_renderer_))
        return false;
    return true;
}

bool PlayPresentation::reject_once(PlaySession& play, const std::string& diagnostic) {
    if (staged_ticket_ == 0 || staged_session_.empty())
        return false;
    const auto session = staged_session_;
    const auto ticket = staged_ticket_;
    const auto bounded = bound_ascii_diagnostic(diagnostic);
    bool ok = false;
    std::string transport_diag;
    try {
        ok = play.submit_sdk_candidate_ack(false, session, ticket, bounded);
        if (!ok)
            transport_diag = "verdict refused by transport";
    } catch (const std::exception& e) {
        ok = false;
        transport_diag = e.what();
    } catch (...) {
        ok = false;
        transport_diag = "unknown transport exception";
    }
    if (!ok) {
        // Verdict could not be queued: stop Play with a clear local
        // diagnostic so the runtime stops waiting on this candidate
        // and the editor surfaces the cause. last_rejected_* is left
        // unset on purpose — a future different ticket proceeds
        // normally without being blocked by this transport failure.
        pending_failure_ = "play.stage.verdict_unreachable: " +
                           (transport_diag.empty() ? std::string("ack refused") : transport_diag);
        release_staged();
        play.stop();
        return false;
    }
    last_rejected_session_ = session;
    last_rejected_ticket_ = ticket;
    release_staged();
    return true;
}

bool PlayPresentation::commit(PlaySession& play) {
    // The runtime has already published this activation; we cannot
    // refuse it. UI activation happens FIRST so a UI-side failure is
    // visible before the renderer is swapped in — a partial swap
    // would otherwise leave the editor showing a runtime-published
    // world against the wrong UI.
    if (active_ui_) {
        const bool has_ui = staged_envelope_.contains("ui") && !staged_envelope_.at("ui").is_null();
        if (has_ui && staged_ui_ticket_ != 0) {
            std::string ui_diag;
            bool ui_ok = true;
            try {
                active_ui_->activate_staged(staged_ui_ticket_);
            } catch (const std::exception& e) {
                ui_ok = false;
                ui_diag = e.what();
            } catch (...) {
                ui_ok = false;
                ui_diag = "runtime-ui.activate.unknown_exception";
            }
            if (!ui_ok) {
                // Surface the activate failure to the editor and stop
                // Play cleanly. The previous renderer/UI stay intact;
                // do NOT advertise a mixed swap that did not happen.
                pending_failure_ = "play.stage.activate_failed: " +
                                   (ui_diag.empty() ? std::string("UI activate refused") : ui_diag);
                release_staged();
                state_ = StageState::Idle;
                play.stop();
                return false;
            }
        } else {
            // Drop the live UI on commit when the accepted candidate
            // has no UI. The candidate that introduced the live UI
            // gets explicitly retired; this is intentional.
            active_ui_->clear_ui(play);
        }
    }
    // Only after UI activation succeeds do we move the staged
    // renderer into the live slot. The shared MeshResourceHost is
    // preserved across this swap.
    if (staged_renderer_) {
        active_renderer_.reset();
        active_renderer_ = std::move(staged_renderer_);
        if (renderer_slot_)
            *renderer_slot_ = active_renderer_.get();
    }
    const auto committed_session = staged_session_;
    const auto committed_ticket = staged_ticket_;
    staged_ui_ticket_ = 0;
    staged_envelope_ = nullptr;
    staged_session_.clear();
    staged_ticket_ = 0;
    state_ = StageState::Idle;
    // Record last successful session/ticket so a stale identical
    // envelope arriving again is skipped, but the next different
    // ticket is admitted immediately.
    last_committed_session_ = committed_session;
    last_committed_ticket_ = committed_ticket;
    last_rejected_session_.clear();
    last_rejected_ticket_ = 0;
    last_seen_generation_ = cached_activation_generation_;
    return true;
}

bool PlayPresentation::poll(PlaySession& play, const std::filesystem::path& project) {
    cached_activation_generation_ = play.sdk_activation_generation();
    if (!play.sdk_play()) {
        // Legacy / non-SDK paths own their own presentation state;
        // nothing to do.
        if (state_ != StageState::Idle)
            release_staged();
        return false;
    }
    if (!play.active()) {
        // Stop path: cancel any staged owners immediately, even when
        // the runtime hasn't published a new candidate envelope.
        release_staged();
        return false;
    }
    // 1) Awaiting: the runtime accepted our prepared verdict. Commit
    //    FIRST if the runtime now publishes a matching (session,
    //    ticket) activation generation; only then fall through to
    //    supersedence / stop checks. The session AND the ticket must
    //    both fail to match before the staged pair is released.
    if (state_ == StageState::Awaiting && staged_ticket_ != 0) {
        const auto& wire_session = play.session();
        const bool session_match = wire_session == staged_session_;
        const bool activation_match = play.sdk_activation_active() &&
                                      cached_activation_generation_ == staged_ticket_ &&
                                      session_match;
        if (activation_match)
            return commit(play);
        if (!session_match || play.sdk_candidate_ticket() != staged_ticket_) {
            release_staged();
            return true;
        }
        return false;
    }
    // 2) Idle path: prepare (or continue preparing) the candidate.
    const auto& envelope = play.sdk_candidate_envelope();
    const bool valid_envelope = envelope.is_object() && envelope.contains("ticket") &&
                                envelope.at("ticket").is_number_unsigned() &&
                                envelope.contains("session");
    if (!valid_envelope) {
        // Reference vanished: a Prepared candidate with no current
        // envelope must be released immediately. An Idle adapter has
        // nothing to release.
        if (state_ == StageState::Prepared)
            release_staged();
        return false;
    }
    const auto ticket = envelope.at("ticket").get<std::uint64_t>();
    const auto session = envelope.at("session").get<std::string>();
    if (session.empty() || session != play.session()) {
        // Empty session or session drift means the candidate is no
        // longer valid against the live runtime session.
        if (state_ == StageState::Prepared)
            release_staged();
        return false;
    }
    // Skip a stale identical envelope already committed.
    if (state_ == StageState::Idle && session == last_committed_session_ &&
        ticket == last_committed_ticket_ && last_committed_ticket_ != 0)
        return false;
    // Skip while the runtime keeps re-offering the same rejected tuple.
    if (state_ == StageState::Idle && session == last_rejected_session_ &&
        ticket == last_rejected_ticket_ && last_rejected_ticket_ != 0)
        return false;
    if (state_ == StageState::Idle) {
        staged_envelope_ = envelope;
        staged_session_ = session;
        staged_ticket_ = ticket;
        staged_ui_ticket_ = 0;
        // Ensure the UI host knows the project before presenter
        // construction so the very first ensure_presenter uses the
        // correct path (otherwise the staged UI binds to the empty
        // initial project_). On a real project change the host
        // releases/destroys/resets its owned presenter, renderer,
        // staged bookkeeping and input routing in-place before
        // installing the new path; an ordinary same-project call is
        // a no-op so cached creation-failure and resource
        // previous_good stay intact.
        if (active_ui_)
            active_ui_->set_project(project, play);
        bool prepared_ok = false;
        std::string failure;
        try {
            prepared_ok = prepare_renderer(play) && prepare_ui(play);
            if (!prepared_ok)
                failure = "play.stage.prepare_failed";
        } catch (const std::exception& e) {
            prepared_ok = false;
            failure = std::string("play.stage.exception: ") + e.what();
        } catch (...) {
            prepared_ok = false;
            failure = "play.stage.unknown_exception";
        }
        if (!prepared_ok) {
            reject_once(play, failure);
            return false;
        }
        state_ = StageState::Prepared;
    }
    if (state_ == StageState::Prepared) {
        if (staged_session_ != session || staged_ticket_ != ticket) {
            // Superseded before we finished preparing: release silently
            // (the runtime will produce a fresh envelope next frame).
            release_staged();
            return true;
        }
        // Keep the prepared renderer advancing; only block until both
        // mesh readiness and the staged UI preparation settle. advance
        // and resource checks are localized so a transient terminal
        // diagnostic or extraction exception becomes a one-shot
        // negative ack rather than pinning the candidate forever.
        try {
            advance_renderer(play);
        } catch (const std::exception& e) {
            reject_once(play, std::string("play.stage.advance.exception: ") + e.what());
            return false;
        } catch (...) {
            reject_once(play, "play.stage.advance.unknown_exception");
            return false;
        }
        if (!candidate_resources_ready()) {
            // pending(): keep waiting; no negative ack yet.
            if (staged_renderer_->pending())
                return false;
            // not pending but resources still not ready: terminal
            // diagnostic. Reject exactly once.
            std::string diagnostic;
            for (const auto& d : staged_renderer_->diagnostics()) {
                if (d.category == "render.camera.missing")
                    continue;
                if (d.severity == Severity::Error || d.severity == Severity::Fatal) {
                    diagnostic = d.category.empty() ? "render.failure" : d.category;
                    if (!d.text.empty()) {
                        diagnostic += ": ";
                        diagnostic += d.text;
                    }
                    break;
                }
            }
            if (staged_renderer_->omitted_diagnostics() != 0 && diagnostic.empty())
                diagnostic = "render.diagnostics.overflow";
            if (diagnostic.empty())
                diagnostic = "play.stage.not_ready";
            reject_once(play, diagnostic);
            return false;
        }
        if (staged_envelope_.contains("ui") && !staged_envelope_.at("ui").is_null() &&
            (staged_ui_ticket_ == 0 || !active_ui_->prepared_staged(staged_ui_ticket_)))
            return false;
        // Ship the positive prepared verdict; runtime lifts the gate
        // for that ticket.
        bool ack_ok = false;
        try {
            ack_ok = play.submit_sdk_candidate_ack(true, session, ticket);
        } catch (...) {
            ack_ok = false;
        }
        if (!ack_ok) {
            // Local verdict was rejected; keep staged for retry on the
            // next frame, but surface as failure so callers can react.
            return false;
        }
        state_ = StageState::Awaiting;
        return true;
    }
    return false;
}
} // namespace forge