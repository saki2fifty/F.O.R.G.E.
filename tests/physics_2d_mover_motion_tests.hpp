#pragma once
#include "physics_2d_mover_tests.hpp"
#include <string>

namespace forge::test {
inline void check_mover_unchanged(MoverGeometryFixture& test,
                                  const physics_detail::Native2DImage& before) {
    const auto after = test.image();
    check_mover(before.metadata == after.metadata && before.bytes == after.bytes,
                "Read-only mover solve changed native simulation or recovery state");
}
inline void reject_mover_motion(MoverGeometryFixture& test, const PhysicsSweep2D& request,
                                std::string_view expected) {
    const auto before = test.image();
    std::string diagnostic;
    try {
        (void)test.native.mover_motion(request, {UINT32_MAX, false});
    } catch (const std::exception& error) {
        diagnostic = error.what();
    }
    if (diagnostic.find(expected) == std::string::npos)
        throw std::runtime_error("Mover rejection missing expected reason: " + diagnostic);
    check_mover_unchanged(test, before);
}
inline void test_mover_motion() {
    using namespace physics_detail;
    {
        MoverGeometryFixture empty;
        const auto request = mover_request();
        const auto result = empty.native.mover_motion(request, {});
        check_mover(result.translation == request.displacement &&
                        result.clipped_displacement == request.displacement &&
                        result.iterations == 0 && !empty.native.realized(),
                    "Free mover motion changed displacement or created a native world");
        for (unsigned invalid = 0; invalid != 3; ++invalid) {
            auto bad = request;
            if (invalid == 0)
                bad.ignore_initial_overlap = true;
            else if (invalid == 1)
                bad.dimensions[0] = .005;
            else
                bad.displacement[0] = 1001;
            bool rejected = false;
            try {
                (void)empty.native.mover_motion(bad, {0, false});
            } catch (const std::exception&) {
                rejected = true;
            }
            check_mover(rejected, "Motion profile was bypassed by an empty masked world");
        }
    }
    for (double offset : {0., -999990., 999990.}) {
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5);
            test.synchronize();
            const auto before = test.image();
            auto request = mover_request({offset, offset + .6});
            request.displacement = {1, -.1};
            auto motion = test.native.mover_motion(request, {UINT32_MAX, false});
            if (!(std::abs(motion.translation[0] - 1) < 1e-5 && motion.translation[1] >= -.00502 &&
                  motion.translation[1] <= .00002 &&
                  std::abs(motion.clipped_displacement[0] - 1) < 1e-5 &&
                  std::abs(motion.clipped_displacement[1]) < 1e-5))
                throw std::runtime_error("Mover floor response: " +
                                         Json{{"origin", request.origin},
                                              {"translation", motion.translation},
                                              {"clipped", motion.clipped_displacement},
                                              {"iterations", motion.iterations}}
                                             .dump());
            request.origin[1] = offset + 3;
            request.displacement = {0, -4};
            motion = test.native.mover_motion(request, {UINT32_MAX, false});
            check_mover(std::abs(3 + motion.translation[1] - .595) < 1e-4 &&
                            std::abs(motion.clipped_displacement[1]) < 1e-5 &&
                            motion.iterations > 0 && motion.iterations <= 8,
                        "Mover fell through the floor or retained blocked vertical motion");
            request.origin[1] = offset + .59;
            request.displacement = {};
            motion = test.native.mover_motion(request, {UINT32_MAX, false});
            check_mover(std::abs(motion.translation[1] - .005) < 1e-5 &&
                            motion.clipped_displacement == Double2{},
                        "Shallow recovery injected velocity or failed to reach native slop");
            check_mover_unchanged(test, before);
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {1, 8}, {1, 1, 1}), offset + .5, offset + 2);
            PhysicsBody2D sensor;
            sensor.sensor = true;
            test.add(inline_configuration_2d(sensor, 0, {.1f, 8}, {1, 1, 1}), offset - 1,
                     offset + 2);
            test.synchronize();
            const auto before = test.image();
            auto request = mover_request({offset - 2, offset + 1});
            request.displacement = {4, 3};
            const auto motion = test.native.mover_motion(request, {UINT32_MAX, false});
            check_mover(std::abs(-2 + motion.translation[0] + .295) < 1e-4 &&
                            std::abs(motion.translation[1] - 3) < 1e-4 &&
                            std::abs(motion.clipped_displacement[0]) < 1e-5 &&
                            std::abs(motion.clipped_displacement[1] - 3) < 1e-4,
                        "Mover could not slide along a wall past a sensor");
            check_mover_unchanged(test, before);
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5);
            test.add(inline_configuration_2d({}, 0, {1, 8}, {1, 1, 1}), offset + .5, offset + 2);
            test.synchronize();
            const auto before = test.image();
            auto request = mover_request({offset - 2, offset + 2});
            request.displacement = {4, -3};
            const auto motion = test.native.mover_motion(request, {UINT32_MAX, false});
            check_mover(std::abs(-2 + motion.translation[0] + .295) < 1e-4 &&
                            std::abs(2 + motion.translation[1] - .595) < 1e-4 &&
                            std::hypot(motion.clipped_displacement[0],
                                       motion.clipped_displacement[1]) < 1e-5,
                        "Mover corner constraints did not stop both blocked directions");
            check_mover_unchanged(test, before);
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 1, {2, 0}, {1, 1, 1}), offset, offset);
            test.synchronize();
            auto request = mover_request({offset, offset});
            request.displacement = {};
            reject_mover_motion(test, request, "deeply embedded");
        }
        {
            MoverGeometryFixture test;
            const auto wall = inline_configuration_2d({}, 0, {1, 4}, {1, 1, 1});
            test.add(wall, offset - .79, offset);
            test.add(wall, offset + .79, offset);
            test.synchronize();
            auto request = mover_request({offset, offset});
            request.displacement = {};
            reject_mover_motion(test, request, "infeasible");
        }
    }
}
inline void test_mover_motion_surfaces() {
    using namespace physics_detail;
    const float floor_y = float(std::cos(50 * std::numbers::pi / 180));
    for (double offset : {0., -999990., 999990.}) {
        for (double degrees : {30., 75., -75.}) {
            MoverGeometryFixture test;
            const double angle = degrees * std::numbers::pi / 180;
            const Double2 normal{-std::sin(angle), std::cos(angle)};
            const double extent = .3 + .3 * normal[1];
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5,
                     rotation_about_axis({0, 0, 1}, float(degrees)));
            test.synchronize();
            const auto before = test.image();
            auto request = mover_request(
                {offset + normal[0] * (.5 + extent), offset - .5 + normal[1] * (.5 + extent)});
            request.displacement =
                degrees == 30 ? Double2{1, -.1} : Double2{std::copysign(.1, degrees), -.01};
            const auto floating = test.native.mover_motion(request, {UINT32_MAX, false});
            const auto grounded = test.native.mover_motion(request, {UINT32_MAX, false}, floor_y);
            if (degrees == 30) {
                check_mover(floating.translation[0] > .5 && floating.translation[1] > .25 &&
                                std::abs(floating.translation[0] - grounded.translation[0]) <
                                    1e-5 &&
                                std::abs(floating.translation[1] - grounded.translation[1]) < 1e-5,
                            "Walkable slope was blocked by grounded motion policy");
            } else {
                check_mover(
                    floating.translation[1] > .005 && floating.clipped_displacement[1] > .005 &&
                        grounded.translation[1] <= 1e-5 && grounded.clipped_displacement[1] <= 1e-5,
                    "Steep face converted grounded horizontal intent into upward climbing");
                request.displacement[1] = .1;
                const auto rising = test.native.mover_motion(request, {UINT32_MAX, false}, floor_y);
                check_mover(rising.translation[1] > .05,
                            "Grounded slope policy suppressed explicitly requested upward motion");
            }
            check_mover_unchanged(test, before);
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset + 2.5);
            test.synchronize();
            auto request = mover_request({offset, offset + .6});
            request.displacement = {1, 3};
            const auto motion = test.native.mover_motion(request, {UINT32_MAX, false}, floor_y);
            check_mover(std::abs(motion.translation[0] - 1) < 1e-4 &&
                            std::abs(.6 + motion.translation[1] - 1.405) < 1e-4 &&
                            std::abs(motion.clipped_displacement[1]) < 1e-5,
                        "Mover did not clip an upward request against the ceiling");
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {20, 1}, {1, 1, 1}), offset, offset - .5);
            test.add(inline_configuration_2d({}, 0, {1, 8}, {1, 1, 1}), offset + .5, offset + 2);
            test.synchronize();
            auto request = mover_request({offset - 2, offset + 2});
            request.angle = std::numbers::pi / 2;
            request.displacement = {4, -3};
            const auto motion = test.native.mover_motion(request, {UINT32_MAX, false});
            check_mover(std::abs(-2 + motion.translation[0] + .595) < 1e-4 &&
                            std::abs(2 + motion.translation[1] - .295) < 1e-4,
                        "Rotated capsule motion used upright extents in a corner");
        }
    }
}
inline void test_mover_motion_sleep() {
    using namespace physics_detail;
    MoverGeometryFixture test;
    PhysicsBody2D dynamic;
    dynamic.motion = 2;
    const auto ref = test.add(inline_configuration_2d(dynamic, 0, {1, 4}, {1, 1, 1}), 0, 0);
    test.synchronize();
    for (std::uint64_t tick = 1; tick <= 180; ++tick) {
        test.native.expire_events();
        test.native.step(1.f / 60, tick);
        const auto poses = test.native.adopted_poses();
        test.native.adopt_events(tick);
        for (const auto& [id, pose] : poses)
            test.desired.at(id).pose = pose;
        test.native.accept_poses(poses);
    }
    const auto state = test.native.state(1, ref);
    check_mover(state && !state->active, "Mover obstacle did not enter native sleep");
    const auto before = test.image();
    const auto motion = test.native.mover_motion(mover_request({-3, 0}), {UINT32_MAX, false});
    check_mover(motion.translation[0] > 2 && motion.translation[0] < 2.3 &&
                    test.native.state(1, ref) == state,
                "Read-only motion solve woke or moved a sleeping Dynamic obstacle");
    check_mover_unchanged(test, before);
}
inline void test_mover_motion_limits() {
    using namespace physics_detail;
    {
        MoverGeometryFixture test;
        for (unsigned i = 0; i < 65; ++i) {
            const double angle = std::numbers::pi + std::numbers::pi * (i + .5) / 65;
            test.add(inline_configuration_2d({}, 1, {.02f, 0}, {1, 1, 1}), .315 * std::cos(angle),
                     -.3 + .315 * std::sin(angle));
        }
        test.synchronize();
        auto request = mover_request();
        request.displacement = {};
        reject_mover_motion(test, request, "plane capacity 64");
    }
    {
        MoverGeometryFixture test;
        Body2DConfiguration compound;
        compound.shape = 3;
        compound.parts =
            std::make_shared<const std::vector<BoxPart2D>>(4096, BoxPart2D{{0, -.5f}, {1, 1}});
        const auto ref = test.add(compound, 0, 0);
        test.synchronize();
        auto request = mover_request({0, .59});
        request.displacement = {.01, 0};
        request.ignore = ref;
        reject_mover_motion(test, request, "work capacity 16384");
    }
}
} // namespace forge::test
