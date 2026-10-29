#pragma once
#include "reflection_source_plan.hpp"
inline void test_reflection_source_plan() {
    const auto check = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Source plan proof failed");
    };
    const auto rejects = [&](unsigned w, unsigned h) {
        try {
            (void)forge::reflection_source_plan(w, h);
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Unbounded source plan admitted");
    };
    const auto one = forge::reflection_source_plan(1, 1);
    check(one.count == 0 && one.bytes == 0);
    const auto odd = forge::reflection_source_plan(3, 3);
    check(odd.count == 2 && odd.bytes == 80 &&
          odd.levels[0] == forge::ReflectionSourceExtent{2, 2} &&
          odd.levels[1] == forge::ReflectionSourceExtent{1, 1});
    const auto thin = forge::reflection_source_plan(1, 3);
    check(thin.count == 2 && thin.bytes == 48);
    const auto maximum = forge::reflection_source_plan(65536, 1);
    check(maximum.count == 6 && maximum.bytes == 1032192 &&
          maximum.levels[5] == forge::ReflectionSourceExtent{1024, 1});
    const auto oddwide = forge::reflection_source_plan(65537, 1);
    check(oddwide.count == 6 && oddwide.bytes == 1032288 &&
          oddwide.levels[5] == forge::ReflectionSourceExtent{1025, 1});
    rejects(0, 1);
    rejects(1, 0);
    rejects(UINT32_MAX, UINT32_MAX);
}
