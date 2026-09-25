#pragma once
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"
#include "frame_renderer.hpp"
#include "mesh_render_host.hpp"
#include "play.hpp"
#include "presentation_diligent.hpp"
#include "runtime_ui_host.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace forge {
// Narrow editor adapter for staged Editor Play renderer/UI. Holds one
// prepared FrameRenderer + UiPresenter pair keyed to a single
// (session, ticket) candidate. The shared MeshResourceHost is supplied
// by main; the adapter never owns it. Activation is gated by matching
// sdk_activation_generation_ == prepared ticket AND sdk_activation_active_;
// the swap is atomic from the editor's perspective. The active scene
// and UI remain intact during candidate preparation and on
// rejection/supersession/stop.
class PlayPresentation {
  public:
    PlayPresentation() = default;
    PlayPresentation(const PlayPresentation&) = delete;
    PlayPresentation& operator=(const PlayPresentation&) = delete;

    // The adapter takes ownership of the live renderer via this slot.
    // Main passes its initial unique_ptr; the adapter updates the slot
    // each time it swaps a staged renderer in. The slot pointer itself
    // remains stable so any code holding it (e.g. main's
    // `game_viewport` alias) sees the post-swap renderer.
    void adopt_active_renderer(FrameRenderer** slot, std::unique_ptr<FrameRenderer> renderer) {
        renderer_slot_ = slot;
        active_renderer_ = std::move(renderer);
        if (renderer_slot_)
            *renderer_slot_ = active_renderer_.get();
    }
    void set_active_ui(RuntimeUiHost* ui) { active_ui_ = ui; }
    void set_context(Diligent::IDeviceContext* context) { context_ = context; }
    void set_presentation(DiligentPresentation* presentation) { presentation_ = presentation; }
    void set_mesh_resource_host(std::shared_ptr<MeshResourceHost> host) { host_ = std::move(host); }
    FrameRenderer* active_renderer() const { return renderer_slot_ ? *renderer_slot_ : nullptr; }

    // Poll once per editor frame. Returns true if a stage swap
    // happened, a verdict was submitted, or staged owners were
    // released.
    bool poll(PlaySession& play, const std::filesystem::path& project);

    // Discard staged owners (used on Stop / project change / restart).
    void clear(PlaySession& play);

    bool has_staged() const { return state_ != StageState::Idle; }
    std::uint64_t staged_ticket() const { return staged_ticket_; }
    const std::string& staged_session() const { return staged_session_; }
    // One-shot diagnostic describing a fatal adapter failure (e.g. UI
    // activate refused by the live presenter). Main drains this once
    // per frame to surface it on the existing Problems / status path.
    // Empty string means "no failure surfaced this frame".
    std::string take_commit_failure() {
        auto out = std::move(pending_failure_);
        pending_failure_.clear();
        return out;
    }

  private:
    enum class StageState { Idle, Prepared, Awaiting };
    void release_staged();
    bool prepare_renderer(PlaySession& play);
    bool prepare_ui(PlaySession& play);
    void advance_renderer(PlaySession& play);
    bool candidate_resources_ready() const;
    // Atomic swap from staged owners into the live slot. Returns
    // true when a swap actually occurred.
    bool commit(PlaySession& play);
    // Mark the candidate rejected exactly once. Returns true when a
    // verdict was successfully shipped on this call. Never fabricates
    // a positive ack and never lies about a transport refusal.
    bool reject_once(PlaySession& play, const std::string& diagnostic);

    Diligent::IDeviceContext* context_ = nullptr;
    RuntimeUiHost* active_ui_ = nullptr;
    DiligentPresentation* presentation_ = nullptr;
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer** renderer_slot_ = nullptr;
    std::unique_ptr<FrameRenderer> active_renderer_;
    // Staged pair.
    std::unique_ptr<FrameRenderer> staged_renderer_;
    nlohmann::json staged_envelope_;
    std::string staged_session_;
    std::uint64_t staged_ticket_ = 0;
    std::uint64_t staged_ui_ticket_ = 0;
    StageState state_ = StageState::Idle;
    // Last successful (session, ticket) the adapter committed; used to
    // skip a stale identical envelope arriving again while the next
    // candidate is being fetched.
    std::string last_committed_session_;
    std::uint64_t last_committed_ticket_ = 0;
    // Last rejected (session, ticket) verdict shipped on this
    // connection; while the runtime keeps offering the same envelope
    // we do not re-stage it. Cleared when a different candidate
    // arrives.
    std::string last_rejected_session_;
    std::uint64_t last_rejected_ticket_ = 0;
    std::uint64_t last_seen_generation_ = 0;
    // Cached snapshot of play.sdk_activation_generation() for use in
    // commit() without re-reading through PlaySession's surface.
    std::uint64_t cached_activation_generation_ = 0;
    // One-shot fatal adapter diagnostic drained by main via
    // take_commit_failure(). Never set on the normal prepare/commit
    // path; only on UI-activate refusal or a stop() driven by the
    // adapter.
    std::string pending_failure_;
};
} // namespace forge