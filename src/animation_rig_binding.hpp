#pragma once
#include "animation_asset.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_components.hpp>
namespace forge::animation_detail {
struct RigInputs {
    std::optional<TwoBoneIK> reach;
    std::optional<AimIK> aim;
    bool operator==(const RigInputs&) const = default;
};
inline std::array<float, 3> rig_direction(std::array<float, 3> direction) {
    double length = 0;
    for (const auto value : direction)
        length += double(value) * value;
    if (!std::isfinite(length) || length < 1e-12)
        throw ArchiveError("IK direction must be finite and nonzero");
    length = std::sqrt(length);
    for (auto& value : direction)
        value = float(value / length);
    return direction;
}
inline RigConstraints bind_rig(const Skeleton& skeleton, std::span<const AssetId> assets,
                               const RigInputs& inputs, std::optional<std::size_t> motion_root,
                               const RigConstraints* cached = nullptr) {
    const auto joint = [&](AssetId id) {
        if (id && cached) {
            const auto matches = [&](std::size_t index) {
                return index < assets.size() && assets[index] == id;
            };
            if (cached->reach)
                for (auto index : {cached->reach->start, cached->reach->middle, cached->reach->end})
                    if (matches(index))
                        return index;
            if (cached->aim && matches(cached->aim->joint))
                return cached->aim->joint;
        }
        const auto found = std::find(assets.begin(), assets.end(), id);
        if (!id || found == assets.end())
            throw ArchiveError("IK needs a durable joint from this exact imported skeleton");
        return std::size_t(found - assets.begin());
    };
    const auto weight = [](float value) {
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw ArchiveError("IK weight outside finite [0,1]");
    };
    const auto target = [](std::array<float, 3> value) {
        for (auto coordinate : value)
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
                throw ArchiveError("IK target outside finite model-space profile");
        return value;
    };
    const auto twist = [](float degrees) {
        if (!std::isfinite(degrees) || std::abs(degrees) > 360)
            throw ArchiveError("IK twist outside finite [-360,360]");
        return float(double(degrees) * .017453292519943295);
    };
    const auto excludes_motion = [&](std::size_t index) {
        if (motion_root == index)
            throw ArchiveError("IK cannot correct the extracted root-motion joint");
    };
    RigConstraints result;
    if (inputs.reach) {
        const auto& v = *inputs.reach;
        weight(v.weight);
        if (v.enabled && v.weight != 0) {
            const auto start = joint(v.start.id), middle = joint(v.middle.id),
                       end = joint(v.end.id);
            const auto& parents = skeleton.info().parents;
            if (parents.at(middle) != int(start) || parents.at(end) != int(middle))
                throw ArchiveError("Two-bone IK needs a direct start-middle-end chain");
            excludes_motion(start);
            excludes_motion(middle);
            if (!std::isfinite(v.soften) || v.soften < 0 || v.soften > 1)
                throw ArchiveError("IK softening outside finite [0,1]");
            result.reach = TwoBoneConstraint{start,
                                             middle,
                                             end,
                                             target({v.target_x, v.target_y, v.target_z}),
                                             rig_direction({v.pole_x, v.pole_y, v.pole_z}),
                                             rig_direction({v.axis_x, v.axis_y, v.axis_z}),
                                             v.weight,
                                             v.soften,
                                             twist(v.twist_degrees)};
        }
    }
    if (inputs.aim) {
        const auto& v = *inputs.aim;
        weight(v.weight);
        if (v.enabled && v.weight != 0) {
            const auto index = joint(v.joint.id);
            excludes_motion(index);
            const auto forward = rig_direction({v.forward_x, v.forward_y, v.forward_z});
            const auto up = rig_direction({v.up_x, v.up_y, v.up_z});
            double dot = 0;
            for (unsigned c = 0; c < 3; ++c)
                dot += double(forward[c]) * up[c];
            if (std::abs(dot) > 1 - 1e-5)
                throw ArchiveError("Aim forward and up directions must not be parallel");
            result.aim = AimConstraint{index,
                                       target({v.target_x, v.target_y, v.target_z}),
                                       rig_direction({v.pole_x, v.pole_y, v.pole_z}),
                                       forward,
                                       up,
                                       v.weight,
                                       twist(v.twist_degrees)};
        }
    }
    return result;
}
} // namespace forge::animation_detail
