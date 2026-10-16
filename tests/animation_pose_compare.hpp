#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>

namespace forge::test {
// Separate blend paths perform estimated quaternion normalization a different
// number of times. Keep every other pose field exact; admit only a few float
// rounding steps per rotation component. Same-path recovery remains bit-exact.
inline bool same_blended_local_pose(nlohmann::json a, nlohmann::json b) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    if (!a.is_object() || !b.is_object() || !a.contains("rotation") || !b.contains("rotation"))
        return false;
    const auto& ar = a.at("rotation");
    const auto& br = b.at("rotation");
    if (!ar.is_array() || !br.is_array() || ar.size() != 4 || br.size() != 4)
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!ar[i].is_number() || !br[i].is_number())
            return false;
        const float x = ar[i].get<float>(), y = br[i].get<float>();
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        if (x == y)
            continue;
        if (std::signbit(x) != std::signbit(y))
            return false;
        const auto xb = std::bit_cast<std::uint32_t>(x), yb = std::bit_cast<std::uint32_t>(y);
        if ((xb > yb ? xb - yb : yb - xb) > 4)
            return false;
    }
    a.erase("rotation");
    b.erase("rotation");
    return a == b;
}
} // namespace forge::test
