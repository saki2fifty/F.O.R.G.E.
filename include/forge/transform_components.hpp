#pragma once
// Value-only exact-SDK transform components. Each channel is independently
// authored/inherited. WorldTransform remains derived and engine-owned.
namespace forge {
struct LocalTranslation {
    double x{}, y{}, z{};
    bool operator==(const LocalTranslation&) const = default;
};
struct LocalRotation {
    float x{}, y{}, z{}, w{1};
    bool operator==(const LocalRotation&) const = default;
};
struct LocalScale {
    float x{1}, y{1}, z{1};
    bool operator==(const LocalScale&) const = default;
};
inline constexpr float max_local_scale = 10000;
} // namespace forge
