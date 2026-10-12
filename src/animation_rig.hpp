#pragma once
#include <array>
#include <cstddef>
#include <optional>
namespace forge::animation_detail {
// Derived exact-resource indices, never authored or exposed through the SDK.
struct TwoBoneConstraint {
    std::size_t start{}, middle{}, end{};
    std::array<float, 3> target{}, pole{}, axis{};
    float weight = 1, soften = 1, twist = 0;
};
struct AimConstraint {
    std::size_t joint{};
    std::array<float, 3> target{}, pole{}, forward{}, up{};
    float weight = 1, twist = 0;
};
struct RigConstraints {
    std::optional<TwoBoneConstraint> reach;
    std::optional<AimConstraint> aim;
};
inline bool same_rig_layout(const RigConstraints& a, const RigConstraints& b) {
    if (bool(a.reach) != bool(b.reach) || bool(a.aim) != bool(b.aim))
        return false;
    if (a.reach && (a.reach->start != b.reach->start || a.reach->middle != b.reach->middle ||
                    a.reach->end != b.reach->end || a.reach->axis != b.reach->axis ||
                    a.reach->pole != b.reach->pole))
        return false;
    if (a.aim && (a.aim->joint != b.aim->joint || a.aim->forward != b.aim->forward ||
                  a.aim->up != b.aim->up || a.aim->pole != b.aim->pole))
        return false;
    return true;
}
inline RigConstraints interpolate_rig(const RigConstraints& previous, const RigConstraints& current,
                                      double alpha) {
    if (!same_rig_layout(previous, current))
        return current;
    auto result = current;
    const auto mix = [&](float a, float b) { return float(double(a) + (double(b) - a) * alpha); };
    const auto layer = [&](auto& value, const auto& old) {
        for (unsigned c = 0; c < 3; ++c)
            value.target[c] = mix(old.target[c], value.target[c]);
        value.weight = mix(old.weight, value.weight);
        value.twist = mix(old.twist, value.twist);
    };
    if (result.reach) {
        layer(*result.reach, *previous.reach);
        result.reach->soften = mix(previous.reach->soften, result.reach->soften);
    }
    if (result.aim)
        layer(*result.aim, *previous.aim);
    return result;
}
} // namespace forge::animation_detail
