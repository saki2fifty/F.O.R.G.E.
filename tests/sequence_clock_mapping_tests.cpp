#include "sequence_clock_mapping.hpp"
#include <array>
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
        check(span.root_point(span.local_begin) == span.begin &&
                  span.root_point(span.local_end) == span.end,
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
void repeated_placements() {
    using Placement = SequenceClockMapping::Placement;
    const auto root = SequenceClockMapping::root(20);
    const std::array placements{Placement{1, 2, false}, Placement{5, .5, true},
                                Placement{9, 1, false}, Placement{13, 4, true}};
    const auto repeated = SequenceClockMapping::child(root, 2, placements);
    check(repeated.spans().size() == 17, "Repeated placements lost finite spans or loop cycles");
    const auto local = [](double time) {
        if (time < 1)
            return 0.;
        if (time < 5)
            return std::min(2., (time - 1) * 2);
        if (time < 9)
            return (time - 5) * .5;
        if (time < 13)
            return std::min(2., time - 9);
        return time == 20 ? 2. : std::fmod((time - 13) * 4, 2.);
    };
    const std::array nested_placements{Placement{.25, 1, false}, Placement{1, 2, false},
                                       Placement{1.5, 1, true}};
    const auto nested = SequenceClockMapping::child(repeated, .5, nested_placements);
    const auto nested_local = [](double time) {
        if (time < .25)
            return 0.;
        if (time < 1)
            return std::min(.5, time - .25);
        if (time < 1.5)
            return std::min(.5, (time - 1) * 2);
        return time - 1.5;
    };
    for (unsigned i = 0; i <= 2000; ++i) {
        const double time = i / 100.;
        const auto expected = local(time);
        near(repeated.sample(time), expected);
        near(nested.sample(time), nested_local(expected));
        const bool parent_active =
            (time >= 1 && time < 2) || (time >= 5 && time < 11) || (time >= 13 && time < 20);
        check(repeated.active(time) == parent_active, "Repeated controller became active in a gap");
        const bool child_active =
            parent_active && ((expected >= .25 && expected < .75) ||
                              (expected >= 1 && expected < 1.25) || expected >= 1.5);
        check(nested.active(time) == child_active,
              "Nested repeated placement acquired a false window");
        const auto* span = nested.at(time);
        const double expected_rate = (time < 5    ? 2
                                      : time < 9  ? .5
                                      : time < 13 ? 1
                                                  : 4) *
                                     (expected >= 1 && expected < 1.5 ? 2 : 1);
        if (child_active)
            near(span->rate, expected_rate);
    }
    for (const auto time : {5., 9., 13., 13.5}) {
        check(repeated.reset_crossed(time - .001, time), "Explicit placement reset was missed");
        near(repeated.sample(time), 0);
        near(nested.sample(time), 0);
    }
    near(repeated.sample(25), 2);
    near(nested.sample(25), .5);
    check(!repeated.reset_crossed(2, 4), "Held gap fabricated a reset");
    for (const auto& span : nested.spans()) {
        check(span.root_point(span.local_begin) == span.begin &&
                  span.root_point(span.local_end) == span.end,
              "Repeated placement inverse moved a stored endpoint");
        near(span.sample(span.begin), span.local_begin);
        near(span.sample(span.end), span.local_end);
    }
    const std::array absent{Placement{2.5, 1, false}, Placement{3, 1, false}};
    rejects([&] { return SequenceClockMapping::child(repeated, .25, absent); });
    const std::array overlap{Placement{1, 1, false}, Placement{2, 1, false}};
    rejects([&] { return SequenceClockMapping::child(root, 2, overlap); });
    const std::array unordered{Placement{1, 1, true}, Placement{1, 1, true}};
    rejects([&] { return SequenceClockMapping::child(root, 2, unordered); });
    rejects([&] { return SequenceClockMapping::child(root, 2, std::span<const Placement>()); });
    const std::array<Placement, 9> excessive{};
    rejects([&] { return SequenceClockMapping::child(root, 2, excessive); });
    const std::array budget{Placement{0, 1, true}, Placement{10, 1, true}};
    check(SequenceClockMapping::child(root, 20. / 256, budget).spans().size() == 256,
          "Repeated placements did not share the exact span budget");
    rejects([&] { return SequenceClockMapping::child(root, 20. / 257, budget); });
}
void authored_boundaries() {
    using Placement = SequenceClockMapping::Placement;
    const auto root = SequenceClockMapping::root(40, 1e6);
    for (const double duration : {.1, 1., 3.7})
        for (const double rate : {.25, .7, 1., 1.3, 2.3, 4.})
            for (const bool loop : {false, true}) {
                const double end = .1 + duration / rate;
                const std::array placements{Placement{.1, rate, loop}, Placement{end, .7, false}};
                const auto value = SequenceClockMapping::child(root, duration, placements);
                check(value.spans().size() == 2, "Unit conversion fabricated a touching cycle");
                const auto& before = value.spans().front();
                const auto& after = value.spans().back();
                check(before.end == end * 1e6 && before.end == after.begin &&
                          before.local_end == duration * 1e6 && after.local_begin == 0,
                      "Touching authored boundaries lost their exact cue/end coordinates");
                near(value.sample(end * 1e6), 0);
                check(before.root_point(duration * 1e6) == after.root_point(0),
                      "Unit conversion separated an outgoing endpoint and incoming entry cue");
                const auto descendant = SequenceClockMapping::child(value, .03, .02, .7, false);
                near(descendant.sample(end * 1e6), 0);
                if (!loop) {
                    const std::array overlap{Placement{.1, rate, false},
                                             Placement{std::nextafter(end, 0.), .7, false}};
                    rejects([&] { return SequenceClockMapping::child(root, duration, overlap); });
                }
            }
}
} // namespace
int main() {
    try {
        clocks();
        limits();
        repeated_placements();
        authored_boundaries();
        std::cout << "Child clock mapping, nested phases, holds, resets and budgets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
