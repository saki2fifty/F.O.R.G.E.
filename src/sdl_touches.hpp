#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/input.hpp>
#include <numbers>
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
    void reset() {
        contacts_ = {};
        gesture_pair_.reset();
        cancel_single();
    }
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
            area != area_) {
            cancel_single();
            for (std::size_t slot = 0; slot < contacts_.size(); ++slot)
                if (contacts_[slot]) {
                    append(result, slot, false, 0, 0, 0);
                    contacts_[slot].reset();
                }
        }
        owned_ = owned;
        window_id_ = window_id;
        width_ = width;
        height_ = height;
        area_ = area;
        update_gesture(result);
        return result;
    }
    // Call after draining queued SDL events, not before a possibly delayed UP.
    std::vector<InputEvent> advance() {
        if (!single_ || single_->long_press || single_->excursion > 8)
            return {};
        return advance(SDL_GetTicksNS());
    }
    std::vector<InputEvent> advance(Uint64 now) {
        std::vector<InputEvent> result;
        update_hold(result, now);
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
        const auto key = std::pair{finger.touchID, finger.fingerID};
        auto found = std::find_if(contacts_.begin(), contacts_.end(), [&](const auto& contact) {
            return contact && contact->key == key;
        });
        const bool ending =
            event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
        const bool valid = std::isfinite(finger.x) && std::isfinite(finger.y) &&
                           std::isfinite(finger.pressure) && finger.x >= 0 && finger.x <= 1 &&
                           finger.y >= 0 && finger.y <= 1 && finger.pressure >= 0 &&
                           finger.pressure <= 1;
        const double x = valid ? (double(finger.x) * width_ - area_.x) / area_.width : 0;
        const double y = valid ? (double(finger.y) * height_ - area_.y) / area_.height : 0;
        const auto now = finger.timestamp;
        if (ending) {
            if (found != contacts_.end()) {
                if (event.type == SDL_EVENT_FINGER_UP && valid) {
                    move_single(result, key, std::clamp(x, 0., 1.), std::clamp(y, 0., 1.), now);
                    finish_single(result, key, now);
                } else
                    cancel_single();
                append(result, std::size_t(found - contacts_.begin()), false, 0, 0, 0);
                found->reset();
                update_gesture(result);
            }
            return result;
        }
        if (!valid) {
            if (single_ && single_->key == key)
                cancel_single();
            return result;
        }
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (found != contacts_.end() || x < 0 || x >= 1 || y < 0 || y >= 1)
                return result;
            found = std::find(contacts_.begin(), contacts_.end(), std::nullopt);
            if (found == contacts_.end())
                return result;
            *found = Contact{key};
        } else if (found == contacts_.end())
            return result; // No late join after capture loss, overflow or an outside start.
        (*found)->x = std::clamp(x, 0., 1.);
        (*found)->y = std::clamp(y, 0., 1.);
        append(result, std::size_t(found - contacts_.begin()), true, (*found)->x, (*found)->y,
               finger.pressure);
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (std::count_if(contacts_.begin(), contacts_.end(),
                              [](const auto& item) { return item.has_value(); }) == 1)
                single_ =
                    Single{key, (*found)->x * area_.width, (*found)->y * area_.height, now, now};
            else
                cancel_single();
        } else
            move_single(result, key, (*found)->x, (*found)->y, now);
        update_gesture(result);
        return result;
    }

  private:
    struct Contact {
        std::pair<SDL_TouchID, SDL_FingerID> key;
        double x = 0, y = 0;
    };
    struct Single {
        std::pair<SDL_TouchID, SDL_FingerID> key;
        double start_x = 0, start_y = 0;
        Uint64 start = 0, last_time = 0;
        double x = 0, y = 0, excursion = 0;
        bool long_press = false;
    };
    struct Tap {
        SDL_TouchID device;
        double x, y;
        Uint64 time;
    };
    void cancel_single() {
        single_.reset();
        last_tap_.reset();
    }
    static void pulse(std::vector<InputEvent>& result, const char* name) {
        const auto control = std::string("touch.gesture.") + name;
        result.push_back({control, 1});
        result.push_back({control, 0});
    }
    void update_hold(std::vector<InputEvent>& result, Uint64 now) {
        if (!single_)
            return;
        if (now < single_->last_time) {
            cancel_single();
            return;
        }
        auto& single = *single_;
        single.last_time = now;
        if (!single.long_press && single.excursion <= 8 && now - single.start >= 500000000) {
            single.long_press = true;
            last_tap_.reset();
            pulse(result, "long_press");
        }
    }
    void move_single(std::vector<InputEvent>& result,
                     const std::pair<SDL_TouchID, SDL_FingerID>& key, double x, double y,
                     Uint64 now) {
        if (!single_ || single_->key != key)
            return;
        if (now < single_->last_time) {
            cancel_single();
            return;
        }
        auto& single = *single_;
        single.last_time = now;
        single.x = x * area_.width - single.start_x;
        single.y = y * area_.height - single.start_y;
        single.excursion = std::max(single.excursion, std::hypot(single.x, single.y));
        update_hold(result, now);
    }
    void finish_single(std::vector<InputEvent>& result,
                       const std::pair<SDL_TouchID, SDL_FingerID>& key, Uint64 now) {
        if (!single_ || single_->key != key)
            return;
        const auto single = *single_;
        single_.reset();
        if (single.long_press)
            return;
        const auto elapsed = now - single.start;
        if (elapsed <= 250000000 && single.excursion <= 8) {
            pulse(result, "tap");
            const double x = single.start_x + single.x, y = single.start_y + single.y;
            if (last_tap_ && last_tap_->device == key.first && now >= last_tap_->time &&
                now - last_tap_->time <= 350000000 &&
                std::hypot(x - last_tap_->x, y - last_tap_->y) <= 24) {
                pulse(result, "double_tap");
                last_tap_.reset(); // A third tap starts a fresh pair.
            } else
                last_tap_ = Tap{key.first, x, y, now};
        } else {
            last_tap_.reset();
            if (elapsed <= 750000000 && std::hypot(single.x, single.y) >= 64)
                pulse(result, std::abs(single.x) >= std::abs(single.y)
                                  ? (single.x < 0 ? "swipe_left" : "swipe_right")
                                  : (single.y < 0 ? "swipe_up" : "swipe_down"));
        }
    }
    void update_gesture(std::vector<InputEvent>& result) {
        const bool was_active = gesture_pair_.has_value();
        const bool retained =
            gesture_pair_ && contacts_[(*gesture_pair_)[0]] && contacts_[(*gesture_pair_)[1]];
        if (!retained) {
            gesture_pair_.reset();
            std::array<std::size_t, 2> selected{};
            unsigned count = 0;
            for (std::size_t i = 0; i < contacts_.size() && count < selected.size(); ++i)
                if (contacts_[i])
                    selected[count++] = i;
            if (count == selected.size())
                gesture_pair_ = selected;
        }
        if (was_active != gesture_pair_.has_value())
            result.push_back({"touch.gesture.active", gesture_pair_ ? 1. : 0.});
        if (!gesture_pair_)
            return;
        const auto& first = *contacts_[(*gesture_pair_)[0]];
        const auto& second = *contacts_[(*gesture_pair_)[1]];
        const std::array center{(first.x + second.x) / 2, (first.y + second.y) / 2};
        const std::array vector{(second.x - first.x) * area_.width,
                                (second.y - first.y) * area_.height};
        if (retained) {
            const auto emit = [&](const char* field, double value) {
                if (value != 0 && std::isfinite(value))
                    result.push_back({std::string("touch.gesture.") + field, value});
            };
            emit("pan_x", center[0] - gesture_center_[0]);
            emit("pan_y", center[1] - gesture_center_[1]);
            const auto before = std::hypot(gesture_vector_[0], gesture_vector_[1]);
            const auto after = std::hypot(vector[0], vector[1]);
            // Coincident/subpixel contacts have no useful scale or angle. Rebase
            // once separated; never divide by distance or invent a first jump.
            if (before >= 1 && after >= 1 && std::isfinite(before) && std::isfinite(after)) {
                emit("pinch", std::log(after) - std::log(before));
                emit("rotation",
                     std::remainder(std::atan2(vector[1], vector[0]) -
                                        std::atan2(gesture_vector_[1], gesture_vector_[0]),
                                    2 * std::numbers::pi));
            }
        }
        gesture_center_ = center;
        gesture_vector_ = vector;
    }
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
    std::optional<Single> single_;
    std::optional<Tap> last_tap_;
    std::optional<std::array<std::size_t, 2>> gesture_pair_;
    std::array<double, 2> gesture_center_{}, gesture_vector_{};
};
} // namespace forge
