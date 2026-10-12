#pragma once
#include "animation_asset.hpp"
#include "animation_root_motion.hpp"
#include <limits>
namespace forge::animation_detail {
// Ordinary absolute clips become additive offsets relative to the admitted rest
// pose. No clock, pose owner or persistent binding is introduced.
inline LocalPose additive_delta(const LocalPose& sample, const LocalPose& rest) {
    const auto rotation = root_compose(
        root_inverse(
            {{}, {rest.rotation[0], rest.rotation[1], rest.rotation[2], rest.rotation[3]}}),
        {{}, {sample.rotation[0], sample.rotation[1], sample.rotation[2], sample.rotation[3]}});
    const auto scalar = [](double value) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            throw ArchiveError("Additive local offset exceeds finite float storage");
        return float(value);
    };
    LocalPose result;
    for (unsigned i = 0; i < 3; ++i) {
        result.translation[i] = scalar(double(sample.translation[i]) - rest.translation[i]);
        if (rest.scale[i] == 0) {
            if (sample.scale[i] != 0)
                throw ArchiveError("Additive scale offset has a zero reference scale");
            result.scale[i] = 1;
        } else
            result.scale[i] = scalar(double(sample.scale[i]) / rest.scale[i]);
    }
    for (unsigned i = 0; i < 4; ++i)
        result.rotation[i] = scalar(rotation.rotation[i]);
    return result;
}
} // namespace forge::animation_detail
