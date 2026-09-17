#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace forge {
struct PhysicsConfig {
    std::array<double, 3> gravity{0, -9.81, 0};
    void validate() const {
        for (auto g : gravity)
            if (!std::isfinite(g) || std::abs(g) > 10000)
                throw std::runtime_error(
                    "Gravity must be finite and within +/-10000 m/s² per axis");
    }
    bool operator==(const PhysicsConfig&) const = default;
};
// Authored values only. No solver handles or Jolt headers cross this boundary.
struct PhysicsBody {
    std::uint32_t motion = 0;       // Static, Kinematic, Dynamic.
    float density = 1000, mass = 0; // kg/m^3; mass=0 uses density, otherwise kg override.
    float friction = .5f, restitution = 0, gravity_factor = 1;
    bool operator==(const PhysicsBody&) const = default;
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
} // namespace forge
