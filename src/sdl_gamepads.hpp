#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/input.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

namespace forge {
// Platform device ownership only. Actions, contexts and bindings remain in
// RuntimeInput. SDL instance IDs are transient and never enter project/settings data.
class SdlGamepads {
  public:
    SdlGamepads() = default;
    ~SdlGamepads() { stop_rumble(); }
    SdlGamepads(const SdlGamepads&) = delete;
    SdlGamepads& operator=(const SdlGamepads&) = delete;
    void discover() {
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (!ids) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad discovery failed: %s", SDL_GetError());
            return;
        }
        std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> owner(ids, SDL_free);
        for (int i = 0; i < count; ++i)
            open(ids[i]);
    }
    SDL_JoystickID active() const { return active_; }
    std::size_t count() const { return pads_.size(); }
    const char* activity() const { return gamepad_activity_ ? "Gamepad" : "KeyboardMouse"; }
    void rumble(const GamepadRumble& value) {
        value.validate();
        if (value.low == 0 && value.high == 0) {
            if (!stop_rumble())
                throw std::runtime_error(SDL_GetError());
            return;
        }
        const auto pad = pads_.find(active_);
        if (pad == pads_.end() || !SDL_GamepadConnected(pad->second.get()))
            throw std::runtime_error("No active connected gamepad for rumble");
        if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad->second.get()),
                                    SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false))
            throw std::runtime_error("The active gamepad does not support rumble");
        if (rumbling_ && rumbling_ != active_ && !stop_rumble())
            throw std::runtime_error(SDL_GetError());
        if (!SDL_RumbleGamepad(pad->second.get(), Uint16(std::lround(value.low * 65535)),
                               Uint16(std::lround(value.high * 65535)), value.duration_ms))
            throw std::runtime_error(std::string("Gamepad rumble failed: ") + SDL_GetError());
        rumbling_ = active_;
        stop_failed_ = false;
    }
    bool stop_rumble() noexcept {
        if (const auto pad = pads_.find(rumbling_);
            pad != pads_.end() && SDL_GamepadConnected(pad->second.get()) &&
            !SDL_RumbleGamepad(pad->second.get(), 0, 0, 0)) {
            if (!stop_failed_)
                SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad rumble stop failed: %s",
                            SDL_GetError());
            stop_failed_ = true;
            return false;
        }
        rumbling_ = 0;
        stop_failed_ = false;
        return true;
    }
    // Main thread. Lifecycle is processed even when gameplay capture is released.
    // Another pad takes over only after deliberate input and a 250ms switch guard.
    std::vector<InputEvent> event(const SDL_Event& event, bool accept, Uint64 now) {
        if (accept &&
            ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) ||
             event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             (event.type == SDL_EVENT_MOUSE_MOTION &&
              std::hypot(event.motion.xrel, event.motion.yrel) >= 2) ||
             (event.type == SDL_EVENT_MOUSE_WHEEL && (event.wheel.x != 0 || event.wheel.y != 0))))
            activity(false, now);
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            open(event.gdevice.which);
            return {};
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            const bool selected = active_ == event.gdevice.which;
            if (rumbling_ == event.gdevice.which)
                stop_rumble();
            pads_.erase(event.gdevice.which);
            std::clog << "Gamepad disconnected: " << event.gdevice.which << '\n';
            if (selected) {
                active_ = pads_.empty() ? 0 : pads_.begin()->first;
                switched_ = now;
                if (!active_) {
                    gamepad_activity_ = false;
                    activity_time_ = now;
                }
                return {{{}, 0, true}};
            }
            return {};
        }
        if (!accept)
            return {};
        SDL_JoystickID id = 0;
        InputEvent value;
        bool meaningful = false;
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            if (event.gbutton.button >= buttons_.size())
                return {};
            id = event.gbutton.which;
            value = {std::string("pad.") + buttons_[event.gbutton.button],
                     event.gbutton.down ? 1.0 : 0.0};
            meaningful = event.gbutton.down;
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            if (event.gaxis.axis >= axes_.size())
                return {};
            id = event.gaxis.which;
            double normalized =
                event.gaxis.value < 0 ? event.gaxis.value / 32768.0 : event.gaxis.value / 32767.0;
            if (event.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                normalized = std::max(0.0, normalized);
            value = {std::string("pad.") + axes_[event.gaxis.axis], normalized};
            meaningful = std::abs(normalized) >= .5;
        } else {
            return {};
        }
        const auto pad = pads_.find(id);
        if (pad == pads_.end() || !SDL_GamepadConnected(pad->second.get()))
            return {};
        std::vector<InputEvent> result;
        if (id != active_) {
            if (!meaningful || now < switched_ || now - switched_ < 250)
                return {};
            stop_rumble();
            active_ = id;
            switched_ = now;
            std::clog << "Active gamepad changed: " << id << '\n';
            result.push_back({{}, 0, true});
        }
        if (std::abs(value.value) >= .25)
            activity(true, now);
        result.push_back(std::move(value));
        return result;
    }

  private:
    struct Close {
        void operator()(SDL_Gamepad* pad) const { SDL_CloseGamepad(pad); }
    };
    void open(SDL_JoystickID id) {
        if (pads_.contains(id))
            return;
        std::unique_ptr<SDL_Gamepad, Close> pad(SDL_OpenGamepad(id));
        if (!pad) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad open failed: %s", SDL_GetError());
            return;
        }
        pads_.emplace(id, std::move(pad));
        std::clog << "Gamepad connected: " << id << '\n';
        if (!active_)
            active_ = id;
    }
    std::map<SDL_JoystickID, std::unique_ptr<SDL_Gamepad, Close>> pads_;
    SDL_JoystickID active_ = 0;
    SDL_JoystickID rumbling_ = 0;
    bool stop_failed_ = false;
    Uint64 switched_ = 0;
    bool gamepad_activity_ = false, activity_seen_ = false;
    Uint64 activity_time_ = 0;
    void activity(bool gamepad, Uint64 now) {
        if (activity_seen_ && gamepad_activity_ != gamepad &&
            (now < activity_time_ || now - activity_time_ < 250))
            return;
        if (gamepad_activity_ != gamepad || !activity_seen_)
            activity_time_ = now;
        gamepad_activity_ = gamepad;
        activity_seen_ = true;
    }
    static constexpr std::array<const char*, 15> buttons_{
        "south",          "east",  "west",       "north",       "back",
        "guide",          "start", "left_stick", "right_stick", "left_shoulder",
        "right_shoulder", "up",    "down",       "left",        "right"};
    static constexpr std::array<const char*, 6> axes_{"left_x",  "left_y",       "right_x",
                                                      "right_y", "left_trigger", "right_trigger"};
};
} // namespace forge
