#pragma once
#include "../asset_bytes.hpp"
#include "RmlUi_Platform_SDL.h"
#include "runtime_ui_input.hpp"
#include <forge/loading_state.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <set>
namespace forge {
// Editor adapter only. The presenter and Diligent backend have no dependency on this file.
class RuntimeUiHost {
    SDL_Window* window_;
    Diligent::IRenderDevice* device_;
    std::filesystem::path font_, project_;
    TextInputMethodEditor_SDL ime_; // Must outlive the presenter/context.
    std::unique_ptr<UiDiligentRenderer> renderer_;
    std::unique_ptr<UiPresenter> presenter_;
    std::string session_;
    std::uint64_t generation_ = 0, submitted_ = 0;
    // Staged UI bookkeeping. The host (not the presenter) tracks the
    // candidate (session, generation) so sync() can defer clearing
    // until the staged UI is committed or cancelled by the explicit
    // staged-UI state machine.
    std::uint64_t prepared_ticket_ = 0;
    std::string prepared_session_;
    std::uint64_t prepared_generation_ = 0;
    ImVec2 origin_{}, size_{};
    unsigned target_width_ = 1, target_height_ = 1;
    RuntimeUiInput input_;
    std::string error_;
    bool creation_failed_ = false, suspended_ = true;
    std::optional<UiAssetSnapshot> observed_assets_;
    void capture_assets() {
        if (const auto* snapshot = presenter_ ? presenter_->asset_snapshot() : nullptr)
            observed_assets_ = *snapshot;
    }
    // Shared presenter construction used by both sync() and the staged
    // prepare path. Single owner; identical platform callbacks; no
    // duplicate state in either path.
    void ensure_presenter() {
        if (presenter_)
            return;
        renderer_ = std::make_unique<UiDiligentRenderer>(device_);
        UiPlatformCallbacks platform;
        platform.get_clipboard = [] {
            char* text = SDL_GetClipboardText();
            std::string result = text ? text : "";
            SDL_free(text);
            return result;
        };
        platform.set_clipboard = [](const std::string& text) {
            SDL_SetClipboardText(text.c_str());
        };
        platform.activate_text = [this](float x, float y, float height) {
            const float sx = size_.x / target_width_, sy = size_.y / target_height_;
            SDL_Rect rect{int(origin_.x + x * sx), int(origin_.y + y * sy), 1,
                          std::max(1, int(height * sy))};
            SDL_SetTextInputArea(window_, &rect, 0);
            SDL_StartTextInput(window_);
        };
        platform.deactivate_text = [this] { SDL_StopTextInput(window_); };
        presenter_ = std::make_unique<UiPresenter>(renderer_->render_interface(), project_,
                                                   asset_detail::read_bytes(font_, 4 * 1024 * 1024),
                                                   std::move(platform), &ime_);
    }

