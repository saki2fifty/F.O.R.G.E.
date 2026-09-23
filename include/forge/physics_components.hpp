#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <forge/asset_ref.hpp>
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
struct CylinderCollider {
    float radius = .5f, height = 1; // Y-axis cylinder, full height in meters.
    bool operator==(const CylinderCollider&) const = default;
};
struct AssetCollider {
    AssetRef<CollisionAsset> asset;
    bool operator==(const AssetCollider&) const = default;
};
} // namespace forge
