#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
namespace forge {
// Admitted positive period and finite times. Return the first cycle index and
// count in (begin, end]; callers enforce their own budget before integer casts.
inline std::pair<double, double> animation_periodic_range(double begin, double end, double point,
                                                          double period) {
    const double first = std::floor((begin - point) / period) + 1;
    const double last = std::floor((end - point) / period);
    return {first, std::max(0., last - first + 1)};
}
} // namespace forge
