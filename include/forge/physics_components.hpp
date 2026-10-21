#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <forge/identity.hpp>
#include <set>
#include <stdexcept>
#include <string>
namespace forge {
struct PhysicsConfig {
    std::array<double, 3> gravity{0, -9.81, 0};
    // Stable slot IDs 0..31. Rename a slot without changing stored references;
    // empty slots are unavailable, and slot zero remains the default layer.
    std::array<std::string, 32> layers{"Default"};
    void validate() const {
        for (auto g : gravity)
            if (!std::isfinite(g) || std::abs(g) > 10000)
                throw std::runtime_error(
                    "Gravity must be finite and within +/-10000 m/s² per axis");
        std::set<std::string> names;
        if (layers[0].empty())
            throw std::runtime_error("Physics layer zero must have a name");
        for (const auto& name : layers)
            if (name.size() > 64 || (!name.empty() && !names.insert(name).second) ||
                name.find_first_of("\r\n\t") != std::string::npos)
                throw std::runtime_error(
                    "Physics layer names must be unique, single-line and at most 64 bytes");
    }
    bool operator==(const PhysicsConfig&) const = default;
};
// Authored values only. No solver handles or Jolt headers cross this boundary.
struct PhysicsBody {
    std::uint32_t motion = 0;       // Static, Kinematic, Dynamic.
    float density = 1000, mass = 0; // kg/m^3; mass=0 uses density, otherwise kg override.
    float friction = .5f, restitution = 0, gravity_factor = 1;
    bool enabled = true, sensor = false;
    std::uint32_t layer = 0, mask = UINT32_MAX;
    bool continuous_collision = false; // Linear sweep for Dynamic nonsensor solids only.
    float linear_damping = .05f, angular_damping = .05f;          // Per second, not friction.
    float max_linear_velocity = 500, max_angular_velocity = 2700; // m/s and degrees/s.
    bool allow_sleeping = true;
    bool operator==(const PhysicsBody&) const = default;
};
// Authored joint entity; solver handles stay in PhysicsRuntime. Body-local anchors
// use the entity origin (not center of mass) and include effective signed scale.
struct PhysicsJoint {
    bool enabled = true;
    std::uint32_t kind = 0; // Distance, Hinge, Point, Fixed, Slider, Swing Twist.
    EntityRef body_a, body_b;
    float anchor_a_x = 0, anchor_a_y = 0, anchor_a_z = 0;
    float anchor_b_x = 0, anchor_b_y = 0, anchor_b_z = 0;
    float min_distance = 1, max_distance = 1;
    float min_travel = -1, max_travel = 1; // Slider signed metres enclosing zero.
    std::uint32_t axis_a = 2, normal_a = 0, axis_b = 2, normal_b = 0; // X, Y, Z.
    float min_angle = -90, max_angle = 90; // Degrees enclosing the rest angle zero.
    bool motor_enabled = false;            // Velocity drive for Hinge/Slider only.
    float hinge_motor_speed = 90;          // Signed degrees/s, Body B relative to A.
    float slider_motor_speed = 1;          // Signed metres/s, Body B relative to A.
    float max_motor_torque = 10;           // Hinge torque budget, N*m; zero coasts.
    float max_motor_force = 100;           // Slider force budget, N; zero coasts.
    std::uint32_t motor_mode = 0;          // Velocity (0) or Position (1).
    float hinge_motor_angle = 0;           // Signed degrees relative to rest frame.
    float slider_motor_position = 0;       // Signed anchor distance in metres.
    float motor_frequency = 2;             // Position spring frequency, Hz.
    float motor_damping = 1;               // Position damping ratio; 1 is critical.
    bool collide_connected = true;         // Enabled joints may suppress this body pair.
    float swing_normal_angle = 45;         // Swing cone half-angle towards frame normal, degrees.
    float swing_plane_angle = 45;          // Swing cone half-angle towards plane axis, degrees.
    float distance_spring_frequency = 0;   // Distance limits: 0 is hard; otherwise Hz.
    float distance_spring_damping = 1;     // Distance spring damping ratio; 1 is critical.
    bool operator==(const PhysicsJoint&) const = default;
};
struct BoxCollider {
    float x = 1, y = 1, z = 1; // Full dimensions in meters, centered at entity origin.
    bool operator==(const BoxCollider&) const = default;
};
struct SphereCollider {
    float radius = .5f;
    bool operator==(const SphereCollider&) const = default;
};
struct CapsuleCollider {
    float radius = .5f, height = 1; // Straight cylinder height; total=height+2*radius, Y axis.
    bool operator==(const CapsuleCollider&) const = default;
};
struct CylinderCollider {
    float radius = .5f, height = 1; // Y-axis cylinder, full height in meters.
    bool operator==(const CylinderCollider&) const = default;
};
struct AssetCollider {
    AssetRef<CollisionAsset> asset;
    bool operator==(const AssetCollider&) const = default;
};
// Distinct authored XY-domain values. Native worlds/handles remain private;
// these do not project a 3D PhysicsBody into another simulation domain.
struct PhysicsBody2D {
    std::uint32_t motion = 0;    // Static, Kinematic, Dynamic.
    float density = 1, mass = 0; // kg/m^2; mass=0 uses area-density, otherwise kg.
    float friction = .6f, restitution = 0, gravity_factor = 1;
    bool enabled = true, sensor = false;
    std::uint32_t layer = 0, mask = UINT32_MAX;
    float linear_damping = 0, angular_damping = 0; // Per second.
    bool allow_sleeping = true;
    bool lock_x = false, lock_y = false, lock_rotation = false;
    bool bullet = false; // Additional Dynamic-solid CCD against moving bodies.
    bool operator==(const PhysicsBody2D&) const = default;
};
// Distinct XY-domain joint values; body-local anchors include signed XY scale.
// Frame angles use each body's rigid rotation, independent of its shape scale.
struct PhysicsJoint2D {
    bool enabled = true;
    std::uint32_t kind = 0; // Distance, Hinge, Slider. Independent of the 3D kind IDs.
    EntityRef body_a, body_b;
    float anchor_a_x = 0, anchor_a_y = 0, anchor_b_x = 0, anchor_b_y = 0;
    float frame_a_angle = 0, frame_b_angle = 0; // Body-local Z angles, degrees.
    float distance = 1; // World metres; also the elastic spring's rest length.
    float spring_frequency = 0, spring_damping = 1; // Distance: Hz (0 is rigid), ratio.
    bool limits_enabled = true;
    float min_angle = -90, max_angle = 90; // Hinge frame angle, degrees.
    float min_travel = -1, max_travel = 1; // Slider frame X displacement, world metres.
    bool motor_enabled = false;
    float hinge_motor_speed = 90, slider_motor_speed = 1; // Degrees/s, metres/s; B vs A.
    float max_motor_torque = 10, max_motor_force = 100;   // N*m, N; zero coasts.
    bool collide_connected = true; // Project layers/masks still apply to permitted pairs.
    bool operator==(const PhysicsJoint2D&) const = default;
};
struct BoxCollider2D {
    float x = 1, y = 1; // Centered full dimensions in meters.
    bool operator==(const BoxCollider2D&) const = default;
};
struct CircleCollider2D {
    float radius = .5f;
    bool operator==(const CircleCollider2D&) const = default;
};
struct CapsuleCollider2D {
    float radius = .25f, height = 1; // Straight segment along Y, excluding the caps.
    bool operator==(const CapsuleCollider2D&) const = default;
};
} // namespace forge