  public:
    const UiAssetSnapshot* asset_snapshot() const {
        return observed_assets_ ? &*observed_assets_ : nullptr;
    }
    // Single source of truth for "has RmlUi Core been initialized by this
    // host yet?". Mirrors `set_game_input_observer`/`set_presenter_alive_observer`
    // in tests/editor_sdk_workflow.hpp: the workflow probes this before
    // calling Rml::GetNumContexts()/GetContext() because those accessors
    // dereference Rml::core_data which is null until the presenter's
    // Rml::Initialise() runs. No new RmlInit owner; the existing
    // RuntimeUiHost::presenter_ is the lifecycle owner.
    bool presenter_alive() const { return presenter_ != nullptr; }
    std::string diagnostic() const {
        return !error_.empty() ? error_ : presenter_ ? presenter_->diagnostic() : std::string{};
    }
    RuntimeUiHost(SDL_Window* w, Diligent::IRenderDevice* d, std::filesystem::path font)
        : window_(w), device_(d), font_(std::move(font)) {}
    void clear(PlaySession& play) {
        if (presenter_)
            presenter_->release_input();
        presenter_.reset();
        renderer_.reset();
        session_.clear();
        generation_ = submitted_ = 0;
        prepared_ticket_ = 0;
        prepared_session_.clear();
        prepared_generation_ = 0;
        creation_failed_ = false;
        input_.reset(play);
    }
    // Drop the live UI without touching prepared_*. Used by the staged
    // presentation commit when the accepted candidate publishes a null
    // UI snapshot. Preserves creation_failed_/project_/font_ so the
    // next candidate can prepare a fresh presenter.
    void clear_ui(PlaySession& play) {
        if (presenter_)
            presenter_->release_input();
        presenter_.reset();
        renderer_.reset();
        session_.clear();
        generation_ = submitted_ = 0;
        // prepared_* untouched on purpose.
        input_.reset(play);
    }
    // Set the project path used by ensure_presenter() when lazily
    // constructing the presenter for the staged UI path. Called by
    // PlayPresentation::poll() before prepare_staged so the very
    // first candidate binds to the correct project, not the empty
    // initial project_. A real project change tears down the old
    // presenter/renderer/staged bookkeeping/input via the existing
    // clear() (single owner; no parallel reset paths) and only then
    // installs the new path. The same-project transition is a no-op
    // so cached creation failure / observed assets / resource
    // previous_good stay intact. The global UiPresenter facilities
    // (renderer/platform callbacks) are reconstructed by the next
    // ensure_presenter(), never here.
    void set_project(const std::filesystem::path& project, PlaySession& play) {
        if (project_ == project)
            return;
        // Real boundary: release old owner, staged bookkeeping,
        // input routing, and retained diagnostic before installing
        // the new project path. clear() is the documented owner.
        clear(play);
        observed_assets_.reset();
        error_.clear();
        project_ = project;
        creation_failed_ = false;
    }
    void sync(PlaySession& play, const std::filesystem::path& project, bool visible) {
        if (project_ != project) {
            observed_assets_.reset();
        }
        // Forward the latest transport loading state into the presenter
        // so configured loading UI/progress works in Editor Play. The
        // existing presenter->loading validates against a fixed state
        // set and binds the documented forge_loading_* model fields,
        // so no new events are needed here.
        if (play.active())
            apply_loading(play.sdk_loading());
        if (!play.active()) {
            clear(play);
            return;
        }
        const bool suspend = !play.ready() || !visible || play.ui_snapshot().is_null();
        if (suspend) {
            if (!suspended_) {
                if (presenter_)
                    presenter_->release_input();
                input_.reset(play);
            }
            suspended_ = true;
            return;
        }
        suspended_ = false;
        const auto& state = play.ui_snapshot();
        const auto generation = state.at("generation").get<std::uint64_t>();
        // A pending staged UI is recorded on the host itself
        // (prepared_session_/prepared_generation_); do NOT clear/destroy
        // a just-accepted staged presenter merely because the live
        // snapshot generation ticked over. Cancellation is driven by
        // the explicit staged-UI state machine in PlayPresentation,
        // not by generation churn.
        if (session_ != play.session() || generation_ != generation || project_ != project) {
            if (prepared_session_ == play.session() && prepared_generation_ == generation)
                return;
            clear(play);
            error_.clear();
            project_ = project;
            session_ = play.session();
            generation_ = generation;
        }
        if (state.contains("errors") && !state.at("errors").empty())
            error_ = state.at("errors")[0].value("error", "UI asset unavailable");
        if (state.at("documents").empty() && !presenter_)
            return;
        try {
            if (!presenter_) {
                if (creation_failed_)
                    return;
                creation_failed_ = true;
                ensure_presenter();
                presenter_->reset(session_, generation_);
                submitted_ = 0;
                creation_failed_ = false;
            }
            const auto before = presenter_->presentation_revision();
            presenter_->accept(state);
            if (before != presenter_->presentation_revision()) {
                input_.reset(play);
                capture_assets();
            }
            if (!play.ui_ack().is_null())
                presenter_->acknowledge(play.ui_ack());
            if (auto command = presenter_->pending_command();
                command && command->at("id").get<std::uint64_t>() != submitted_)
                if (play.submit_ui(*command))
                    submitted_ = command->at("id").get<std::uint64_t>();
            // Reliable process pipe: no automatic retry. Timeout/crash discards the session;
            // uncertain commands are never replayed into a new generation.
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    bool event(const SDL_Event& e, GameInput& game, PlaySession& play) {
        try {
            return !suspended_ && play.ready() && presenter_ &&
                   input_.event(e, *presenter_, ime_, game, play);
        } catch (const std::exception& why) {
            error_ = why.what();
            input_.reset(play);
            if (presenter_)
                presenter_->release_input();
            return true;
        }
    }
    void draw(PlaySession& play, Diligent::IDeviceContext* context, Diligent::ITextureView* texture,
              ImVec2 origin, ImVec2 size, unsigned w, unsigned h) {
        origin_ = origin;
        size_ = size;
        target_width_ = w;
        target_height_ = h;
        input_.bounds(origin, size, w, h);
        if (!presenter_ || !play.ready())
            return;
        try {
            presenter_->update(
                double(SDL_GetTicksNS()) / 1e9, int(w), int(h),
                std::clamp(SDL_GetWindowDisplayScale(window_) * float(w) / std::max(1.f, size.x),
                           .5f, 4.f));
            renderer_->begin(context, texture, w, h);
            presenter_->render();
            renderer_->end();
        } catch (const std::exception& e) {
            renderer_->end();
            error_ = e.what();
        }
    }
    void controls(PlaySession& play) {
        const auto& loading = play.sdk_loading();
        const auto offered_ticket =
            loading.is_object() ? loading.value("ticket", std::uint64_t{}) : 0u;
        const bool can_cancel = loading.is_object() && loading.value("can_cancel", false);
        // Reachable Cancel button for ANY cancellable preparation,
        // including the initial loading/no-presenter branch. The
        // previous build only rendered Cancel inside the
        // presenter_-alive block, so a user clicking the SDK Start
        // button when no RML asset was registered could not cancel
        // their preparation. main also drains the actual RmlUi
        // cancel separately; this ImGui path is independent and
        // submits the offered ticket straight to the bounded
        // protocol command so an ImGui click cancels even when no
        // RmlUi cancel action ever fires. No play.ready() gate —
        // the cancel must remain reachable during initial loading
        // when no presenter has yet been created.
        if (offered_ticket != 0 && can_cancel &&
            ui::button("Cancel loading",
                       "Cancel the current SDK scene preparation. The active world stays. "
                       "A new candidate appears only when the runtime has another scene to "
                       "prepare.")) {
            play.submit_sdk_cancel_loading(offered_ticket);
        }
        if (!presenter_) {
            if (!error_.empty()) {
                ImGui::TextWrapped("Runtime UI: %s", error_.c_str());
                ui::help("Assign an available registered RML asset and restart Play.");
            }
            return;
        }
        ImGui::SameLine();
        if (ui::button("Reload UI",
                       "Validate updated RML/RCSS resources and replace the presentation only "
                       "after the candidate loads. A failed reload keeps the previous UI.")) {
            presenter_->release_input();
            input_.reset(play);
            error_.clear();
            if (presenter_->reload())
                capture_assets();
        }
        // The Reload UI Cancel branch is now obsolete: the unified
        // Cancel loading button above covers every cancellable
        // preparation, including the live presenter. We keep this
        // dead path removed (no consumed RmlUi action) so the next
        // reviewer cannot mistake a non-functional button for a
        // working one.
        const auto& why = error_.empty() ? presenter_->diagnostic() : error_;
        if (!why.empty()) {
            ImGui::TextWrapped("Runtime UI: %s", why.c_str());
            ui::help("Check the document and its project-relative resource paths, then use Reload "
                     "UI. Previous usable documents remain displayed after a failed replacement.");
        }
    }
    inline std::uint64_t prepare_staged(const Json& snapshot) {
        // Lazily create a presenter for the staged candidate. The
        // initial candidate scene always has a UI snapshot and the
        // sync() that would build the live presenter happens AFTER
        // PlayPresentation::poll(), so without this helper the very
        // first commit would throw "no presenter" and stall the
        // editor. ensure_presenter() reuses the same construction
        // block as sync() (single owner, identical callbacks).
        if (!presenter_) {
            if (creation_failed_)
                throw std::runtime_error(
                    "Runtime UI host presenter creation previously failed; cannot stage");
            creation_failed_ = true;
            ensure_presenter();
            if (!session_.empty())
                presenter_->reset(session_, generation_);
            creation_failed_ = false;
        }
        // Before prepare: drive the existing presenter update with a
        // bounded current/fallback target size and a sensible
        // density, so the staged DOM layout is sized off the live
        // viewport (or a safe fallback) rather than 1x1. prime
        // exceptions propagate; we convert them into a documented
        // runtime-ui.prepare.prime_failed diagnostic so the caller
        // (PlayPresentation) can turn them into a one-shot negative
        // verdict instead of silently accepting a broken primed
        // layout.
        try {
            prime_for_prepare();
        } catch (const std::exception& e) {
            error_ = std::string("runtime-ui.prepare.prime_failed: ") + e.what();
            throw;
        } catch (...) {
            error_ = "runtime-ui.prepare.prime_failed";
            throw;
        }
        const auto ticket = presenter_->prepare(snapshot);
        prepared_ticket_ = ticket;
        prepared_session_ = snapshot.value("session", session_);
        prepared_generation_ = snapshot.value("generation", std::uint64_t{});
        return ticket;
    }
    // Bounded presenter's existing update() call before prepare, so
    // the staged layout fits the live (or fallback) viewport size and
    // density. Reuses the same draw-time density formula; safe to
    // call with no output yet because target_width_/target_height_
    // are clamped to a sensible minimum here. Dimensions honor the
    // existing UiPresenter contract (positive ints, bounded above by
    // the documented 8192 ceiling); density is positive-finite and
    // clamped. Exceptions PROPAGATE so prepare_staged can convert
    // them into a PlayPresentation rejection instead of silently
    // recording an error and continuing.
    void prime_for_prepare() {
        if (!presenter_)
            return;
        constexpr int kPrimeMaxDim = 8192; // matches UiPresenter
        const int w = std::clamp<int>(std::max(target_width_ ? int(target_width_) : 1280, 64), 64,
                                      kPrimeMaxDim);
        const int h = std::clamp<int>(std::max(target_height_ ? int(target_height_) : 720, 64), 64,
                                      kPrimeMaxDim);
        const float sx = size_.x > 0.f ? size_.x : float(w);
        const float density = std::clamp((window_ ? SDL_GetWindowDisplayScale(window_) : 1.f) *
                                             float(w) / std::max(1.f, sx),
                                         .5f, 4.f);
        // Exceptions propagate: prepare_staged catches them and
        // converts them into a one-shot negative verdict. Swallowing
        // here would let prepare_staged still report success, leave
        // the staged UI unbound to the live viewport, and stall the
        // runtime activation gate on a broken primed layout.
        presenter_->update(double(SDL_GetTicksNS()) / 1e9, w, h, density);
    }
    inline bool prepared_staged(std::uint64_t ticket) const {
        return presenter_ && presenter_->prepared(ticket);
    }
    inline void activate_staged(std::uint64_t ticket) {
        if (!presenter_ || !presenter_->activate_prepared(ticket))
            throw std::runtime_error("Failed to activate prepared UI");
        // Promote the candidate session/generation into the host's
        // live bookkeeping so the next sync() accepts this UI as the
        // currently-published live one. submitted_ must reset so the
        // first command from the new generation (whose id may equal
        // the previous submitted_) is not skipped. Clear any stale
        // error from the previous generation and re-capture the asset
        // snapshot now that the staged resources are live.
        session_ = prepared_session_;
        generation_ = prepared_generation_;
        submitted_ = 0;
        error_.clear();
        capture_assets();
        prepared_ticket_ = 0;
        prepared_session_.clear();
        prepared_generation_ = 0;
    }
    inline void cancel_staged(std::uint64_t ticket) {
        // Cancel releases the staged GPU/DOM resources; the live
        // presenter remains untouched.
        if (presenter_)
            presenter_->cancel_prepared(ticket);
        if (prepared_ticket_ == ticket) {
            prepared_ticket_ = 0;
            prepared_session_.clear();
            prepared_generation_ = 0;
        }
    }
    inline void apply_loading(const Json& loading) {
        if (!presenter_)
            return;
        LoadingState value;
        if (loading.is_object()) {
            value.ticket = loading.value("ticket", std::uint64_t{});
            value.superseded_ticket = loading.value("superseded_ticket", std::uint64_t{});
            value.state = loading.value("state", std::string{"idle"});
            value.stage = loading.value("stage", std::string{});
            value.error_code = loading.value("error_code", std::string{});
            value.error = loading.value("error", std::string{});
            value.completed = loading.value("completed", std::size_t{});
            value.total = loading.value("total", std::size_t{});
            value.can_cancel = loading.value("can_cancel", false);
        }
        try {
            presenter_->loading(value);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    // Honest navigation: UiPresenter::navigate returns void and issues
    // key down/up internally. We surface the exact presenter's
    // diagnostic on exception so callers can build a bounded ack.
    // Reject suspended/no-live-document cases explicitly so the
    // caller never believes a focus change happened against an
    // empty / paused DOM. success means the navigation was
    // delivered to the live UI, not merely that focus might shift.
    inline void checked_navigate(const std::string& direction, std::string& failure) {
        failure.clear();
        if (!presenter_) {
            failure = "runtime-ui.navigate.no_presenter";
            return;
        }
        if (suspended_) {
            failure = "runtime-ui.navigate.suspended";
            return;
        }
        if (presenter_->document_count() == 0) {
            failure = "runtime-ui.navigate.no_live_documents";
            return;
        }
        try {
            presenter_->navigate(direction);
        } catch (const std::invalid_argument& e) {
            // Bounded diagnostic for a literal unknown direction.
            failure = std::string("runtime-ui.navigate.invalid_direction: ") + e.what();
        } catch (const std::exception& e) {
            failure = e.what();
        } catch (...) {
            failure = "runtime-ui.navigate.unknown_exception";
        }
    }
    // Take a pending loading cancel exactly matching the offered
    // ticket. Returns std::nullopt when no presenter has a pending
    // cancel, when the ticket does not match the offered ticket, or
    // when the loading record says can_cancel=false. The cancel is
    // caller-driven; the helper merely drains the offer so the
    // caller can issue a bounded cancel command. No new queue is
    // allocated here: the call is bounded to the presenter's own
    // take_loading_cancel and the strict ticket comparison.
    inline std::optional<std::uint64_t> take_loading_cancel_exact(std::uint64_t offered_ticket,
                                                                  bool can_cancel) {
        if (!presenter_ || !can_cancel || offered_ticket == 0)
            return std::nullopt;
        const auto cancel = presenter_->take_loading_cancel();
        if (!cancel || *cancel != offered_ticket)
            return std::nullopt;
        return cancel;
    }
};
} // namespace forge
