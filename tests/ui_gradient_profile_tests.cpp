#include "fixtures/ui_gradient.hpp"
#include "ui_gradient_admission.hpp"
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
    throw std::runtime_error("Invalid UI gradient payload admitted");
}

} // namespace
int main() {
    using namespace forge::ui_gradient_detail;
    try {
        auto payload = forge::test::linear_gradient_parameters();
        const auto admitted = admit_gradient("linear-gradient", payload);
        payload.clear();
        check(admitted.count == 2 && !admitted.repeating && admitted.line[0] == 4 &&
                  admitted.line[1] == 8 && std::abs(admitted.line[2] - .01f) < 1e-8 &&
                  admitted.line[3] == 0 && admitted.positions[1] == 1 &&
                  admitted.colors[0] == std::array{1.f, 0.f, 0.f, 1.f} &&
                  admitted.colors[1][2] == 128 / 255.f,
              "Resolved gradient lost its projection, copied stop data or premultiplied tint");
        std::array<Stop, max_stops> stops{};
        for (unsigned i = 0; i < stops.size(); ++i)
            stops[i] = {float(i) / 15, {0, .5f, 0, .5f}};
        const auto diagonal = make_linear_gradient({-2, 3}, {1, 7}, 5, true, stops);
        check(diagonal.count == 16 && diagonal.repeating &&
                  std::abs(diagonal.line[2] - .12f) < 1e-7 &&
                  std::abs(diagonal.line[3] - .16f) < 1e-7,
              "Diagonal projection or maximum stop admission differs");
        for (const auto value : {std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::quiet_NaN(), 65537.f}) {
            reject([&] { make_linear_gradient({value, 0}, {1, 0}, 1, false, stops); });
            reject([&] { make_linear_gradient({0, 0}, {0, value}, 1, false, stops); });
        }
        for (const auto length : {0., -1., 99., std::numeric_limits<double>::infinity()})
            reject([&] { make_linear_gradient({0, 0}, {100, 0}, length, false, stops); });
        reject([&] { make_linear_gradient({0, 0}, {0, 0}, 0, false, stops); });
        reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, false, {}); });
        reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, false, {stops.data(), 1}); });
        std::array<Stop, 17> excessive{};
        reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, false, excessive); });
        for (const auto position : {-1.f, 65537.f, std::numeric_limits<float>::quiet_NaN()}) {
            auto invalid = stops;
            invalid[1].position = position;
            reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, false, invalid); });
        }
        for (const auto color :
             {std::array{1.f, 0.f, 0.f, .5f}, std::array{-1.f, 0.f, 0.f, 1.f},
              std::array{0.f, 0.f, 0.f, std::numeric_limits<float>::quiet_NaN()}}) {
            auto invalid = stops;
            invalid[0].color = color;
            reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, false, invalid); });
        }
        auto collapsed = stops;
        for (auto& stop : collapsed)
            stop.position = .5f;
        check(make_linear_gradient({0, 0}, {1, 0}, 1, false, collapsed).count == 16,
              "Nonrepeating coincident stops were rejected");
        reject([&] { make_linear_gradient({0, 0}, {1, 0}, 1, true, collapsed); });
        for (const auto* key : {"p0", "p1", "length", "repeating", "color_stop_list", "extra"}) {
            auto invalid = forge::test::linear_gradient_parameters();
            invalid[key] = Rml::Variant(Rml::String("wrong type"));
            reject([&] { admit_gradient("linear-gradient", invalid); });
        }
        auto unresolved = forge::test::linear_gradient_parameters();
        unresolved["color_stop_list"] =
            Rml::Variant(Rml::ColorStopList{{{255, 0, 0, 255}, {0, Rml::Unit::PERCENT}},
                                            {{0, 0, 255, 255}, {100, Rml::Unit::PERCENT}}});
        reject([&] { admit_gradient("linear-gradient", unresolved); });
        for (const char* name : {"radial-gradient", "conic-gradient", "shader", "custom"})
            reject([&] { admit_gradient(name, forge::test::linear_gradient_parameters()); });
        std::cout << "Bounded typed linear gradient projection, copied stops, premultiplied color "
                     "and rejection checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
