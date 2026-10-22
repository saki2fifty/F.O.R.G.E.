#pragma once
#include "game_host_source.hpp"
#include <algorithm>
#include <cmath>

namespace forge::test {
// Observe the real exported game and inject native SDL events. No direct writes
// to gameplay components, body state or simulation clock.
struct Movement2DGameFixture {
    unsigned stage = 0;
    std::uint64_t start_tick = 0;
    double start_x = 0, floor_y = 0, apex = 0;
    bool captured_jump = false;
    static void key(SDL_Window* window, SDL_Scancode code, SDL_Keycode value, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.scancode = code;
        event.key.key = value;
        event.key.down = down;
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    template <class Capture>
    void frame(GameSession& game, SDL_Window* window, const std::filesystem::path& output,
               bool& running, Capture capture) {
        const auto tick = game.status().at("clock").at("tick").get<std::uint64_t>();
        const auto scene = game.active().scene.snapshot();
        const Json* player = nullptr;
        for (const auto& row : scene.at("entities"))
            if (row.at("components").contains("project.movement_2d") ||
                row.at("components").contains("project.character_movement_2d")) {
                if (player)
                    throw std::runtime_error("Exported movement example has multiple players");
                player = &row;
            }
        if (!player)
            throw std::runtime_error("Exported movement example lost its player");
        const auto& components = player->at("components");
        const auto& position = components.at("forge.local_translation");
        const double x = position.at("x"), y = position.at("y");
        const bool controller = components.contains("project.character_movement_2d");
        const char* policy = controller ? "project.character_movement_2d" : "project.movement_2d";
        const double resting_y = controller ? 1. : 1.6;
        const bool grounded = components.at(policy).at("grounded");
        if (controller && (components.contains("forge.physics_body_2d") ||
                           components.contains("forge.capsule_collider_2d")))
            throw std::runtime_error("Exported character gained a hidden authored body/collider");
        const auto require = [](bool condition, const char* text) {
            if (!condition)
                throw std::runtime_error(text);
        };
        if (stage == 0) {
            if (tick < 100 || !grounded)
                return;
            require(std::abs(x + 4) < .002 && std::abs(y - resting_y) < .03,
                    "Exported player did not settle on the painted platform");
            capture("movement-2d-rest.ppm");
            start_x = x;
            start_tick = tick;
            key(window, SDL_SCANCODE_D, SDLK_D, true);
            stage = 1;
        } else if (stage == 1) {
            if (tick < start_tick + 12)
                return;
            const double expected = 4 * double(tick - start_tick) / 60;
            require(x > start_x + .5 && std::abs(x - start_x - expected) < .15,
                    "Exported SDL input did not drive C++ movement at the saved speed");
            key(window, SDL_SCANCODE_D, SDLK_D, false);
            start_tick = tick;
            stage = 2;
        } else if (stage == 2) {
            if (tick < start_tick + 3)
                return;
            require(grounded && std::abs(y - resting_y) < .03,
                    "Exported movement left its starting ledge before the jump");
            floor_y = apex = y;
            start_tick = tick;
            key(window, SDL_SCANCODE_SPACE, SDLK_SPACE, true);
            stage = 3;
        } else if (stage == 3) {
            apex = std::max(apex, y);
            if (!captured_jump && y > floor_y + .9) {
                capture("movement-2d-jump.ppm");
                captured_jump = true;
            }
            if (tick < start_tick + 150)
                return;
            require(captured_jump && apex - floor_y > 1.6 && apex - floor_y < 2.1 && grounded &&
                        std::abs(y - floor_y) < .03,
                    "Exported jump or held-button landing failed");
            key(window, SDL_SCANCODE_SPACE, SDLK_SPACE, false);
            capture("movement-2d-landed.ppm");
            atomic_write(output / "movement-2d-result.json", Json{{"tick", tick},
                                                                  {"native_sdl_input", true},
                                                                  {"controller", controller},
                                                                  {"painted_support", true},
                                                                  {"held_jump_no_repeat", true},
                                                                  {"jump_rise", apex - floor_y},
                                                                  {"components", components}}
                                                                 .dump(2));
            stage = 4;
            running = false;
        }
    }
};
} // namespace forge::test
