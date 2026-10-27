#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/input.hpp>
#include <optional>

namespace forge {
inline bool sdl_finger_event(const SDL_Event& event) {
    return event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
           event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
}
inline bool sdl_touch_mouse_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION)
        return event.motion.which == SDL_TOUCH_MOUSEID;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return event.button.which == SDL_TOUCH_MOUSEID;
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
        return event.wheel.which == SDL_TOUCH_MOUSEID;
    return false;
}
// Main-thread SDL ownership only. Slots and physical IDs are transient; semantic
// actions, validation, contexts and tick edges remain owned by RuntimeInput.
class SdlTouches {
  public:
    struct Viewport {
        double x = 0, y = 0, width = 0, height = 0;
        bool operator==(const Viewport&) const = default;
    };
    explicit SdlTouches(SDL_Window* window = nullptr) : window_(window) {}
    void reset() { contacts_ = {}; }
    std::vector<InputEvent> synchronize(bool accept, std::optional<Viewport> viewport = {}) {
        int width = 0, height = 0;
        const auto window_id = window_ ? SDL_GetWindowID(window_) : 0;
        const bool available =
            window_id && SDL_GetWindowSize(window_, &width, &height) && width > 0 && height > 0;
        const auto area = viewport.value_or(Viewport{0, 0, double(width), double(height)});
        const bool owned = accept && available && std::isfinite(area.x) && std::isfinite(area.y) &&
                           std::isfinite(area.width) && std::isfinite(area.height) &&
                           area.width > 0 && area.height > 0;
        std::vector<InputEvent> result;
        if (!owned || !owned_ || window_id != window_id_ || width != width_ || height != height_ ||
            area != area_)
            for (std::size_t slot = 0; slot < contacts_.size(); ++slot)
                if (contacts_[slot]) {
                    append(result, slot, false, 0, 0, 0);
                    contacts_[slot].reset();
                }
        owned_ = owned;
        window_id_ = window_id;
        width_ = width;
        height_ = height;
        area_ = area;
        return result;
    }
    bool owns(const SDL_Event& event) const {
        return owned_ && sdl_finger_event(event) && event.tfinger.windowID == window_id_;
    }
    std::vector<InputEvent> event(const SDL_Event& event, bool accept,
                                  std::optional<Viewport> viewport = {}) {
        auto result = synchronize(accept, viewport);
        if (!owns(event) || event.tfinger.touchID == SDL_MOUSE_TOUCHID ||
            event.tfinger.touchID == SDL_PEN_TOUCHID)
            return result;
        const auto& finger = event.tfinger;
        const Contact key{finger.touchID, finger.fingerID};
        auto found = std::find(contacts_.begin(), contacts_.end(), std::optional<Contact>(key));
        const bool ending =
            event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
        if (ending) {
            if (found != contacts_.end()) {
                append(result, std::size_t(found - contacts_.begin()), false, 0, 0, 0);
                found->reset();
            }
            return result;
        }
        if (!std::isfinite(finger.x) || !std::isfinite(finger.y) ||
            !std::isfinite(finger.pressure) || finger.x < 0 || finger.x > 1 || finger.y < 0 ||
            finger.y > 1 || finger.pressure < 0 || finger.pressure > 1)
            return result;
        const double x = (double(finger.x) * width_ - area_.x) / area_.width;
        const double y = (double(finger.y) * height_ - area_.y) / area_.height;
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (found != contacts_.end() || x < 0 || x >= 1 || y < 0 || y >= 1)
                return result;
            found = std::find(contacts_.begin(), contacts_.end(), std::nullopt);
            if (found == contacts_.end())
                return result;
            *found = key;
        } else if (found == contacts_.end())
            return result; // No late join after capture loss, overflow or an outside start.
        append(result, std::size_t(found - contacts_.begin()), true, std::clamp(x, 0., 1.),
               std::clamp(y, 0., 1.), finger.pressure);
        return result;
    }

  private:
    using Contact = std::pair<SDL_TouchID, SDL_FingerID>;
    static void append(std::vector<InputEvent>& result, std::size_t slot, bool down, double x,
                       double y, double pressure) {
        const auto prefix = "touch." + std::to_string(slot) + ".";
        // A down precedes coordinates so rebinding captures the contact button.
        result.push_back({prefix + "down", down ? 1. : 0.});
        result.push_back({prefix + "x", x});
        result.push_back({prefix + "y", y});
        result.push_back({prefix + "pressure", pressure});
    }
    SDL_Window* window_ = nullptr; // Borrowed; host destroys this adapter first.
    SDL_WindowID window_id_ = 0;
    int width_ = 0, height_ = 0;
    Viewport area_;
    bool owned_ = false;
    std::array<std::optional<Contact>, touch_contact_limit> contacts_{};
};
} // namespace forge
