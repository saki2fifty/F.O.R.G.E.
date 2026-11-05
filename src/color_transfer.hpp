#pragma once
#include <cmath>
namespace forge {
// Scalar RGB transfer only. Alpha, exposure and tone mapping belong to callers.
inline double linear_to_srgb(double value) {
    return value <= .0031308 ? 12.92 * value : 1.055 * std::pow(value, 1. / 2.4) - .055;
}
inline double srgb_to_linear(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
} // namespace forge
