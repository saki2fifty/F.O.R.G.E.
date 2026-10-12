#pragma once
#include "animation_root_motion.hpp"
#include <limits>
namespace forge::test {
inline void root_motion_math_tests() {
    using namespace animation_detail;
    const auto near = [](double actual, double expected) {
        if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-7)
            throw std::runtime_error("Root displacement differs from independent trajectory");
    };
    const auto vector = [&](const auto& actual, const std::array<double, 3>& expected) {
        for (unsigned i = 0; i < 3; ++i)
            near(actual[i], expected[i]);
    };
    const auto rejects = [](const auto& operation) {
        try {
            operation();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Invalid root motion was accepted");
    };
    const double half = std::sqrt(.5);
    const RootRigid identity;
    const RootRigid quarter{{0, 0, 1}, {0, half, 0, half}};
    // A one-meter forward step followed by each quarter turn traces a square.
    for (const auto& [turns, position] :
         {std::pair{1u, std::array{0., 0., 1.}}, std::pair{2u, std::array{1., 0., 1.}},
          std::pair{3u, std::array{1., 0., 0.}}, std::pair{4u, std::array{0., 0., 0.}},
          std::pair{5u, std::array{0., 0., 1.}}}) {
        const auto result =
            root_interval(identity, identity, identity, quarter, true, 0, double(turns), 1);
        vector(result.translation, position);
        const std::array directions{std::array{0., 0., 1.}, std::array{1., 0., 0.},
                                    std::array{0., 0., -1.}, std::array{-1., 0., 0.}};
        vector(root_rotate(result.rotation, {0, 0, 1}), directions[turns % 4]);
    }
    const auto inverse = root_compose(quarter, root_inverse(quarter));
    vector(inverse.translation, {0, 0, 0});
    vector(root_rotate(inverse.rotation, {0, 0, 1}), {0, 0, 1});
    // Nonzero clip origins cancel; the delta remains in the previous root frame.
    const RootRigid previous{{101, 2, -29}, {0, half, 0, half}};
    const RootRigid current{{101, 2, -28}, {0, 1, 0, 0}};
    const auto shifted = root_interval(previous, current, previous, current, false, .2, .1, 1);
    vector(shifted.translation, {-1, 0, 0});
    vector(root_rotate(shifted.rotation, {0, 0, 1}), {1, 0, 0});
    const auto wrapped =
        root_interval({{0, 0, .75}}, {{0, 0, .25}}, identity, {{0, 0, 1}}, true, .75, .5, 1);
    vector(wrapped.translation, {0, 0, .5});
    vector(root_interval(previous, current, identity, quarter, true, .2, 0, 1).translation,
           {0, 0, 0});
    const auto many = root_interval(identity, identity, identity, {{0, 0, 1}}, true, 0, 1048576, 1);
    vector(many.translation, {0, 0, 1048576});
    const auto blended = root_blend({{0, 0, 1}}, {{0, 0, 3}, {0, 1, 0, 0}}, .5);
    vector(blended.translation, {0, 0, 2});
    vector(root_rotate(blended.rotation, {0, 0, 1}), {1, 0, 0});
    const auto same_sign = root_blend(quarter, {{0, 0, 1}, {0, -half, 0, -half}}, .5);
    vector(root_rotate(same_sign.rotation, {0, 0, 1}), {1, 0, 0});
    rejects([&] { root_checked({{}, {0, 0, 0, 0}}); });
    rejects([&] { root_checked({{std::numeric_limits<double>::infinity(), 0, 0}}); });
    rejects([&] { root_interval(identity, identity, identity, quarter, true, 0, -1, 1); });
    rejects([&] { root_interval(identity, identity, identity, quarter, true, 0, 1, 0); });
    rejects([&] { root_interval(identity, identity, identity, quarter, true, 0, 4294967296., 1); });
    rejects([&] { root_blend(identity, quarter, std::numeric_limits<double>::quiet_NaN()); });
}
} // namespace forge::test
