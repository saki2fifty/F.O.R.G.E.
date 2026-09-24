#pragma once
#include "../samples/reference_game/ids.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <forge/character_components.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <set>
#include <vector>

namespace forge::test {
// Observer and native SDL input only. Never mutates gameplay/session state.
class ReferenceGameWorkflow {
    unsigned stage_ = 0;
    SDL_JoystickID pad_ = 0;
    bool loading_ = false;
    Uint64 started_ = SDL_GetTicks();
    double before_z_ = 0, before_x_ = 0;
    unsigned pad_probe_ = 0;
    Uint64 idle_previous_ = 0;
    std::vector<double> idle_frame_ms_;
    std::set<std::string> captures_;
    static void push(SDL_Event event) {
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    static void key(SDL_Window* window, SDL_Scancode code, SDL_Keycode value, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.scancode = code;
        event.key.key = value;
        event.key.down = down;
        push(event);
    }
    static void tap(SDL_Window* window, SDL_Scancode code, SDL_Keycode value) {
        key(window, code, value, true);
        key(window, code, value, false);
    }
    void pad_button(SDL_GamepadButton button) {
        SDL_Event event{};
        event.gbutton.which = pad_;
        event.gbutton.button = Uint8(button);
        event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        event.gbutton.down = true;
        push(event);
        event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        event.gbutton.down = false;
        push(event);
    }
    void pad_axis(SDL_GamepadAxis axis, Sint16 value) {
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.which = pad_;
        event.gaxis.axis = Uint8(axis);
        event.gaxis.value = value;
        push(event);
    }
    bool activate(SDL_Window* window, const std::string& label, bool controller = false) {
        auto* context = Rml::GetContext(0);
        if (!context)
            return false;
        auto* focus = context->GetFocusElement();
        if (focus && focus->IsVisible(true) && focus->GetTagName() == "button" &&
            focus->GetInnerRML() == label) {
            if (controller)
                pad_button(SDL_GAMEPAD_BUTTON_SOUTH);
            else
                tap(window, SDL_SCANCODE_RETURN, SDLK_RETURN);
            return true;
        }
        if (controller)
            pad_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        else
            tap(window, SDL_SCANCODE_TAB, SDLK_TAB);
        return false;
    }
    static Json model(GameSession& game) {
        auto ui = std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui());
        const auto value = ui->snapshot(game.active().scene, "reference-observer", 1, 0, false);
        if (value.at("documents").empty())
            return Json::object();
        return value.at("documents")[0].at("model");
    }
    static LocalTranslation position(GameSession& game) {
        return game.active().scene.entity(reference::player_id).get<LocalTranslation>();
    }
    static void require(bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    }

