#include "morph_animation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace forge::asset_detail;
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid morph curve accepted");
}
int main() {
    try {
        Json track{{"node", 5},
                   {"components", 2},
                   {"interpolation", 0},
                   {"times", {1, 3}},
                   {"values", {-1, 2, 3, -2}}};
        MorphAnimation linear(Json::array({track}));
        require(linear.sample(2).at(0).node == 5 &&
                    linear.sample(2)[0].weights == std::vector<float>{1, 0},
                "Linear morph weights changed or were clamped");
        require(linear.sample(-1)[0].weights == std::vector<float>{-1, 2} &&
                    linear.sample(99)[0].weights == std::vector<float>{3, -2},
                "Morph endpoint clamp changed");
        track["interpolation"] = 1;
        MorphAnimation step(Json::array({track}));
        require(step.sample(2.999)[0].weights == std::vector<float>{-1, 2} &&
                    step.sample(3)[0].weights == std::vector<float>{3, -2},
                "STEP boundary changed");
        track = {{"node", 7},
                 {"components", 1},
                 {"interpolation", 2},
                 {"times", {0, 2}},
                 {"values", {0, 0, 2, 0, 1, 0}}};
        MorphAnimation cubic(Json::array({track}));
        require(std::abs(cubic.sample(1)[0].weights[0] - 1.f) < 1e-6f,
                "Cubic morph tangent time scaling changed");
        require(cubic.sample(0)[0].weights[0] == 0 && cubic.sample(2)[0].weights[0] == 1,
                "Cubic endpoints changed");
        auto bad = track;
        bad["times"] = {1, 1};
        rejects([&] { MorphAnimation c(Json::array({bad})); });
        bad = track;
        bad["values"].erase(0);
        rejects([&] { MorphAnimation c(Json::array({bad})); });
        bad = track;
        bad["components"] = 257;
        rejects([&] { MorphAnimation c(Json::array({bad})); });
        bad = track;
        bad["interpolation"] = 3;
        rejects([&] { MorphAnimation c(Json::array({bad})); });
        bad = track;
        bad["values"][0] = std::numeric_limits<double>::infinity();
        rejects([&] { MorphAnimation c(Json::array({bad})); });
        rejects([&] { MorphAnimation c(Json::array({track, track})); });
        rejects([&] { (void)cubic.sample(std::numeric_limits<double>::quiet_NaN()); });
        track["values"] = {0, 0, 3e38, 0, 0, 0};
        // A long interval magnifies derivative contribution; diagnose overflow.
        track["times"] = {0, 3600};
        MorphAnimation long_curve(Json::array({track}));
        rejects([&] { (void)long_curve.sample(1800); });
        std::cout << "Morph STEP/LINEAR/CUBIC sampling and rejection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
