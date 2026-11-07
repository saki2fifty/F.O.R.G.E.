#include "sequence_clock_mapping.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using forge::detail::SequenceClockMapping;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double value, double expected) {
    check(std::isfinite(value) && std::abs(value - expected) < 1e-9,
          "Derived child clock differs from the independent reference");
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid or unbounded child clock was accepted");
}
void clocks() {
    const auto root = SequenceClockMapping::root(20);
    const auto finite = SequenceClockMapping::child(root, 2, 3, 2, false);
    check(finite.spans().size() == 1, "Ordinary child lost its inline affine span");
    for (int i = 0; i <= 200; ++i) {
        const double time = i * .1;
        near(finite.sample(time), std::clamp((time - 2) * 2, 0., 3.));
    }
    const auto loop = SequenceClockMapping::child(root, 2, 3, 2, true);
    check(loop.spans().size() == 12, "Child loop did not fill the remaining parent span");
    for (int i = 0; i < 200; ++i) {
        const double time = i * .1;
        near(loop.sample(time), time < 2 ? 0 : std::fmod((time - 2) * 2, 3.));
    }
    near(loop.sample(20), 3); // The final endpoint holds instead of wrapping to zero.
    near(loop.sample(25), 3);
    near(loop.sample(3.5), 0);
    check(!loop.active(1) && loop.active(2) && !loop.active(20) && loop.active(20, true),
          "Child active span or inclusive endpoint differs");
    check(loop.reset_crossed(3.49, 3.5) && !loop.reset_crossed(3.5, 3.6) &&
              !loop.reset_crossed(1, 2),
          "Child reset boundaries do not use the forward interval convention");
    const auto nested = SequenceClockMapping::child(loop, .5, 1, .5, false);
    check(nested.spans().size() == 12, "Ancestor loops did not repeat a finite descendant");
    for (int i = 0; i < 400; ++i) {
        const double time = i * .05;
        const auto parent = time < 2 ? 0 : std::fmod((time - 2) * 2, 3.);
        near(nested.sample(time), std::clamp((parent - .5) * .5, 0., 1.));
        check(nested.active(time) == (time >= 2 && parent >= .5 && parent < 2.5),
              "Finite descendant is active in an ancestor's held gap");
    }
    const auto nested_loop = SequenceClockMapping::child(loop, .5, .75, .5, true);
    check(nested_loop.spans().size() == 24, "Nested child cycles were not composed");
    for (int i = 0; i < 400; ++i) {
        const double time = i * .05;
        const auto parent = time < 2 ? 0 : std::fmod((time - 2) * 2, 3.);
        // The containing parent's reset clears the child's inactive entry gap.
        const double expected = time < 2.25 || parent < .5 ? 0 : std::fmod((parent - .5) * .5, .75);
        near(nested_loop.sample(time), expected);
    }
    near(nested_loop.sample(20), .5);
    const auto endpoint = SequenceClockMapping::child(root, 20, 1, 1, true);
    check(endpoint.spans().size() == 1 && !endpoint.active(20),
          "Zero-length endpoint placement became playing");
    near(endpoint.sample(30), 0);
    const auto clipped = SequenceClockMapping::child(finite, 2, 10, 1, true);
    near(clipped.sample(20), 1);
    const auto absent = SequenceClockMapping::child(clipped, 5, 1, 1, true);
    check(absent.spans().size() == 1 && !absent.spans().front().active && !absent.active(20),
          "Ancestor-clipped child acquired a fabricated active span");
    near(absent.sample(20), 0);
    for (const double rate : {1.3, .7, 2.3, .25, 4.}) {
        const auto fractional = SequenceClockMapping::child(root, 2.1, 3.7, rate, false);
        const auto& span = fractional.spans().front();
        check(span.root_point(span.local_begin, fractional.rate()) == span.begin &&
                  span.root_point(span.local_end, fractional.rate()) == span.end,
              "Fractional inverse timing moved an exact endpoint");
        const auto final = SequenceClockMapping::child(fractional, 3.7, .1, .7, false);
        check(final.spans().size() == 1 && final.spans().front().begin == span.end &&
                  final.spans().front().end == span.end,
              "Fractional parent endpoint fabricated or lost a child endpoint span");
        near(final.sample(20), 0);
    }
}
void limits() {
    const auto root = SequenceClockMapping::root(256);
    const auto full = SequenceClockMapping::child(root, 0, 1, 1, true);
    check(full.spans().size() == 256, "Exact derived span budget was rejected");
    rejects([&] { return SequenceClockMapping::child(root, 0, .5, 1, true); });
    rejects([&] { return SequenceClockMapping::child(full, 0, .5, 1, true); });
    rejects([&] { return SequenceClockMapping::child(root, 0, 1e-300, 1, true); });
    rejects([&] { return SequenceClockMapping::root(0); });
    rejects([&] { return SequenceClockMapping::root(std::numeric_limits<double>::infinity()); });
    rejects([&] { return SequenceClockMapping::child(root, -1, 1, 1, false); });
    rejects([&] { return SequenceClockMapping::child(root, 257, 1, 1, false); });
    rejects([&] { return SequenceClockMapping::child(root, 0, 1, 0, false); });
    const auto small =
        SequenceClockMapping::child(SequenceClockMapping::root(256. / 64), 0, 1, 64, true);
    check(small.spans().size() == 256, "Small root span exhausted a wrong coordinate budget");
    near(small.sample(1. / 64), 0);
    near(small.sample(4), 1);
}
} // namespace
int main() {
    try {
        clocks();
        limits();
        std::cout << "Child clock mapping, nested phases, holds, resets and budgets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
