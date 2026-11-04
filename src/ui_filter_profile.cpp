#include "ui_filter_profile.hpp"
#include <cmath>
#include <stdexcept>

namespace forge::ui_filter_detail {
Filter make_filter(std::string_view name, double value) {
    if (!std::isfinite(value) || value < 0)
        throw std::runtime_error("UI filter requires a finite nonnegative parameter");
    Filter result;
    double maximum = 0;
    if (name == "blur") {
        result.kind = Kind::Blur;
        maximum = 16;
    } else if (name == "opacity") {
        result.kind = Kind::Opacity;
        maximum = 1;
    } else if (name == "brightness") {
        result.kind = Kind::Brightness;
        maximum = 4;
    } else if (name == "contrast") {
        result.kind = Kind::Contrast;
        maximum = 4;
    } else if (name == "grayscale") {
        result.kind = Kind::Grayscale;
        maximum = 1;
    } else {
        throw std::runtime_error(
            "Unsupported UI filter; use blur, opacity, brightness, contrast or grayscale");
    }
    if (value > maximum)
        throw std::runtime_error("UI filter parameter exceeds its supported range");
    result.value = float(value);
    result.weights[0] = 1;
    if (result.kind != Kind::Blur || value < 1e-6)
        return result;
    result.radius = unsigned(std::ceil(value * 3));
    double total = 1;
    for (unsigned i = 1; i <= result.radius; ++i) {
        const double distance = i / value;
        result.weights[i] = float(std::exp(-.5 * distance * distance));
        total += 2 * result.weights[i];
    }
    for (unsigned i = 0; i <= result.radius; ++i)
        result.weights[i] = float(result.weights[i] / total);
    return result;
}
Filter make_drop_shadow(double sigma, double x, double y, std::array<float, 4> color) {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 128 || std::abs(y) > 128)
        throw std::runtime_error(
            "UI drop-shadow offsets require finite values within -128..128 pixels");
    for (const auto value : color)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error(
                "UI drop-shadow color requires finite straight RGBA within 0..1");
    auto result = make_filter("blur", sigma);
    result.kind = Kind::DropShadow;
    result.offset = {float(x), float(y)};
    result.color = color;
    return result;
}
} // namespace forge::ui_filter_detail
