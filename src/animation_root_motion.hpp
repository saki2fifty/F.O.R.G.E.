#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace forge::animation_detail {
// Private rigid algebra in clip-local coordinates, x/y/z/w unit quaternions.
// Existing Animator clocks and transform ownership remain authoritative.
struct RootRigid {
    std::array<double, 3> translation{};
    std::array<double, 4> rotation{0, 0, 0, 1};
};
inline RootRigid root_checked(RootRigid value) {
    for (const auto coordinate : value.translation)
        if (!std::isfinite(coordinate))
            throw std::runtime_error("Root motion translation is not finite");
    double length = 0;
    for (const auto coordinate : value.rotation) {
        if (!std::isfinite(coordinate))
            throw std::runtime_error("Root motion rotation is not finite");
        length += coordinate * coordinate;
    }
    if (!std::isfinite(length) || length < 1e-12)
        throw std::runtime_error("Root motion rotation is degenerate");
    const double inverse_length = 1 / std::sqrt(length);
    for (auto& coordinate : value.rotation)
        coordinate *= inverse_length;
    return value;
}
inline std::array<double, 3> root_rotate(const std::array<double, 4>& q,
                                         const std::array<double, 3>& point) {
    const std::array cross{q[1] * point[2] - q[2] * point[1], q[2] * point[0] - q[0] * point[2],
                           q[0] * point[1] - q[1] * point[0]};
    return {point[0] + 2 * (q[3] * cross[0] + q[1] * cross[2] - q[2] * cross[1]),
            point[1] + 2 * (q[3] * cross[1] + q[2] * cross[0] - q[0] * cross[2]),
            point[2] + 2 * (q[3] * cross[2] + q[0] * cross[1] - q[1] * cross[0])};
}
inline RootRigid root_compose(const RootRigid& parent, const RootRigid& local) {
    const auto p = root_checked(parent), l = root_checked(local);
    const auto position = root_rotate(p.rotation, l.translation);
    const auto& a = p.rotation;
    const auto& b = l.rotation;
    return root_checked({{p.translation[0] + position[0], p.translation[1] + position[1],
                          p.translation[2] + position[2]},
                         {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                          a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                          a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]}});
}
inline RootRigid root_inverse(const RootRigid& value) {
    const auto v = root_checked(value);
    const std::array inverse{-v.rotation[0], -v.rotation[1], -v.rotation[2], v.rotation[3]};
    return root_checked(
        {root_rotate(inverse, {-v.translation[0], -v.translation[1], -v.translation[2]}), inverse});
}
inline RootRigid root_power(RootRigid cycle, std::uint64_t count) {
    RootRigid result;
    while (count) {
        if (count & 1)
            result = root_compose(result, cycle);
        count >>= 1;
        if (count)
            cycle = root_compose(cycle, cycle);
    }
    return result;
}
inline RootRigid root_interval(const RootRigid& previous, const RootRigid& current,
                               const RootRigid& start, const RootRigid& end, bool loop,
                               double begin, double advance, double duration) {
    if (!std::isfinite(begin) || !std::isfinite(advance) || !std::isfinite(duration) ||
        duration <= 0 || begin < 0 || begin > duration || advance < 0)
        throw std::runtime_error("Root motion interval is invalid");
    if (advance == 0)
        return {};
    const double cycles = loop ? std::floor((begin + advance) / duration) : 0;
    if (!std::isfinite(cycles) || cycles > 4294967295.)
        throw std::runtime_error("Root motion cycle count exceeds its safe interval limit");
    if (cycles == 0)
        return root_compose(root_inverse(previous), current);
    const auto cycle = root_compose(root_inverse(start), end);
    // End the old cycle, compose whole intervening turns, then enter the new one.
    // Subtracting wrapped endpoints would move backwards or discard cycle rotation.
    return root_compose(root_compose(root_compose(root_inverse(previous), end),
                                     root_power(cycle, std::uint64_t(cycles) - 1)),
                        root_compose(root_inverse(start), current));
}
inline RootRigid root_blend(const RootRigid& first, const RootRigid& second, double weight) {
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        throw std::runtime_error("Root motion blend weight is invalid");
    const auto a = root_checked(first);
    auto b = root_checked(second);
    double dot = 0;
    for (unsigned i = 0; i < 4; ++i)
        dot += a.rotation[i] * b.rotation[i];
    if (dot < 0) {
        for (auto& coordinate : b.rotation)
            coordinate = -coordinate;
        dot = -dot;
    }
    double left = 1 - weight, right = weight;
    if (dot < .9995) {
        const auto angle = std::acos(std::min(1., dot));
        left = std::sin((1 - weight) * angle) / std::sin(angle);
        right = std::sin(weight * angle) / std::sin(angle);
    }
    RootRigid result;
    for (unsigned i = 0; i < 3; ++i)
        result.translation[i] = a.translation[i] * (1 - weight) + b.translation[i] * weight;
    for (unsigned i = 0; i < 4; ++i)
        result.rotation[i] = a.rotation[i] * left + b.rotation[i] * right;
    return root_checked(result);
}
} // namespace forge::animation_detail
