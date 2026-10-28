#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/input.hpp>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

namespace forge {
// Platform device ownership only. Actions, contexts and bindings remain in
// RuntimeInput. SDL instance IDs are transient and never enter project/settings data.
class SdlGamepads {
  public:
    SdlGamepads() : namespace_(detail::uuid_text(detail::uuid_v4())) {}
    ~SdlGamepads() {
        stop_rumble();
        stop_sensors();
    }
    SdlGamepads(const SdlGamepads&) = delete;
    SdlGamepads& operator=(const SdlGamepads&) = delete;
    std::vector<InputEvent> discover() {
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (!ids) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad discovery failed: %s", SDL_GetError());
            return {};
        }
        std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> owner(ids, SDL_free);
        std::vector<InputEvent> events;
        for (int i = 0; i < count; ++i) {
            auto added = open(ids[i]);
            events.insert(events.end(), added.begin(), added.end());
        }
        return events;
    }
    SDL_JoystickID active() const { return active_; }
    std::size_t count() const { return pads_.size(); }
    const char* activity() const { return gamepad_activity_ ? "Gamepad" : "KeyboardMouse"; }
    GamepadTopology snapshot() const {
        GamepadTopology result;
        result.available = SDL_WasInit(SDL_INIT_GAMEPAD) != 0;
        result.revision = revision_;
        for (const auto& [id, pad] : pads_) {
            result.devices.push_back(
                {pad.token, pad.name, pad.slot, SDL_GamepadConnected(pad.handle.get()),
                 SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad.handle.get()),
                                        SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)});
            if (id == active_)
                result.active = pad.token;
        }
        result.validate();
        return result;
    }
    GamepadMotion motion(bool routed) const {
        GamepadMotion result;
        result.available = SDL_WasInit(SDL_INIT_GAMEPAD) != 0;
        result.routed = result.available && routed && motion_routed_;
        result.revision = revision_;
        for (const auto& [id, pad] : pads_) {
            GamepadMotionDevice device;
            device.device = pad.token;
            device.slot = pad.slot;
            device.connected = SDL_GamepadConnected(pad.handle.get());
            if (device.connected)
                for (std::size_t index = 0; index < 2; ++index) {
                    auto& sample = index ? device.gyroscope : device.accelerometer;
                    const auto type = sensor_type(index);
                    sample.supported = SDL_GamepadHasSensor(pad.handle.get(), type);
                    if (!sample.supported)
                        continue;
                    sample.enabled = SDL_GamepadSensorEnabled(pad.handle.get(), type);
                    const auto rate = SDL_GetGamepadSensorDataRate(pad.handle.get(), type);
                    sample.rate_hz = std::isfinite(rate) && rate >= 0 ? rate : 0;
                    const auto& reading = pad.motion[index];
                    if (result.routed && sample.enabled && reading.valid) {
                        sample.measured = true;
                        sample.timestamp_ns = reading.timestamp_ns;
                        sample.sensor_timestamp_ns = reading.sensor_timestamp_ns;
                        sample.value = reading.value;
                    }
                }
            result.devices.push_back(std::move(device));
            if (id == active_)
                result.active = pad.token;
        }
        result.validate();
        return result;
    }
    // Routing changes require a new native event; queued pre-capture samples
    // cannot reappear after a focus/modal/Stop or capture boundary.
    void route_motion(bool routed) noexcept {
        if (motion_routed_ == routed)
            return;
        motion_routed_ = routed;
        const auto now = SDL_GetTicksNS();
        for (auto& [id, pad] : pads_)
            reset_motion(pad, now);
    }
    void configure_sensor(const GamepadSensorConfig& value) {
        value.validate();
        auto& pad = connection(value.revision, value.device);
        const auto index = std::size_t(value.sensor);
        const auto type = sensor_type(index);
        if (!SDL_GamepadHasSensor(pad.handle.get(), type)) {
            if (!value.enabled)
                return;
            throw std::runtime_error("The selected gamepad does not support this motion sensor");
        }
        if (SDL_GamepadSensorEnabled(pad.handle.get(), type) == value.enabled)
            return;
        revision_room();
        if (!SDL_SetGamepadSensorEnabled(pad.handle.get(), type, value.enabled))
            throw std::runtime_error(std::string("Gamepad sensor configuration failed: ") +
                                     SDL_GetError());
        if (SDL_GamepadSensorEnabled(pad.handle.get(), type) != value.enabled)
            throw std::runtime_error("The native gamepad sensor mode did not match the request");
        pad.motion[index] = {};
        pad.motion[index].since_ns = SDL_GetTicksNS();
        pad.sensor_stop_failed[index] = false;
        ++revision_;
    }
    // One owned topology edit. Prepare neutral values before native output or
    // routing changes; failed Stop leaves assignments unchanged and retryable.
    std::vector<InputEvent> assign(const GamepadAssignment& value) {
        value.validate();
        auto& selected = connection(value.revision, value.device);
        if (selected.slot == value.slot)
            return {};
        revision_room();
        std::vector<InputEvent> events;
        if (selected.slot)
            append_neutral(events, selected.slot);
        if (value.slot)
            append_neutral(events, value.slot);
        auto occupied =
            value.slot
                ? std::find_if(pads_.begin(), pads_.end(),
                               [&](const auto& row) { return row.second.slot == value.slot; })
                : pads_.end();
        if (!stop_pad(selected) || (occupied != pads_.end() && !stop_pad(occupied->second)))
            throw std::runtime_error("Gamepad feedback could not stop; assignment was retained");
        const auto now = SDL_GetTicksNS();
        reset_motion(selected, now);
        if (occupied != pads_.end())
            reset_motion(occupied->second, now);
        if (occupied != pads_.end())
            occupied->second.slot = 0;
        selected.slot = value.slot;
        ++revision_;
        return events;
    }
    void rumble(const GamepadRumble& value) {
        value.validate();
        if (value.low == 0 && value.high == 0) {
            const bool ok = value.slot ? stop_slot(value.slot) : stop_rumble();
            if (!ok)
                throw std::runtime_error("Gamepad feedback could not stop");
            return;
        }
        auto target =
            value.slot
                ? std::find_if(pads_.begin(), pads_.end(),
                               [&](const auto& row) { return row.second.slot == value.slot; })
                : pads_.find(active_);
        if (target == pads_.end() || !SDL_GamepadConnected(target->second.handle.get()))
            throw std::runtime_error(value.slot ? "No connected gamepad is assigned to this slot"
                                                : "No active connected gamepad for rumble");
        auto& pad = target->second;
        if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad.handle.get()),
                                    SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false))
            throw std::runtime_error("The selected gamepad does not support rumble");
        if (!value.slot)
            for (auto& [id, old] : pads_)
                if (id != active_ && old.feedback && old.feedback_slot == 0 && !stop_pad(old))
                    throw std::runtime_error("Previous active gamepad feedback could not stop");
        if (!SDL_RumbleGamepad(pad.handle.get(), Uint16(std::lround(value.low * 65535)),
                               Uint16(std::lround(value.high * 65535)), value.duration_ms))
            throw std::runtime_error(std::string("Gamepad rumble failed: ") + SDL_GetError());
        pad.feedback = true;
        pad.feedback_slot = value.slot;
        pad.stop_failed = false;
    }
    bool stop_rumble() noexcept {
        bool ok = true;
        for (auto& [id, pad] : pads_)
            ok = stop_pad(pad) && ok;
        return ok;
    }
    // Lifecycle always progresses. Indexed slots route independently; the
    // legacy active alias alone retains the deliberate 250ms switch guard.
    std::vector<InputEvent> event(const SDL_Event& event, bool accept, Uint64 now) {
        route_motion(accept);
        if (accept &&
            ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) ||
             event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             (event.type == SDL_EVENT_MOUSE_MOTION &&
              std::hypot(event.motion.xrel, event.motion.yrel) >= 2) ||
             (event.type == SDL_EVENT_MOUSE_WHEEL && (event.wheel.x != 0 || event.wheel.y != 0))))
            activity(false, now);
        if (event.type == SDL_EVENT_GAMEPAD_ADDED)
            return open(event.gdevice.which);
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            auto found = pads_.find(event.gdevice.which);
            if (found == pads_.end())
                return {};
            revision_room();
            std::vector<InputEvent> events;
            if (found->second.slot)
                append_neutral(events, found->second.slot);
            const bool selected = active_ == event.gdevice.which;
            if (selected)
                append_neutral(events, 0);
            stop_pad(found->second);
            stop_sensors(found->second);
            pads_.erase(found);
            limit_warned_ = false;
            ++revision_;
            std::clog << "Gamepad disconnected: " << event.gdevice.which << '\n';
            if (selected) {
                active_ = pads_.empty() ? 0 : pads_.begin()->first;
                switched_ = now;
                if (!active_) {
                    gamepad_activity_ = false;
                    activity_time_ = now;
                }
            }
            return events;
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
            auto found = pads_.find(event.gdevice.which);
            if (found == pads_.end())
                return {};
            revision_room();
            std::vector<InputEvent> events;
            if (found->second.slot)
                append_neutral(events, found->second.slot);
            if (active_ == event.gdevice.which)
                append_neutral(events, 0);
            found->second.name = name(found->second.handle.get());
            stop_pad(found->second);
            reset_motion(found->second, SDL_GetTicksNS());
            ++revision_;
            return events;
        }
        if (event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE) {
            const auto found = pads_.find(event.gsensor.which);
            if (!accept || found == pads_.end() ||
                !SDL_GamepadConnected(found->second.handle.get()))
                return {};
            const auto type = SDL_SensorType(event.gsensor.sensor);
            if (type != SDL_SENSOR_ACCEL && type != SDL_SENSOR_GYRO)
                return {};
            auto& pad = found->second;
            const auto index = type == SDL_SENSOR_ACCEL ? 0u : 1u;
            auto& sample = pad.motion[index];
            if (!SDL_GamepadSensorEnabled(pad.handle.get(), type) ||
                event.gsensor.timestamp <= sample.since_ns ||
                event.gsensor.timestamp < sample.timestamp_ns)
                return {};
            if (!event.gsensor.timestamp ||
                !std::all_of(std::begin(event.gsensor.data), std::end(event.gsensor.data),
                             [](float axis) { return std::isfinite(axis); })) {
                const auto since = sample.since_ns;
                sample = {};
                sample.since_ns = since;
                if (!pad.sensor_invalid_warned[index])
                    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Invalid native gamepad motion sample");
                pad.sensor_invalid_warned[index] = true;
                return {};
            }
            sample.valid = true;
            sample.timestamp_ns = event.gsensor.timestamp;
            sample.sensor_timestamp_ns = event.gsensor.sensor_timestamp;
            std::copy(std::begin(event.gsensor.data), std::end(event.gsensor.data),
                      sample.value.begin());
            return {};
        }
        if (!accept)
            return {};
        SDL_JoystickID id = 0;
        const char* leaf = nullptr;
        double sample = 0;
        bool meaningful = false;
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            if (event.gbutton.button >= buttons_.size())
                return {};
            id = event.gbutton.which;
            leaf = buttons_[event.gbutton.button];
            sample = event.gbutton.down ? 1.0 : 0.0;
            meaningful = event.gbutton.down;
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            if (event.gaxis.axis >= axes_.size())
                return {};
            id = event.gaxis.which;
            leaf = axes_[event.gaxis.axis];
            sample =
                event.gaxis.value < 0 ? event.gaxis.value / 32768.0 : event.gaxis.value / 32767.0;
            if (event.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                sample = std::max(0.0, sample);
            meaningful = std::abs(sample) >= .5;
        } else
            return {};
        const auto found = pads_.find(id);
        if (found == pads_.end() || !SDL_GamepadConnected(found->second.handle.get()))
            return {};
        std::vector<InputEvent> result;
        const bool legacy =
            id == active_ || (meaningful && now >= switched_ && now - switched_ >= 250);
        if (legacy) {
            if (id != active_) {
                append_neutral(result, 0);
                if (const auto old = pads_.find(active_);
                    old != pads_.end() && old->second.feedback_slot == 0)
                    stop_pad(old->second);
                active_ = id;
                switched_ = now;
                std::clog << "Active gamepad changed: " << id << '\n';
            }
            result.push_back({gamepad_control_id(0, leaf), sample});
        }
        if (found->second.slot)
            result.push_back({gamepad_control_id(found->second.slot, leaf), sample});
        if (std::abs(sample) >= .25)
            activity(true, now);
        return result;
    }

  private:
    struct Close {
        void operator()(SDL_Gamepad* pad) const { SDL_CloseGamepad(pad); }
    };
    struct Pad {
        std::unique_ptr<SDL_Gamepad, Close> handle;
        std::string token, name;
        std::uint32_t slot = 0;
        // Output may still need a physical Stop even after its requested expiry.
        bool feedback = false, stop_failed = false;
        std::uint32_t feedback_slot = 0;
        struct Reading {
            bool valid = false;
            Uint64 since_ns = 0, timestamp_ns = 0, sensor_timestamp_ns = 0;
            std::array<double, 3> value{};
        };
        std::array<Reading, 2> motion{};
        std::array<bool, 2> sensor_stop_failed{}, sensor_invalid_warned{};
    };
    static SDL_SensorType sensor_type(std::size_t index) {
        return index ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL;
    }
    static void reset_motion(Pad& pad, Uint64 now) noexcept {
        for (auto& sample : pad.motion) {
            sample = {};
            sample.since_ns = now;
        }
    }
    Pad& connection(std::uint64_t revision, const std::string& token) {
        if (revision != revision_)
            throw std::runtime_error("Gamepad devices changed; refresh and try again");
        auto found = std::find_if(pads_.begin(), pads_.end(),
                                  [&](const auto& row) { return row.second.token == token; });
        if (found == pads_.end() || !SDL_GamepadConnected(found->second.handle.get()))
            throw std::runtime_error("The selected gamepad connection is no longer available");
        return found->second;
    }
    static void stop_sensors(Pad& pad) noexcept {
        if (!SDL_GamepadConnected(pad.handle.get()))
            return;
        for (std::size_t index = 0; index < 2; ++index)
            if (SDL_GamepadSensorEnabled(pad.handle.get(), sensor_type(index)) &&
                !SDL_SetGamepadSensorEnabled(pad.handle.get(), sensor_type(index), false)) {
                if (!pad.sensor_stop_failed[index])
                    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad sensor disable failed: %s",
                                SDL_GetError());
                pad.sensor_stop_failed[index] = true;
            }
    }
    void stop_sensors() noexcept {
        for (auto& [id, pad] : pads_)
            stop_sensors(pad);
    }
    static void append_neutral(std::vector<InputEvent>& events, std::uint32_t slot) {
        for (const auto* leaf : buttons_)
            events.push_back({gamepad_control_id(slot, leaf), 0});
        for (const auto* leaf : axes_)
            events.push_back({gamepad_control_id(slot, leaf), 0});
    }
    static std::string name(SDL_Gamepad* pad) {
        char text[257]{};
        if (const auto* raw = SDL_GetGamepadName(pad))
            SDL_strlcpy(text, raw, sizeof(text));
        std::string result(text);
        for (char& c : result)
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 127)
                c = ' ';
        try {
            (void)nlohmann::json(result).dump();
        } catch (const nlohmann::json::exception&) {
            result.clear();
        }
        return result.empty() ? "Gamepad" : result;
    }
    bool stop_pad(Pad& pad) noexcept {
        if (pad.feedback && SDL_GamepadConnected(pad.handle.get()) &&
            !SDL_RumbleGamepad(pad.handle.get(), 0, 0, 0)) {
            if (!pad.stop_failed)
                SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad rumble stop failed: %s",
                            SDL_GetError());
            pad.stop_failed = true;
            return false;
        }
        pad.feedback = false;
        pad.stop_failed = false;
        return true;
    }
    bool stop_slot(std::uint32_t slot) noexcept {
        for (auto& [id, pad] : pads_)
            if (pad.slot == slot)
                return stop_pad(pad);
        return true;
    }
    void revision_room() const {
        if (revision_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Gamepad topology revision exhausted");
    }
    std::vector<InputEvent> open(SDL_JoystickID id) {
        if (pads_.contains(id))
            return {};
        if (pads_.size() >= gamepad_device_limit) {
            if (!limit_warned_)
                SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "FORGE tracks at most 16 connected gamepads");
            limit_warned_ = true;
            return {};
        }
        revision_room();
        if (connections_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Gamepad connection token space exhausted");
        Pad pad;
        pad.handle.reset(SDL_OpenGamepad(id));
        if (!pad.handle) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Gamepad open failed: %s", SDL_GetError());
            return {};
        }
        pad.name = name(pad.handle.get());
        reset_motion(pad, SDL_GetTicksNS());
        pad.token = namespace_ + ":" + std::to_string(id) + ":" + std::to_string(connections_ + 1);
        for (std::uint32_t slot = 1; slot <= gamepad_slot_limit; ++slot)
            if (std::none_of(pads_.begin(), pads_.end(),
                             [&](const auto& row) { return row.second.slot == slot; })) {
                pad.slot = slot;
                break;
            }
        std::vector<InputEvent> events;
        if (pad.slot)
            append_neutral(events, pad.slot);
        if (!active_)
            append_neutral(events, 0);
        pads_.emplace(id, std::move(pad));
        ++connections_;
        ++revision_;
        if (!active_)
            active_ = id;
        std::clog << "Gamepad connected: " << id << '\n';
        return events;
    }
    std::map<SDL_JoystickID, Pad> pads_;
    std::string namespace_;
    std::uint64_t revision_ = 0, connections_ = 0;
    SDL_JoystickID active_ = 0;
    bool limit_warned_ = false;
    Uint64 switched_ = 0;
    bool gamepad_activity_ = false, activity_seen_ = false;
    bool motion_routed_ = false;
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
