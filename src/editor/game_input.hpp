#pragma once
#include "../sdl_game_cursor.hpp"
#include "../sdl_gamepads.hpp"
#include "../sdl_input.hpp"
#include "play.hpp"
#include "widgets.hpp"
#include <array>
namespace forge {
// One active gamepad, keyboard and mouse. SDL is confined to this platform adapter.
class GameInput {
  public:
    explicit GameInput(SDL_Window* window = nullptr) {
        if (window)
            cursor_ = std::make_unique<SdlGameCursor>(window);
    }
    GameInput(const GameInput&) = delete;
    GameInput& operator=(const GameInput&) = delete;
    bool captured() const { return captured_; }
    bool relative() const { return captured_ && relative_capture_; }
    void viewport(ImVec2 origin, ImVec2 size) {
        origin_ = origin;
        size_ = size;
        bounded_ = true;
    }
    bool outside(float x, float y) const {
        return bounded_ && (x < origin_.x || y < origin_.y || x >= origin_.x + size_.x ||
                            y >= origin_.y + size_.y);
    }
    void capture(PlaySession& play, bool relative = false) {
        if (play.ready()) {
            try {
                if (cursor_) {
                    if (relative)
                        cursor_->capture();
                    else
                        cursor_->release();
                }
                capture_error_.clear();
            } catch (const std::exception& e) {
                capture_error_ = e.what();
                return;
            }
            play.input_event({{}, 0, true});
            captured_ = true;
            relative_capture_ = relative;
        }
    }
    void release(PlaySession& play) {
        if (cursor_)
            cursor_->release();
        if (captured_)
            play.input_event({{}, 0, true});
        captured_ = false;
        relative_capture_ = false;
    }
    void pump(PlaySession& play, bool allowed) {
        if (!allowed || !play.ready() || session_ != play.session())
            release(play);
        session_ = play.session();
        if (devices_dirty_) {
            devices_dirty_ = false;
            pads_.discover();
        }
    }
    // Returns true for input owned by gameplay. Window/lifecycle events still reach the UI.
    bool event(const SDL_Event& e, PlaySession& play) {
        const auto pad_events = pads_.event(e, captured_ && play.ready(), SDL_GetTicks());
        for (const auto& value : pad_events)
            play.input_event(value);
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            release(play);
            return false;
        }
        if (!pad_events.empty() && e.type != SDL_EVENT_GAMEPAD_REMOVED)
            return true;
        if (captured_ && !relative() && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            outside(e.button.x, e.button.y)) {
            release(play);
            return false;
        }
        if (!captured_ || !play.ready())
            return false;
        auto send = [&](std::string control, double value) {
            play.input_event({std::move(control), value, false});
        };
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                if (e.key.down)
                    release(play);
                return true;
            }
            if (e.key.scancode == SDL_SCANCODE_F6 || e.key.scancode == SDL_SCANCODE_F7) {
                if (e.key.down && !e.key.repeat) {
                    if (e.key.scancode == SDL_SCANCODE_F7)
                        play.step();
                    else if (play.paused())
                        play.resume();
                    else
                        play.pause();
                }
                return true;
            }
            if (!e.key.repeat) {
                const auto key = key_control(e.key.scancode);
                if (!key.empty())
                    send(key, e.key.down ? 1 : 0);
            }
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const char* name = e.button.button == SDL_BUTTON_LEFT     ? "left"
                               : e.button.button == SDL_BUTTON_RIGHT  ? "right"
                               : e.button.button == SDL_BUTTON_MIDDLE ? "middle"
                               : e.button.button == SDL_BUTTON_X1     ? "x1"
                               : e.button.button == SDL_BUTTON_X2     ? "x2"
                                                                      : nullptr;
            if (name)
                send(std::string("mouse.") + name, e.button.down ? 1 : 0);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            send("mouse.delta_x", e.motion.xrel);
            send("mouse.delta_y", e.motion.yrel);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            const double sign = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
            send("mouse.wheel_x", e.wheel.x * sign);
            send("mouse.wheel_y", e.wheel.y * sign);
            return true;
        }
        return e.type == SDL_EVENT_TEXT_INPUT || e.type == SDL_EVENT_TEXT_EDITING;
    }
    static std::string key_control(SDL_Scancode key) { return sdl_key_control(key); }
    void controls(PlaySession& play) {
        ImGui::BeginDisabled(captured_);
        ImGui::Checkbox("Relative mouse", &relative_mode_);
        FORGE_UI_PROBE("game-relative-mouse");
        ui::help("Hide and capture the mouse for gameplay look. Escape always releases it. "
                 "Turn this off to use a pointer with runtime menus. Release capture before "
                 "changing modes.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!play.ready());
        if (ui::button(captured_ ? "Release gameplay input (Esc)" : "Capture gameplay input",
                       "Send keyboard, mouse and the active gamepad to project actions. "
                       "Escape releases; F6 pauses/resumes and F7 steps. Editor navigation is "
                       "suspended during capture.")) {
            if (captured_)
                release(play);
            else {
                ImGui::ClearActiveID();
                ImGui::GetIO().ClearInputKeys();
                ImGui::GetIO().ClearInputMouse();
                capture(play, relative_mode_);
            }
        }
        ImGui::EndDisabled();
        if (!capture_error_.empty())
            ImGui::TextWrapped("%s", capture_error_.c_str());
        if (captured_) {
            ImGui::TextWrapped("GAME INPUT | Esc: release | F6: pause/resume | F7: step");
            ui::help(
                "Input snapshots advance only on fixed simulation ticks. Clicks are gameplay "
                "input inside Game. Escape, focus loss or a click outside Game releases capture.");
        }
    }

  private:
    ImVec2 origin_{}, size_{};
    bool bounded_ = false;
    SdlGamepads pads_;
    std::unique_ptr<SdlGameCursor> cursor_;
    std::string capture_error_;
    bool captured_ = false, devices_dirty_ = true, relative_mode_ = false,
         relative_capture_ = false;
    std::string session_;
};
inline void draw_input_monitor(const PlaySession& play) {
    if (!ImGui::CollapsingHeader("Gameplay input"))
        return;
    ui::help("The runtime's fixed-tick consumer. Counts retain short presses that happen between "
             "editor frames. Action labels have no hard-coded gameplay meaning.");
    const auto& status = play.input_status();
    ImGui::Text("Input tick: %llu",
                static_cast<unsigned long long>(status.value("tick", std::uint64_t{})));
    ui::help("This snapshot is immutable for the entire tick. Pause leaves it unchanged until Step "
             "or Resume.");
    if (status.contains("actions"))
        for (const auto& a : status.at("actions")) {
            ImGui::Text("%s: %s | %.3f, %.3f | presses %llu | releases %llu",
                        a.at("name").get_ref<const std::string&>().c_str(),
                        a.at("held").get<bool>() ? "held" : "up", a.at("x").get<double>(),
                        a.at("y").get<double>(),
                        static_cast<unsigned long long>(a.at("presses").get<std::uint64_t>()),
                        static_cast<unsigned long long>(a.at("releases").get<std::uint64_t>()));
            ui::help("Digital edges count once when consumed by a fixed tick; held persists. "
                     "Continuous axes persist; mouse deltas/wheel apply to one tick only.");
        }
}
} // namespace forge