  public:
    ~ReferenceGameWorkflow() {
        if (pad_ && SDL_WasInit(SDL_INIT_GAMEPAD))
            SDL_DetachVirtualJoystick(pad_);
    }
    template <class Capture>
    void frame(GameSession& game, SDL_Window* window, const std::filesystem::path& output,
               bool reopen, unsigned error_mode, Capture capture_raw) {
        auto capture = [&](const char* name) {
            if (captures_.insert(name).second)
                capture_raw(name);
        };
        if (SDL_GetTicks() - started_ > 180000) {
            atomic_write(
                output / "reference-timeout.json",
                Json{{"stage", stage_}, {"game", game.status()}, {"model", model(game)}}.dump(2));
            throw std::runtime_error("Reference input workflow timed out at " +
                                     std::to_string(stage_));
        }
        if (!loading_ && game.loading_state().state == "loading") {
            capture("reference-loading.ppm");
            loading_ = true;
        }
        const auto data = model(game);
        const auto page = data.value("page", "");
        if (error_mode) {
            if (stage_ == 0 && page == "main") {
                if (error_mode == 2) {
                    if (!data.value("message", "")
                             .starts_with("Saved settings could not be loaded"))
                        return;
                    for (const auto& action :
                         game.active().simulation.input().map().source().at("actions"))
                        if (action.at("id") == reference::jump)
                            require(action.at("bindings")[0].at("control") == "key.space",
                                    "Corrupt settings did not recover project default bindings");
                    ++stage_;
                } else if (activate(window, "Load Game"))
                    ++stage_;
            } else if (stage_ == 1 && page == "main" && !data.value("message", "").empty()) {
                require(game.active().scene.asset_id().str() == reference::menu_scene,
                        "Rejected load retired the main menu");
                require(!SDL_GetWindowRelativeMouseMode(window),
                        "Rejected load captured the mouse");
                capture("reference-load-error.ppm");
                atomic_write(output / "reference-error.json", Json{{"rejected", true},
                                                                   {"message", data.at("message")},
                                                                   {"menu_retained", true}}
                                                                  .dump(2));
                ++stage_;
            } else if (stage_ == 2 && page == "main") {
                if (activate(window, "Quit"))
                    ++stage_;
            }
            return;
        }
        if (reopen) {
            if (stage_ == 0 && page == "main") {
                const auto& map = game.active().simulation.input().map().source();
                bool rebound = false;
                for (const auto& action : map.at("actions"))
                    if (action.at("id") == reference::jump)
                        rebound = action.at("bindings")[0].at("control") == "key.j";
                require(rebound, "Relaunch lost the player's saved Jump binding");
                // Slot enumeration is queued after the first control callback. The
                // startup frame renders before that asynchronous receipt is consumed.
                if (!data.value("have_save", false))
                    return; // The workflow deadline still rejects a missing receipt.
                if (activate(window, "Continue"))
                    ++stage_;
            } else if (stage_ == 1 && page == "play") {
                require(data.at("interactions") == 1, "Relaunch lost the saved interaction state");
                std::ifstream expected(output.parent_path() / "reference-state.json");
                Json prior;
                expected >> prior;
                require(std::abs(position(game).z - prior.at("position_z").get<double>()) < .15,
                        "Relaunch lost saved player position");
                capture("reference-loaded-save.ppm");
                tap(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                ++stage_;
            } else if (stage_ == 2 && page == "pause") {
                atomic_write(
                    output / "reference-relaunch.json",
                    Json{{"restored", true}, {"interactions", data.at("interactions")}}.dump(2));
                if (activate(window, "Quit"))
                    ++stage_;
            }
            return;
        }
        if (stage_ == 0 && page == "main") {
            const auto now = SDL_GetTicksNS();
            if (idle_previous_)
                idle_frame_ms_.push_back(double(now - idle_previous_) / 1e6);
            idle_previous_ = now;
            if (idle_frame_ms_.size() < 120)
                return;
            auto samples = idle_frame_ms_;
            std::sort(samples.begin(), samples.end());
            atomic_write(
                output / "reference-performance.json",
                Json{{"menu_idle_frames", samples.size()},
                     {"menu_frame_ms_median", samples[samples.size() / 2]},
                     {"menu_frame_ms_p95", samples[samples.size() * 95 / 100]},
                     {"scope",
                      "Hosted graphical menu incl. presentation; not physical input latency"}}
                    .dump(2));
            capture("reference-main-menu.ppm");
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = 6;
            desc.nbuttons = 15;
            desc.axis_mask = 63;
            desc.button_mask = 32767;
            desc.name = "FORGE reference acceptance controller";
            pad_ = SDL_AttachVirtualJoystick(&desc);
            require(pad_ != 0, "Virtual acceptance gamepad could not connect");
            ++stage_;
        } else if (stage_ == 1 && page == "main") {
            if (activate(window, "New Game", true))
                ++stage_;
        } else if (stage_ == 2 && page == "play" && SDL_GetWindowRelativeMouseMode(window)) {
            capture("reference-gameplay.ppm");
            tap(window, SDL_SCANCODE_E, SDLK_E);
            ++stage_;
        } else if (stage_ == 3 && data.value("interactions", 0) == 1) {
            require(!data.value("prompt", "").empty(),
                    "Beacon interaction had no player-facing prompt");
            capture("reference-interaction.ppm");
            before_z_ = position(game).z;
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.xrel = 10;
            motion.motion.yrel = -2;
            push(motion);
            key(window, SDL_SCANCODE_W, SDLK_W, true);
            ++stage_;
        } else if (stage_ == 4 && position(game).z > before_z_ + .5) {
            key(window, SDL_SCANCODE_W, SDLK_W, false);
            require(
                std::abs(game.active().scene.entity(reference::camera_id).get<LocalRotation>().y) >
                    .001,
                "Relative mouse did not turn the reference camera");
            capture("reference-moving.ppm");
            tap(window, SDL_SCANCODE_SPACE, SDLK_SPACE);
            ++stage_;
        } else if (stage_ == 5 && position(game).y > .15) {
            capture("reference-jump.ppm");
            ++stage_;
        } else if (stage_ == 6) {
            const bool grounded =
                game.active()
                    .physics()
                    ->character(game.active().scene.reference(reference::player_id))
                    .ground == CharacterGround::OnGround;
            if (pad_probe_ == 0 && grounded) {
                before_x_ = position(game).x;
                pad_axis(SDL_GAMEPAD_AXIS_LEFTX, 24576);
                pad_axis(SDL_GAMEPAD_AXIS_RIGHTX, 16384);
                pad_button(SDL_GAMEPAD_BUTTON_SOUTH);
                pad_probe_ = 1;
            } else if (pad_probe_ == 1 && position(game).y > .15 &&
                       position(game).x > before_x_ + .1) {
                require(
                    std::abs(
                        game.active().scene.entity(reference::camera_id).get<LocalRotation>().y) >
                        .02,
                    "Right stick did not turn the reference camera");
                capture("reference-controller-move-look-jump.ppm");
                pad_axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
                pad_axis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
                pad_probe_ = 2;
            } else if (pad_probe_ == 2 && grounded) {
                pad_button(SDL_GAMEPAD_BUTTON_START);
                ++stage_;
            }
        } else if (stage_ == 7 && page == "pause") {
            require(!SDL_GetWindowRelativeMouseMode(window), "Pause retained native mouse capture");
            capture("reference-pause.ppm");
            if (activate(window, "Options"))
                ++stage_;
        } else if (stage_ == 8 && page == "options") {
            if (activate(window, "Rebind Jump"))
                ++stage_;
        } else if (stage_ == 9 && data.value("binding", "").starts_with("Press a key")) {
            capture("reference-controls-listening.ppm");
            tap(window, SDL_SCANCODE_J, SDLK_J);
            ++stage_;
        } else if (stage_ == 10 && data.value("binding", "") == "key.j") {
            capture("reference-controls-candidate.ppm");
            if (activate(window, "Apply binding"))
                ++stage_;
        } else if (stage_ == 11 && data.value("binding", "") != "key.j") {
            if (activate(window, "Back"))
                ++stage_;
        } else if (stage_ == 12 && page == "pause") {
            if (activate(window, "Resume", true))
                ++stage_;
        } else if (stage_ == 13 && page == "play" && SDL_GetWindowRelativeMouseMode(window)) {
            tap(window, SDL_SCANCODE_J, SDLK_J);
            ++stage_;
        } else if (stage_ == 14 && position(game).y > .15) {
            capture("reference-rebound-jump.ppm");
            ++stage_;
        } else if (stage_ == 15 &&
                   game.active()
                           .physics()
                           ->character(game.active().scene.reference(reference::player_id))
                           .ground == CharacterGround::OnGround) {
            SDL_Event focus{};
            focus.window.windowID = SDL_GetWindowID(window);
            focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            push(focus);
            focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            push(focus);
            ++stage_;
        } else if (stage_ == 16 && page == "pause") {
            require(!SDL_GetWindowRelativeMouseMode(window),
                    "Regaining focus unexpectedly recaptured the mouse");
            if (activate(window, "Save Game"))
                ++stage_;
        } else if (stage_ == 17 && data.value("message", "") == "Game saved") {
            atomic_write(output / "reference-state.json",
                         Json{{"interactions", 1}, {"position_z", position(game).z}}.dump(2));
            capture("reference-saved.ppm");
            if (activate(window, "Main Menu"))
                ++stage_;
        } else if (stage_ == 18 && page == "main") {
            capture("reference-returned-menu.ppm");
            if (activate(window, "Continue"))
                ++stage_;
        } else if (stage_ == 19 && page == "play") {
            require(data.at("interactions") == 1, "Same-process load lost game state");
            capture("reference-load.ppm");
            tap(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
            ++stage_;
        } else if (stage_ == 20 && page == "pause") {
            atomic_write(output / "reference-workflow.json",
                         Json{{"complete", true},
                              {"loading_captured", loading_},
                              {"native_relative_mouse", true},
                              {"controller_menu_start_resume", true},
                              {"rebound_jump_executed", true},
                              {"scene_round_trip", true},
                              {"save_load", true}}
                             .dump(2));
            if (activate(window, "Quit"))
                ++stage_;
        }
    }
};
} // namespace forge::test
