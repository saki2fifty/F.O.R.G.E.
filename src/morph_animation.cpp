#include "morph_animation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::size_t integer(const Json& j, std::size_t limit) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Invalid morph curve integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= limit, "Morph curve count/index exceeds limit");
    return static_cast<std::size_t>(n);
}
float scalar(const Json& j) {
    require(j.is_number(), "Invalid morph curve scalar");
    const double x = j.get<double>();
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max() &&
                (x == 0 || float(x) != 0),
            "Morph curve scalar outside finite float storage");
    return static_cast<float>(x);
}
} // namespace
MorphAnimation::MorphAnimation(const Json& tracks) {
    require(tracks.is_array() && tracks.size() <= 1024, "Morph track count exceeds rig profile");
    std::size_t numbers = 16 * 1024 * 1024 / 32;
    std::set<std::size_t> nodes;
    for (const auto& source : tracks) {
        Track track;
        track.node = integer(source.at("node"), 99999);
        require(nodes.insert(track.node).second, "Duplicate morph target node");
        track.components = static_cast<unsigned>(integer(source.at("components"), 256));
        track.interpolation = static_cast<unsigned>(integer(source.at("interpolation"), 2));
        require(track.components != 0, "Morph track requires weights");
        const auto& times = source.at("times");
        const auto& values = source.at("values");
        const std::size_t multiplier = track.interpolation == 2 ? 3 : 1;
        require(times.is_array() && !times.empty() && times.size() <= numbers &&
                    (multiplier != 3 || times.size() >= 2),
                "Invalid morph curve key count");
        numbers -= times.size();
        require(values.is_array() && times.size() <= numbers / track.components / multiplier &&
                    values.size() == times.size() * track.components * multiplier,
                "Morph curve values exceed bounds or do not match keys");
        numbers -= values.size();
        for (const auto& value : times) {
            const auto t = scalar(value);
            require(t >= 0 && t <= 3600 && (track.times.empty() || t > track.times.back()),
                    "Morph curve times must increase within clip profile");
            track.times.push_back(t);
        }
        for (const auto& value : values)
            track.values.push_back(scalar(value));
        tracks_.push_back(std::move(track));
    }
}
std::vector<MorphWeightsSample> MorphAnimation::sample(double seconds) const {
    require(std::isfinite(seconds), "Morph sampling time must be finite");
    std::vector<MorphWeightsSample> result;
    result.reserve(tracks_.size());
    for (const auto& track : tracks_) {
        const auto upper = std::upper_bound(track.times.begin(), track.times.end(), seconds);
        const auto right = static_cast<std::size_t>(upper - track.times.begin());
        const auto left = right == 0 ? 0 : right - 1;
        const auto multiplier = track.interpolation == 2 ? 3u : 1u;
        const auto offset = (left * multiplier + (multiplier == 3 ? 1 : 0)) * track.components;
        MorphWeightsSample sampled{track.node, std::vector<float>(track.components)};
        for (unsigned component = 0; component < track.components; ++component) {
            double value = track.values[offset + component];
            if (right > 0 && right < track.times.size() && track.interpolation != 1) {
                const double delta = double(track.times[right]) - track.times[left];
                const double t = (seconds - track.times[left]) / delta;
                const auto next =
                    (right * multiplier + (multiplier == 3 ? 1 : 0)) * track.components;
                const double end = track.values[next + component];
                if (multiplier == 1)
                    value = (1 - t) * value + t * end;
                else {
                    const double t2 = t * t, t3 = t2 * t;
                    const double out = track.values[offset + track.components + component];
                    const double in = track.values[next - track.components + component];
                    value = (2 * t3 - 3 * t2 + 1) * value + (t3 - 2 * t2 + t) * delta * out +
                            (-2 * t3 + 3 * t2) * end + (t3 - t2) * delta * in;
                }
            }
            require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
                    "Morph curve evaluation exceeds finite float profile");
            sampled.weights[component] = static_cast<float>(value);
        }
        result.push_back(std::move(sampled));
    }
    return result;
}
} // namespace forge::asset_detail
