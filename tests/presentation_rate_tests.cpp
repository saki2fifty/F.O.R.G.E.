#include "presentation_rate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    check(std::isfinite(actual) && actual > 0 &&
              std::abs(actual - expected) <= std::max(1e-9, std::abs(expected) * 1e-12),
          "Presentation statistics changed elapsed-time arithmetic");
}
using Rate = forge::PresentationRate;
auto at(long long milliseconds) {
    return Rate::Clock::time_point(
        std::chrono::duration_cast<Rate::Clock::duration>(std::chrono::milliseconds(milliseconds)));
}
} // namespace
int main() {
    try {
        Rate rate;
        check(!rate.sample() && !rate.presented(at(0)), "First presentation fabricated a rate");
        for (int i = 1; i < 50; ++i)
            check(!rate.presented(at(i * 10)) && !rate.sample(),
                  "Presentation window published prematurely");
        check(rate.presented(at(500)) && rate.sample(), "Complete rate window did not publish");
        near(rate.sample()->fps, 100);
        near(rate.sample()->mean_ms, 10);
        near(rate.sample()->peak_ms, 10);
        check(!rate.presented(at(520)), "Partial next window republished an unchanged rate");
        near(rate.sample()->fps, 100);
        for (int i = 2; i <= 25; ++i)
            rate.presented(at(500 + i * 20));
        near(rate.sample()->fps, 50);
        near(rate.sample()->mean_ms, 20);
        rate.reset();
        check(!rate.sample(), "Explicit minimized/startup reset retained an old reading");
        rate.presented(at(90000));
        for (auto time : {90005, 90045, 90100, 90500})
            rate.presented(at(time));
        near(rate.sample()->fps, 8);
        near(rate.sample()->mean_ms, 125);
        near(rate.sample()->peak_ms, 400);
        check(rate.presented(at(90500)) && !rate.sample(),
              "Repeated time retained a reading or divided by zero");
        rate.presented(at(91000));
        check(rate.sample().has_value(), "Rate did not recover after invalid time");
        check(rate.presented(at(100)) && !rate.sample(), "Clock regression retained old metrics");
        rate.presented(at(2600));
        near(rate.sample()->fps, .4);
        near(rate.sample()->mean_ms, 2500);
        near(rate.sample()->peak_ms, 2500); // Real hitches must not be guessed away.
        rate.reset();
        rate.presented(Rate::Clock::time_point::min());
        check(rate.presented(Rate::Clock::time_point::max()) && rate.sample(),
              "Full clock extent overflowed presentation sampling");
        check(std::isfinite(rate.sample()->fps) && rate.sample()->fps > 0 &&
                  std::isfinite(rate.sample()->mean_ms) && rate.sample()->mean_ms > 0,
              "Extreme timestamps produced invalid statistics");
        rate.reset();
        rate.presented(at(0));
        for (int i = 1; i <= 500000; ++i)
            rate.presented(Rate::Clock::time_point(
                std::chrono::duration_cast<Rate::Clock::duration>(std::chrono::microseconds(i))));
        near(rate.sample()->fps, 1000000);
        near(rate.sample()->mean_ms, .001);
        std::cout << "Presented-frame rate, weighted intervals, peak hitches, reset, invalid time "
                     "and full clock extents passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
