#pragma once
#include "RmlUi_Platform_SDL.h"
#include "game_input.hpp"
#include <forge/ui_presenter.hpp>
#include <set>
namespace forge {
// SDL routing policy, independent of the graphics backend. RmlUi always receives
// a matching release for a UI-owned press, even when focus/hover changes.
class RuntimeUiInput {
    ImVec2 origin_{}, size_{};
    unsigned target_width_ = 1, target_height_ = 1;
    std::set<SDL_Scancode> ui_keys_;
    std::set<Uint8> ui_buttons_;
    bool captured_ = false;
    bool inside(float x, float y) const {
        return x >= origin_.x && y >= origin_.y && x < origin_.x + size_.x &&
               y < origin_.y + size_.y;
    }
    void mouse(UiPresenter& presenter, float x, float y, int mods) {
        presenter.mouse_move(int((x - origin_.x) * target_width_ / std::max(1.f, size_.x)),
                             int((y - origin_.y) * target_height_ / std::max(1.f, size_.y)), mods);
    }
    static void neutralize(PlaySession& play) { play.input_event({{}, 0, true}); }

  public:
    void bounds(ImVec2 origin, ImVec2 size, unsigned w, unsigned h) {
        origin_ = origin;
        size_ = size;
        target_width_ = w;
        target_height_ = h;
    }
    void reset(PlaySession& play) {
        neutralize(play);
        ui_keys_.clear();
        ui_buttons_.clear();
        captured_ = false;
    }
    bool event(const SDL_Event& e, UiPresenter& presenter, TextInputMethodEditor_SDL& ime,
               GameInput& game, PlaySession& play) {

        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            presenter.release_input();
            ui_keys_.clear();
            ui_buttons_.clear();
            neutralize(play);
            return false;
        }
        if (!game.captured()) {
            if (captured_) {
                presenter.release_input();
                ui_keys_.clear();
                ui_buttons_.clear();
            }
            captured_ = false;
            return false;
        }
        if (game.relative()) {
            if (captured_) {
                presenter.release_input();
                ui_keys_.clear();
                ui_buttons_.clear();
                captured_ = false;
            }
            return game.event(e, play);
        }
        captured_ = true;
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
            (e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_F6 ||
             e.key.scancode == SDL_SCANCODE_F7)) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                presenter.release_input();
                ui_keys_.clear();
                ui_buttons_.clear();
            }
            return game.event(e, play);
        }
        const auto modifiers = RmlSDL::GetKeyModifierState();
        bool consumed = false;
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            consumed = presenter.mouse_move(
                           int((e.motion.x - origin_.x) * target_width_ / std::max(1.f, size_.x)),
                           int((e.motion.y - origin_.y) * target_height_ / std::max(1.f, size_.y)),
                           modifiers) ||
                       !ui_buttons_.empty();
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (e.button.down && !inside(e.button.x, e.button.y)) {
                presenter.release_input();
                game.release(play);
                ui_keys_.clear();
                ui_buttons_.clear();
                return false;
            }
            mouse(presenter, e.button.x, e.button.y, modifiers);
            const int button = RmlSDL::ConvertMouseButton(e.button.button);
            if (button >= 0)
                consumed = presenter.mouse_button(button, e.button.down, modifiers);
            if (e.button.down && consumed)
                ui_buttons_.insert(e.button.button);
            else if (!e.button.down)
                consumed = ui_buttons_.erase(e.button.button) || consumed;
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            const float sign = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1.f : -1.f;
            consumed = presenter.wheel(e.wheel.x * sign, e.wheel.y * sign, modifiers);
        } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            consumed = presenter.key(RmlSDL::ConvertKey(int(e.key.key)), e.key.down, modifiers);
            if (e.key.down && consumed)
                ui_keys_.insert(e.key.scancode);
            else if (!e.key.down)
                consumed = ui_keys_.erase(e.key.scancode) || consumed;
        } else if (e.type == SDL_EVENT_TEXT_INPUT) {
            presenter.text(e.text.text);
            consumed = true;
        } else if (e.type == SDL_EVENT_TEXT_EDITING) {
            if (presenter.wants_text() && std::strlen(e.edit.text) <= 1024)
                ime.HandleEdit(e.edit);
            consumed = true;
        }
        if (consumed && e.type != SDL_EVENT_MOUSE_MOTION)
            neutralize(play);
        return consumed;
    }
};
} // namespace forge
