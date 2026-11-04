#include "ui_filter_profile.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void reject(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Unsupported UI filter admitted");
}
} // namespace
int main() {
    using namespace forge::ui_filter_detail;
    try {
        for (double sigma : {0., 1e-12, .2, 1., 3., 8., 16.}) {
            const auto filter = make_filter("blur", sigma);
            check(filter.kind == Kind::Blur && filter.radius <= max_blur_radius,
                  "Blur kernel exceeds its bound");
            double sum = filter.weights[0], moment = 0;
            for (unsigned i = 1; i <= filter.radius; ++i) {
                check(std::isfinite(filter.weights[i]) && filter.weights[i] >= 0 &&
                          filter.weights[i] <= filter.weights[i - 1],
                      "Blur kernel has negative, nonfinite or increasing tails");
                sum += 2 * filter.weights[i];
                moment += 2 * i * i * filter.weights[i];
            }
            check(std::abs(sum - 1) < 2e-7, "Blur changes a constant image");
            if (sigma >= 1)
                check(std::abs(moment / (sigma * sigma) - 1) < .03,
                      "Blur kernel does not represent the requested variance");
            if (sigma == 0)
                check(filter.radius == 0 && filter.weights[0] == 1, "Zero blur is not identity");
        }
        check(make_filter("opacity", 0).value == 0 && make_filter("brightness", 4).value == 4 &&
                  make_filter("contrast", 1).value == 1 && make_filter("grayscale", 1).value == 1,
              "Supported color filter boundary changed");
        for (const char* name : {"blur", "opacity", "brightness", "contrast", "grayscale"})
            for (double value : {-1., std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()})
                reject([&] { make_filter(name, value); });
        reject([] { make_filter("blur", 16.001); });
        reject([] { make_filter("opacity", 1.001); });
        reject([] { make_filter("brightness", 4.001); });
        reject([] { make_filter("contrast", 4.001); });
        reject([] { make_filter("grayscale", 1.001); });
        reject([] { make_filter("drop-shadow", 1); });
        reject([] { make_filter("custom", 1); });
        for (const auto sigma : {0., .5, 16.}) {
            const auto shadow = make_drop_shadow(sigma, -128, 128, {.2f, .4f, 1, .5f});
            const auto blur = make_filter("blur", sigma);
            check(shadow.kind == Kind::DropShadow && shadow.radius == blur.radius &&
                      shadow.weights == blur.weights &&
                      shadow.offset == std::array{-128.f, 128.f} &&
                      shadow.color == std::array{.2f, .4f, 1.f, .5f},
                  "Drop-shadow lost its bounded kernel, displacement or straight tint");
        }
        for (const auto value : {-129., 128.001, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            reject([&] { make_drop_shadow(1, value, 0, {0, 0, 0, 1}); });
            reject([&] { make_drop_shadow(1, 0, value, {0, 0, 0, 1}); });
        }
        for (const auto sigma : {-1., 16.001, std::numeric_limits<double>::infinity()})
            reject([&] { make_drop_shadow(sigma, 0, 0, {0, 0, 0, 1}); });
        for (unsigned channel = 0; channel < 4; ++channel)
            for (const auto value : {-0.001f, 1.001f, std::numeric_limits<float>::quiet_NaN()}) {
                std::array<float, 4> color{0, 0, 0, 1};
                color[channel] = value;
                reject([&] { make_drop_shadow(1, 0, 0, color); });
            }
        std::cout << "Bounded UI filter parameters and normalized symmetric blur kernels passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
