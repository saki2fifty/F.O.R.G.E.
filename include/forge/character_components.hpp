#pragma once
#include <cstdint>
namespace forge {
// Authored mechanics only. No input bindings, camera, velocity or contact cache.
struct CharacterController {
    bool enabled = true;
    std::uint32_t shape = 0; // Capsule, Cylinder; feet are the local origin.
    float radius = .35f, height = 1.1f, crouch_height = .45f;
    float mass = 70, max_strength = 100;
    float max_slope = 50; // Degrees.
    float step_height = .4f, step_forward = .15f, floor_probe = .5f;
    float gravity_factor = 1;
    std::uint32_t layer = 0, mask = UINT32_MAX;
    bool operator==(const CharacterController&) const = default;
};
// XY capsule mechanics. The local origin is at the feet. Game C++ supplies
// world-space velocity, gravity and jump policy through CharacterService.
struct CharacterController2D {
    bool enabled = true;
    std::uint32_t mode = 0;           // Grounded, Floating.
    float radius = .3f, height = .6f; // Straight height excludes the caps.
    float max_slope = 50;             // Walkable degrees, Grounded only.
    float floor_probe = .1f;          // World metres; zero disables floor following.
    std::uint32_t layer = 0, mask = UINT32_MAX;
    bool operator==(const CharacterController2D&) const = default;
};
} // namespace forge
