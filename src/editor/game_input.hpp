#pragma once
#include "../sdl_game_cursor.hpp"
#include "../sdl_gamepads.hpp"
#include "../sdl_input.hpp"
#include "../sdl_touches.hpp"
#include "play.hpp"
#include "widgets.hpp"
#include <array>
#include <source_location>
namespace forge {
// Independent logical gamepad slots, a legacy active alias, keyboard/mouse and bounded
// captured touch contacts. SDL stays in this platform adapter.
//
// SdlGameCursor owns the observed SDL_GetWindowRelativeMouseMode
// flag. captured() / relative() reflect that observed flag (or
// false when no cursor is owned; that is the headless menu-input
// path used by tests).
class GameInput {
  public:
    explicit GameInput(SDL_Window* window = nullptr) : touches_(window) {
        if (window)
            cursor_ = std::make_unique<SdlGameCursor>(window);
    }
    GameInput(const GameInput&) = delete;
    GameInput& operator=(const GameInput&) = delete;
    bool captured() const { return captured_; }
    bool relative() const { return cursor_ ? cursor_->relative() : false; }
    const std::string& capture_error() const { return capture_error_; }
    void viewport(ImVec2 origin, ImVec2 size) {
        origin_ = origin;
        size_ = size;
        bounded_ = true;
    }
    bool outside(float x, float y) const {
        return bounded_ && (x < origin_.x || y < origin_.y || x >= origin_.x + size_.x ||
                            y >= origin_.y + size_.y);
    }
    // Legacy UI-button capture. Delegates to capture_checked so the
    // checked vs. unchecked paths share one implementation.
    void capture(PlaySession& play, bool relative_mode = false) {
        capture_checked(play, relative_mode);
    }
    // Narrow truthful capture used by SDK platform-effect paths. Returns
    // true iff the observed SDL_GetWindowRelativeMouseMode flag agrees
    // with the request after the setter ran. The headless (no-cursor)
    // case cannot satisfy a
    // relative capture request, but a non-relative request is allowed
    // for menu routing: there is no physical mode to release, so the
    // logical routing can settle to its non-relative form.
    bool capture_checked(PlaySession& play, bool relative_mode) {
        if (!play.ready())
            return false;
        if (!cursor_) {
            if (relative_mode) {
                capture_error_ = "play.input.capture_failed: no window to capture";
                return false;
            }
            // No physical target; treat as a successful non-relative
            // capture so menu routing can settle. The local routing
            // flag flips to match the request.
            if (!captured_ || relative_capture_) {
                touches_.reset();
                play.input_event({{}, 0, true});
            }
            captured_ = true;
            relative_capture_ = false;
            observe_input_reset(play);
            pads_.route_motion(true);
            capture_error_.clear();
            return true;
        }
        try {
            if (relative_mode)
                cursor_->capture();
            else if (!cursor_->checked_release()) {
                capture_error_ = "play.input.release_failed: physical "
                                 "relative mode still on";
                return false;
            }
        } catch (const std::exception& e) {
            capture_error_ = e.what();
            return false;
        }
        const bool physically_relative = cursor_->relative();
        const bool ok = (physically_relative == relative_mode);
        if (!ok) {
            capture_error_ = relative_mode ? std::string("play.input.capture_failed: "
                                                         "physical relative mode is off")
                                           : std::string("play.input.release_failed: "
                                                         "physical relative mode is on");
            return false;
        }
        if (!captured_ || relative_capture_ != relative_mode) {
            touches_.reset();
            play.input_event({{}, 0, true});
        }
        captured_ = true;
        relative_capture_ = relative_mode;
        observe_input_reset(play);
        pads_.route_motion(true);
        capture_error_.clear();
        return true;
    }
    // Legacy void release. Always tears routing down (focus / Stop).
    void release(PlaySession& play) { release(play, false); }
    // Logical menu routing stays alive when keep_routing=true and only
    // the physical cursor is freed. Headless (no cursor) clears routing
    // when keep_routing=false; retains it otherwise.
    void release(PlaySession& play, bool keep_routing) {
        release_motion(play);
        touches_.reset();
        stop_rumble();
        if (cursor_) {
            try {
                cursor_->checked_release();
            } catch (const std::exception& e) {
                capture_error_ = e.what();
            }
        }
        if (captured_)
            play.input_event({{}, 0, true});
        if (!keep_routing) {
            captured_ = false;
            relative_capture_ = false;
        } else {
            relative_capture_ = false;
        }
    }
    // Narrow truthful release used by SDK platform-effect paths. keep_routing
    // retains logical routing after a successful physical release (Escape).
    // Returns true iff the physical mode is off. The neutral event is
    // sent on every successful physical release whenever routing was
    // captured, BEFORE any subsequent gameplay edge (the Escape
    // down/up pair). The keep_routing flag controls only whether the
    // logical captured flag is retained for menu routing.
    bool release_checked(PlaySession& play, bool keep_routing = false) {
        release_motion(play);
        touches_.reset();
        stop_rumble();
        if (!cursor_) {
            // No physical target: the release "succeeds" because
            // there is no window mode to clear, so logical routing
            // follows the keep_routing flag. A neutral edge is still
            // shipped whenever routing was previously captured so
            // headless tests / tests with no cursor still neutralize
            // held keys before later gameplay edges.
            if (captured_)
                play.input_event({{}, 0, true});
            if (!keep_routing) {
                captured_ = false;
                relative_capture_ = false;
            } else {
                relative_capture_ = false;
            }
            capture_error_.clear();
            return true;
        }
        const bool ok = cursor_->checked_release();
        // Always ship the neutral edge BEFORE returning, even on
        // setter failure, so the runtime neutralizes the previously-
        // held action regardless of the SDL setter outcome. A failed
        // setter must still revoke logical routing when the caller
        // asks for it (keep_routing=false) — the OS already owns the
        // physical capture, so leaving captured_=true would lie to the
        // runtime guard and stall its release-required epoch.
        if (captured_)
            play.input_event({{}, 0, true});
        if (!ok) {
            capture_error_ = std::string("play.input.release_failed: physical relative "
                                         "mode still on");
            if (!keep_routing) {
                captured_ = false;
                relative_capture_ = false;
            } else {
                relative_capture_ = false;
            }
            return false;
        }
        if (!keep_routing) {
            captured_ = false;
            relative_capture_ = false;
        } else {
            relative_capture_ = false;
        }
        capture_error_.clear();
        return true;
    }
    void pump(PlaySession& play, bool allowed) {
        const bool was_captured = captured_;
        const auto prior_session = session_;
        if (!allowed || !play.ready() || session_ != play.session()) {
            // Transition into "not owned" — clear logical routing
            // and, when this is the SDK profile, publish one fresh
            // observed epoch so the runtime guard advances. Not on
            // every disallowed frame: the previous frame already
            // settled into captured_=false on the prior transition.
            //
            // ONE setter call per transition. SDK profile routes
            // through the centralized checked helper (which already
            // performs the physical release, ships the neutral edge,
            // and queues the observation). Legacy profile falls back
            // to the existing release(play) void path. Calling both
            // here would double the SDL setter and could let the
            // first failure mask a successful second attempt.
            const bool sdk = play.sdk_play();
            if (sdk && (was_captured || prior_session != play.session()))
                submit_external_release_observation(play);
            else if (!sdk && was_captured)
                release(play);
        }
        session_ = play.session();
        if (motion_generation_ != play.world_generation()) {
            pads_.route_motion(false);
            motion_generation_ = play.world_generation();
        }
        observe_input_reset(play);
        for (const auto& value : touches_.synchronize(captured_ && play.ready(), touch_viewport()))
            play.input_event(value);
        if (devices_dirty_) {
            devices_dirty_ = false;
            for (const auto& value : pads_.discover())
                play.input_event(value);
        }
        play.observe_gamepads(pads_.snapshot());
        pads_.route_motion(captured_ && play.ready());
        play.observe_gamepad_motion(gamepad_motion(play));
    }
    void finish_events(PlaySession& play, std::optional<Uint64> now = {}) {
        observe_input_reset(play);
        for (const auto& value : now ? touches_.advance(*now) : touches_.advance())
            play.input_event(value);
        play.observe_gamepads(pads_.snapshot());
        play.observe_gamepad_motion(gamepad_motion(play));
    }
    // Returns true for input owned by gameplay. Window/lifecycle events still reach the UI.
    bool event(const SDL_Event& e, PlaySession& play) {
        observe_input_reset(play);
        const auto pad_events = pads_.event(e, captured_ && play.ready(), SDL_GetTicks());
        for (const auto& value : pad_events)
            play.input_event(value);
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            // Focus loss is an external revocation — clear logical
            // routing (it is no longer ours to own) AND publish a
            // fresh observed epoch so the runtime's release-guard
            // advances. The observed epoch uses the LIVE SDL
            // relative-mode probe; the setter result is reflected
            // separately via capture_error_. Not sent on every
            // disallowed frame — only on actual transition events
            // (focus loss, hide / minimize), which is what the
            // runtime needs to honor the release.
            //
            // ONE setter call per transition. SDK profile uses the
            // centralized checked helper; legacy profile uses the
            // existing release(play) void path. The helper itself
            // ships the neutral edge and queues the observation, so
            // the second setter call would only repeat work and
            // could conceal the first failure.
            const bool sdk = play.sdk_play();
            if (sdk)
                submit_external_release_observation(play);
            else
                release(play);
            return false;
        }
        if (!pad_events.empty() && e.type != SDL_EVENT_GAMEPAD_REMOVED)
            return true;
        for (const auto& value : touches_.event(e, captured_ && play.ready(), touch_viewport()))
            play.input_event(value);
        if (touches_.owns(e) || (captured_ && sdl_touch_mouse_event(e)))
            return true;
        if (captured_ && !relative() && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            outside(e.button.x, e.button.y)) {
            // Outside-game click is an external SDK revocation:
            // route through the same one-setter path the focus /
            // hide transitions use so the runtime sees exactly one
            // observation per physical change. Legacy profile keeps
            // the simple release() path. Calling both would double
            // the SDL setter and could mask the first failure.
            const bool sdk = play.sdk_play();
            if (sdk)
                submit_external_release_observation(play);
            else
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
                // SDK and legacy Escape are explicitly branched. SDK
                // Escape ships a neutral edge + observed epoch BEFORE
                // the gameplay down/up pair so the runtime's
                // release-guard sees the actual physical state, not a
                // guessed one. Menu routing is retained
                // (keep_routing=true) per the documented bound.
                if (e.key.down && !e.key.repeat) {
                    if (play.sdk_play()) {
                        if (!submit_external_release_observation(play, true))
                            return true;
                        send(key_control(SDL_SCANCODE_ESCAPE), 1.0);
                        send(key_control(SDL_SCANCODE_ESCAPE), 0.0);
                    } else {
                        release(play);
                    }
                }
                return true;
            }
            if (e.key.scancode == SDL_SCANCODE_F6 || e.key.scancode == SDL_SCANCODE_F7) {
                // Physical release + observed epoch precede the
                // runtime control command even when logical routing is
                // already cleared — a failed physical release is
                // surfaced through capture_error_ and the command is
                // refused. No duplicate release attempt: routing may
                // be cleared despite a failed setter, but the
                // observed epoch always reflects the LIVE SDL probe.
                if (e.key.down && !e.key.repeat) {
                    if (play.sdk_play()) {
                        if (!submit_external_release_observation(play)) {
                            capture_error_ = "Could not physically release cursor; "
                                             "Pause / Step refused";
                            return true;
                        }
                    } else if (captured_) {
                        release(play);
                    }
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
    // Centralized release + neutral + observation path. Used by every
    // externally-requested release (SDK Escape, F6/F7, focus loss,
    // external revocation / hide, toolbar Pause/Step) so the runtime
    // sees exactly ONE observation per physical change. The captured
    // / input_device fields reflect the OBSERVED
    // SDL_GetWindowRelativeMouseMode flag (see SdlGameCursor). A
    // failed setter can still leave the observed flag off, in which
    // case we publish
    // "captured=false" and surface the operation result through
    // capture_error_. Returns true when the physical release
    // succeeded; callers that REQUIRE a successful release before
    // issuing a runtime command (Pause / Step) gate on this. Cursor
    // platform-effect success acks use checked_release only and do
    // NOT bump the epoch — the adapter owns its own ack channel and
    // a successful cursor effect does not change the editor_epoch
    // the runtime is using.
    bool submit_external_release_observation(
        PlaySession& play, bool keep_routing = false,
        const std::source_location location = std::source_location::current()) {
#ifdef FORGE_UI_FIXTURE
        static unsigned logged = 0;
        if (logged++ < 64) {
            std::fprintf(stderr,
                         "FORGE input release caller=%s:%u ready=%d routing=%d relative=%d "
                         "epoch=%llu keep=%d\n",
                         location.function_name(), location.line(), int(play.ready()),
                         int(captured_), int(relative()),
                         static_cast<unsigned long long>(play.current_effective_epoch()),
                         int(keep_routing));
        }
#else
        (void)location;
#endif
        const bool released = release_checked(play, keep_routing);
        // Always publish the ACTUAL SDL relative-mode state, even when
        // the setter failed. The runtime needs to know whether the OS
        // probe says relative-mode is on or off; lying about it would
        // silently keep the runtime guard waiting on an epoch that
        // never matches reality.
        const bool physical_relative = relative();
        // Input device: real gamepad activity takes precedence over
        // the keyboard/mouse probe (an idle gamepad stays on
        // KeyboardMouse). Falls back to EditorHost when no device is
        // active so the runtime validator never receives an unknown
        // string.
        const std::string device = observed_input_device();
        // Failure path still ships the observation so the runtime's
        // guard can advance; success path ships it for the same
        // reason. Either way the queue is bounded to one per call.
        const bool queued = play.submit_editor_observation(physical_relative, device);
        (void)queued;
        return released;
    }
    // The runtime's documented enum (KeyboardMouse / Gamepad /
    // EditorHost). Source of truth is existing
    // SdlGamepads::activity() — no new input manager, no relative-
    // mode guess. Returns the pad's literal string when a gamepad is
    // active ("Gamepad"), "KeyboardMouse" otherwise. EditorHost is
    // never fabricated from the cursor state; callers that need the
    // EditorHost signal must set it explicitly.
    std::string observed_input_device() const {
        if (const auto* pad = pads_.activity())
            return std::string{pad};
        return "KeyboardMouse";
    }
    void rumble(const GamepadRumble& value) {
        if ((value.low != 0 || value.high != 0) && !captured_)
            throw std::runtime_error("Gamepad rumble requires routed gameplay input");
        pads_.rumble(value);
    }
    void stop_rumble() noexcept { pads_.stop_rumble(); }
    GamepadTopology gamepads() const { return pads_.snapshot(); }
    GamepadMotion gamepad_motion(const PlaySession& play) const {
        return pads_.motion(captured_ && play.ready());
    }
    GamepadMotion configure_gamepad_sensor(PlaySession& play, const GamepadSensorConfig& value) {
        pads_.configure_sensor(value);
        play.observe_gamepads(pads_.snapshot());
        auto result = gamepad_motion(play);
        play.observe_gamepad_motion(result);
        return result;
    }
    GamepadTopology assign_gamepad(PlaySession& play, const GamepadAssignment& value) {
        // Reassigning at most two 21-control namespaces must not overflow and
        // trigger the queue's global emergency reset after a physical edit.
        if (play.ready() && !play.input_queue_room(42))
            throw std::runtime_error("Wait for the pending input queue before assigning gamepads");
        for (const auto& event : pads_.assign(value))
            play.input_event(event);
        auto result = pads_.snapshot();
        play.observe_gamepads(result);
        play.observe_gamepad_motion(gamepad_motion(play));
        return result;
    }
    void controls(PlaySession& play) {
        ImGui::BeginDisabled(captured_);
        ImGui::Checkbox("Relative mouse", &relative_mode_);
        FORGE_UI_PROBE("game-relative-mouse");
        ui::help("Hide and capture the mouse for gameplay look. Escape always releases it. "
                 "Turn this off to use a pointer with runtime menus. Release capture before "
                 "changing modes.");
        ImGui::EndDisabled();
        ui::next_text_button("Gamepads...");
        if (ImGui::Button("Gamepads..."))
            ImGui::OpenPopup("Gamepad assignments");
        FORGE_UI_PROBE("button:Gamepads");
        const auto extent = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSizeConstraints({0, 0},
                                            {std::max(100.f, extent.x - 2 * ImGui::GetFontSize()),
                                             std::max(100.f, extent.y - 2 * ImGui::GetFontSize())});
        if (ImGui::BeginPopup("Gamepad assignments")) {
            // Explicit wrap width avoids auto-size/TextWrapped feedback that
            // otherwise produces a narrow, screen-height popup on first open.
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 28 * ImGui::GetFontSize());
            ImGui::TextUnformatted("Local gamepads");
            ImGui::TextWrapped("Assign controllers to Player 1..%zu for indexed pad controls. "
                               "Assignments last for this editor session.",
                               gamepad_slot_limit);
            static const auto slot_labels = [] {
                std::string labels = "Unassigned";
                for (std::size_t index = 1; index <= gamepad_slot_limit; ++index) {
                    labels.push_back('\0');
                    labels += "Player " + std::to_string(index);
                }
                labels.push_back('\0');
                return labels;
            }();
            const auto devices = gamepads();
            if (devices.devices.empty())
                ImGui::TextUnformatted("No connected gamepads.");
            for (const auto& device : devices.devices) {
                ImGui::PushID(device.device.c_str());
                ImGui::TextWrapped("%s", device.name.c_str());
                int slot = int(device.slot);
                ImGui::BeginDisabled(!device.connected);
                ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
                if (ImGui::Combo("Slot", &slot, slot_labels.c_str())) {
                    try {
                        assign_gamepad(play,
                                       {devices.revision, device.device, std::uint32_t(slot)});
                        assignment_error_.clear();
                    } catch (const std::exception& error) {
                        assignment_error_ = error.what();
                    }
                }
                FORGE_UI_PROBE("gamepad:slot:" + device.device);
                const auto motion = gamepad_motion(play);
                const auto found =
                    std::find_if(motion.devices.begin(), motion.devices.end(),
                                 [&](const auto& row) { return row.device == device.device; });
                const bool show_motion = ImGui::TreeNode("Motion sensors");
                FORGE_UI_PROBE("gamepad:motion:" + device.device);
                if (show_motion) {
                    ImGui::TextWrapped(
                        "Motion sensors default off. Enabling is temporary. Raw data "
                        "requires captured gameplay input; capture again after closing this menu.");
                    if (found != motion.devices.end())
                        for (auto kind :
                             {GamepadSensorKind::Accelerometer, GamepadSensorKind::Gyroscope}) {
                            const auto& sensor = kind == GamepadSensorKind::Accelerometer
                                                     ? found->accelerometer
                                                     : found->gyroscope;
                            const auto label = std::string(gamepad_sensor_name(kind));
                            bool enabled = sensor.enabled;
                            ImGui::BeginDisabled(!sensor.supported);
                            const auto* title = kind == GamepadSensorKind::Accelerometer
                                                    ? "Accelerometer"
                                                    : "Gyroscope";
                            if (ImGui::Checkbox(title, &enabled)) {
                                try {
                                    configure_gamepad_sensor(
                                        play, {gamepads().revision, device.device, kind, enabled});
                                    assignment_error_.clear();
                                } catch (const std::exception& error) {
                                    assignment_error_ = error.what();
                                }
                            }
                            FORGE_UI_PROBE("gamepad:sensor:" + label + ":" + device.device);
                            ImGui::EndDisabled();
                            if (!sensor.supported)
                                ImGui::TextUnformatted("Not supported by this controller.");
                            else if (sensor.rate_hz > 0)
                                ImGui::Text("Reported rate: %.1f Hz", sensor.rate_hz);
                            else
                                ImGui::TextUnformatted("Reported rate: unknown");
                        }
                    ImGui::TreePop();
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            if (!assignment_error_.empty())
                ImGui::TextWrapped("%s", assignment_error_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        ImGui::BeginDisabled(!play.ready());
        if (ui::button(captured_ ? "Release gameplay input (Esc)" : "Capture gameplay input",
                       "Send keyboard, mouse and assigned gamepads to project actions. "
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
        FORGE_UI_PROBE("button:Capture gameplay input");
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
    void release_motion(PlaySession& play) {
        pads_.route_motion(false);
        play.observe_gamepad_motion(pads_.motion(false));
    }
    std::optional<SdlTouches::Viewport> touch_viewport() const {
        if (!bounded_)
            return {};
        return SdlTouches::Viewport{origin_.x, origin_.y, size_.x, size_.y};
    }
    void observe_input_reset(const PlaySession& play) {
        if (touch_reset_serial_ != play.input_reset_serial()) {
            touches_.reset();
            pads_.route_motion(false);
            pads_.route_motion(captured_ && play.ready());
            touch_reset_serial_ = play.input_reset_serial();
        }
    }
    SdlTouches touches_;
    std::uint64_t touch_reset_serial_ = 0;
    std::uint64_t motion_generation_ = 0;
    ImVec2 origin_{}, size_{};
    bool bounded_ = false;
    SdlGamepads pads_;
    std::unique_ptr<SdlGameCursor> cursor_;
    std::string capture_error_;
    std::string assignment_error_;
    bool captured_ = false, devices_dirty_ = true, relative_mode_ = false,
         relative_capture_ = false;
    std::string session_;
};
inline void draw_motion_monitor(const GameInput& input, const PlaySession& play) {
    if (!ImGui::CollapsingHeader("Controller motion"))
        return;
    const auto motion = input.gamepad_motion(play);
    if (!motion.available) {
        ImGui::TextUnformatted("No native controller motion host.");
        return;
    }
    ImGui::TextUnformatted(motion.routed ? "Gameplay input routed"
                                         : "Capture gameplay input for raw samples.");
    const auto topology = input.gamepads();
    for (const auto& device : motion.devices) {
        const auto found =
            std::find_if(topology.devices.begin(), topology.devices.end(),
                         [&](const auto& row) { return row.device == device.device; });
        if (found != topology.devices.end())
            ImGui::TextWrapped("Player %u: %s", device.slot, found->name.c_str());
        for (auto kind : {GamepadSensorKind::Accelerometer, GamepadSensorKind::Gyroscope}) {
            const auto& sensor =
                kind == GamepadSensorKind::Accelerometer ? device.accelerometer : device.gyroscope;
            const auto* title =
                kind == GamepadSensorKind::Accelerometer ? "Acceleration" : "Angular speed";
            if (!sensor.supported)
                ImGui::Text("%s: not supported", title);
            else if (!sensor.enabled)
                ImGui::Text("%s: disabled", title);
            else if (!sensor.measured)
                ImGui::Text("%s: enabled; no sample in this capture interval", title);
            else {
                ImGui::Text("%s: X %.3f  Y %.3f  Z %.3f %s", title, sensor.value[0],
                            sensor.value[1], sensor.value[2],
                            kind == GamepadSensorKind::Accelerometer ? "m/s²" : "rad/s");
                ImGui::Text("Event: %llu ns | Sensor: %llu ns",
                            static_cast<unsigned long long>(sensor.timestamp_ns),
                            static_cast<unsigned long long>(sensor.sensor_timestamp_ns));
            }
        }
    }
    if (motion.devices.empty())
        ImGui::TextUnformatted("No connected controllers.");
    ui::help("Latest raw device-frame readings since capture/configuration. Acceleration includes "
             "gravity; "
             "gyroscope values are angular speed. This is not calibrated orientation. Reading does "
             "not tick Play.");
}
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
