#pragma once
#include "../asset_bytes.hpp"
#include "RmlUi_Platform_SDL.h"
#include "runtime_ui_input.hpp"
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

  public:
    const UiAssetSnapshot* asset_snapshot() const {
        return observed_assets_ ? &*observed_assets_ : nullptr;
    }
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
        creation_failed_ = false;
        input_.reset(play);
    }
    void sync(PlaySession& play, const std::filesystem::path& project, bool visible) {
        if (project_ != project) {
            observed_assets_.reset();
        }
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
        if (session_ != play.session() || generation_ != generation || project_ != project) {
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
                presenter_ = std::make_unique<UiPresenter>(
                    renderer_->render_interface(), project_,
                    asset_detail::read_bytes(font_, 4 * 1024 * 1024), std::move(platform), &ime_);
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
        const auto& why = error_.empty() ? presenter_->diagnostic() : error_;
        if (!why.empty()) {
            ImGui::TextWrapped("Runtime UI: %s", why.c_str());
            ui::help("Check the document and its project-relative resource paths, then use Reload "
                     "UI. Previous usable documents remain displayed after a failed replacement.");
        }
    }
};
} // namespace forge
