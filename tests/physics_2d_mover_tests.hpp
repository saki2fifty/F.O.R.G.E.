#pragma once
#include "physics_2d.hpp"
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace forge::test {
inline void check_mover(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct MoverGeometryFixture {
    static PhysicsConfig settings() {
        PhysicsConfig value;
        value.gravity = {};
        value.layers[1] = "Mover target";
        return value;
    }
    PhysicsConfig config = settings();
    physics_detail::Native2D native{config};
    physics_detail::Desired2D desired;
    AssetId scene = AssetId::generate();
    EntityRef add(physics_detail::Body2DConfiguration config, double x, double y,
                  LocalRotation rotation = {}) {
        const EntityRef ref{scene, EntityId::generate()};
        LocalTransform pose;
        pose.translation = {x, y, 7};
        pose.rotation = rotation;
        desired.emplace(desired.size() + 1, physics_detail::Body2DDesired{ref, config, pose});
        return ref;
    }
    void synchronize() {
        native.validate(desired);
        std::vector<std::uint64_t> snaps;
        native.synchronize(desired, desired, 0, {}, snaps);
    }
    physics_detail::Native2DImage image() { return native.checkpoint(desired, 2 * 1024 * 1024); }
};
inline PhysicsSweep2D mover_request(Double2 origin = {}) {
    PhysicsSweep2D value;
    value.shape = PhysicsOverlap2D::Shape::Capsule;
    value.dimensions = {.3, .6};
    value.origin = origin;
    value.displacement = {6, 0};
    return value;
}
inline void test_mover_geometry() {
    using namespace physics_detail;
    {
        MoverGeometryFixture empty;
        auto request = mover_request();
        check_mover(!empty.native.mover_cast(request, {}), "Empty mover query invented a hit");
        const auto rejects = [&](PhysicsSweep2D value) {
            bool rejected = false;
            try {
                (void)empty.native.mover_cast(value, {0, false});
            } catch (const std::exception&) {
                rejected = true;
            }
            check_mover(rejected, "Invalid mover request accepted through empty/masked fast path");
        };
        request.shape = PhysicsOverlap2D::Shape::Circle;
        rejects(request);
        request = mover_request();
        request.ignore = EntityRef{};
        rejects(request);
        request = mover_request();
        request.displacement[0] = std::numeric_limits<double>::quiet_NaN();
        rejects(request);
        request = mover_request({1000000, 0});
        rejects(request);
        request = mover_request();
        request.dimensions[0] = 0;
        rejects(request);
        check_mover(!empty.native.realized(), "Empty or invalid mover query realized a world");
    }
    for (double offset : {0., -999990., 999990.}) {
        for (unsigned shape = 0; shape != 4; ++shape) {
            MoverGeometryFixture test;
            PhysicsBody2D body;
            body.mask = 0; // Query layers are independent of physical pair masks.
            Body2DConfiguration compound;
            compound.body = body;
            compound.shape = 3;
            compound.parts = std::make_shared<const std::vector<BoxPart2D>>(
                std::vector<BoxPart2D>{{{0, 0}, {.2f, 4}}, {{1, 0}, {.2f, 4}}});
            const auto own = test.add(compound, offset - 3, offset);
            auto sensor = body;
            sensor.sensor = true;
            const auto sensor_ref =
                test.add(inline_configuration_2d(sensor, 0, {.2f, 4}, {1, 1, 1}), offset, offset);
            constexpr std::array<std::array<float, 2>, 3> dimensions{
                {{.2f, 4}, {.1f, 0}, {.1f, .6f}}};
            auto target =
                shape == 3 ? compound
                           : inline_configuration_2d(body, shape, dimensions.at(shape), {1, 1, 1});
            target.body.layer = 1;
            const auto target_ref = test.add(target, offset + 2, offset);
            const auto far_ref = test.add(inline_configuration_2d(body, 0, {.2f, 4}, {1, 1, 1}),
                                          offset + 2.7, offset);
            test.synchronize();
            const auto before = test.image();
            auto request = mover_request({offset - 3, offset});
            request.ignore = own;
            const auto hit = test.native.mover_cast(request, {3, false});
            check_mover(hit && hit->entity == target_ref &&
                            std::abs(hit->fraction - .7675) < 1e-5 && hit->normal[0] < -.99,
                        "Mover lost filtered inline/compound hit or double-origin local frame");
            const auto regular = test.native.shape_cast(request, {3, false});
            check_mover(regular && regular->entity == target_ref &&
                            hit->fraction > regular->fraction + 1e-4 &&
                            hit->fraction - regular->fraction < .005,
                        "Mover lost native encroachment relative to the ordinary sweep profile");
            const auto filtered = test.native.mover_cast(request, {1, false});
            check_mover(filtered && filtered->entity == far_ref && filtered->fraction > .88,
                        "Mover ignored the requested named-layer mask");
            const auto included = test.native.mover_cast(request, {3, true});
            check_mover(included && included->entity == sensor_ref && included->fraction < .44,
                        "Mover sensor policy could not be selected explicitly");
            check_mover(!test.native.mover_cast(request, {0, false}), "Empty mover mask hit");
            request.ignore = target_ref;
            check_mover(!test.native.mover_cast(request, {2, false}),
                        "Mover exclusion left an inline or compound body part behind");
            const auto after = test.image();
            check_mover(before.metadata == after.metadata && before.bytes == after.bytes,
                        "Read-only mover queries changed native state or recovery metadata");
        }
        {
            MoverGeometryFixture test;
            const auto ref = test.add(inline_configuration_2d({}, 0, {.2f, 4}, {1, 1, 1}), offset,
                                      offset, rotation_about_axis({0, 0, 1}, 37));
            test.synchronize();
            const auto hit = test.native.mover_cast(mover_request({offset - 3, offset}), {});
            check_mover(hit && hit->entity == ref && hit->normal[0] < -.75 &&
                            hit->normal[1] < -.5 &&
                            std::abs(std::hypot(hit->normal[0], hit->normal[1]) - 1) < 1e-5 &&
                            std::abs(hit->position[0] - offset) < 1 &&
                            std::abs(hit->position[1] - offset) < 1,
                        "Mover hit point/normal did not return from the rotated native frame");
        }
        {
            MoverGeometryFixture test;
            const auto ref =
                test.add(inline_configuration_2d({}, 1, {2, 0}, {1, 1, 1}), offset, offset);
            test.synchronize();
            const auto before = test.image();
            const auto request = mover_request({offset, offset});
            const auto overlap = test.native.overlap(
                {request.shape, request.dimensions, request.origin, request.angle}, {});
            check_mover(overlap == std::vector<EntityRef>{ref},
                        "Deep-containment mover fixture is unexpectedly clear");
            const auto hit = test.native.mover_cast(request, {});
            check_mover(!hit || hit->fraction == 0,
                        "Contained mover cast reported a positive clear travel fraction");
            const auto after = test.image();
            check_mover(before.metadata == after.metadata && before.bytes == after.bytes,
                        "Contained mover cast changed the published native body");
        }
    }
}
inline void test_mover_directions() {
    using namespace physics_detail;
    constexpr std::array<Double2, 4> directions{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    for (double offset : {0., -999990., 999990.})
        for (double angle : {0., std::numbers::pi / 2})
            for (const auto direction : directions) {
                MoverGeometryFixture test;
                const auto ref =
                    test.add(inline_configuration_2d({}, 0, {1, 1}, {1, 1, 1}), offset, offset);
                test.synchronize();
                auto request =
                    mover_request({offset - 3 * direction[0], offset - 3 * direction[1]});
                request.angle = angle;
                request.displacement = {6 * direction[0], 6 * direction[1]};
                const auto hit = test.native.mover_cast(request, {});
                const bool along_segment = angle == 0 ? direction[1] != 0 : direction[0] != 0;
                const double extent = along_segment ? .6 : .3;
                const double expected = (3 - .5 - extent + .005) / 6;
                check_mover(hit && hit->entity == ref &&
                                std::abs(hit->fraction - expected) < 1e-5 &&
                                std::abs(hit->normal[0] + direction[0]) < 1e-5 &&
                                std::abs(hit->normal[1] + direction[1]) < 1e-5,
                            "Mover lost a negative/vertical/rotated-capsule sweep");
                request.displacement = {};
                check_mover(!test.native.mover_cast(request, {}),
                            "Stationary separated capsule invented a mover hit");
            }
}
inline void test_mover_capacity() {
    using namespace physics_detail;
    MoverGeometryFixture test;
    Body2DConfiguration compound;
    compound.shape = 3;
    auto parts = std::make_shared<std::vector<BoxPart2D>>();
    for (unsigned i = 0; i < 4096; ++i)
        parts->push_back({{float(i) * .02f, 0}, {.01f, 1}});
    compound.parts = std::move(parts);
    const auto ref = test.add(compound, 0, 0);
    test.synchronize();
    auto request = mover_request({-1, 0});
    request.displacement = {100, 0};
    const auto hit = test.native.mover_cast(request, {1, false});
    check_mover(test.native.shape_size() == 4096 && hit && hit->entity == ref &&
                    std::abs(hit->fraction - .007) < 1e-5,
                "Mover omitted the closest part at the complete native shape capacity");
    request.ignore = ref;
    check_mover(!test.native.mover_cast(request, {1, false}),
                "Mover exclusion left a shape of the4096-part compound body behind");
    const PhysicsOverlap2D overlap{PhysicsOverlap2D::Shape::Capsule, {.3, .6}, {0, 0}};
    check_mover(test.native.mover_penetration(overlap, {1, false}) > .29 &&
                    test.native.mover_penetration(overlap, {1, false}, ref) == 0,
                "Mover penetration omitted or failed to exclude a compound body");
}
inline void test_mover_penetration() {
    using namespace physics_detail;
    {
        MoverGeometryFixture empty;
        PhysicsOverlap2D request{PhysicsOverlap2D::Shape::Capsule, {.3, .6}, {0, 0}};
        check_mover(empty.native.mover_penetration(request, {}) == 0,
                    "Empty mover penetration probe invented a contact");
        const auto rejects = [&](PhysicsOverlap2D value, std::optional<EntityRef> ignore = {}) {
            bool rejected = false;
            try {
                (void)empty.native.mover_penetration(value, {0, false}, ignore);
            } catch (const std::exception&) {
                rejected = true;
            }
            check_mover(rejected, "Invalid penetration probe accepted through a masked fast path");
        };
        rejects(request, EntityRef{});
        request.shape = PhysicsOverlap2D::Shape::Circle;
        rejects(request);
        request.shape = PhysicsOverlap2D::Shape::Capsule;
        request.angle = std::numeric_limits<double>::quiet_NaN();
        rejects(request);
        request.angle = 0;
        request.origin[0] = 1000001;
        rejects(request);
        request.origin = {};
        request.dimensions[0] = 0;
        rejects(request);
        check_mover(!empty.native.realized(), "Invalid penetration probe realized a native world");
    }
    for (double offset : {0., -999990., 999990.}) {
        {
            MoverGeometryFixture test;
            PhysicsBody2D sensor;
            sensor.sensor = true;
            sensor.mask = 0;
            const auto sensor_ref =
                test.add(inline_configuration_2d(sensor, 1, {2, 0}, {1, 1, 1}), offset, offset);
            auto body = sensor;
            body.sensor = false;
            body.layer = 1;
            const auto solid_ref =
                test.add(inline_configuration_2d(body, 2, {.3f, .6f}, {1, 1, 1}), offset, offset);
            test.synchronize();
            const auto before = test.image();
            const PhysicsOverlap2D request{
                PhysicsOverlap2D::Shape::Capsule, {.3, .6}, {offset, offset}};
            check_mover(std::abs(test.native.mover_penetration(request, {3, false}) - .6) < 1e-5,
                        "Coincident solid capsule penetration was not measured");
            check_mover(std::abs(test.native.mover_penetration(request, {3, true}) - 2.3) < 1e-5,
                        "Zero-normal circle containment became a false clear placement");
            check_mover(test.native.mover_penetration(request, {1, false}) == 0 &&
                            test.native.mover_penetration(request, {3, false}, solid_ref) == 0,
                        "Penetration probe lost layer, sensor or body exclusion");
            check_mover(
                std::abs(test.native.mover_penetration(request, {3, true}, sensor_ref) - .6) < 1e-5,
                "Ignoring the sensor also hid a different solid body");
            auto wrong_scene = solid_ref;
            wrong_scene.scene = AssetId::generate();
            check_mover(std::abs(test.native.mover_penetration(request, {3, false}, wrong_scene) -
                                 .6) < 1e-5,
                        "Penetration exclusion compared only the entity half of a canonical ref");
            const auto after = test.image();
            check_mover(before.metadata == after.metadata && before.bytes == after.bytes,
                        "Penetration probes mutated native state or recovery metadata");
        }
        {
            MoverGeometryFixture test;
            test.add(inline_configuration_2d({}, 0, {10, 1}, {1, 1, 1}), offset, offset - .5);
            test.synchronize();
            PhysicsOverlap2D request{
                PhysicsOverlap2D::Shape::Capsule, {.3, .6}, {offset, offset + .59}};
            check_mover(std::abs(test.native.mover_penetration(request, {}) - .01) < 1e-5,
                        "Shallow floor penetration lost its signed metre separation");
            request.origin[1] = offset - .5;
            check_mover(test.native.mover_penetration(request, {}) > .5,
                        "Deep box overlap became a false clear placement");
            request.origin[1] = offset + 3;
            check_mover(test.native.mover_penetration(request, {}) == 0,
                        "Separated capsule reported positive penetration");
        }
        {
            MoverGeometryFixture test;
            constexpr double angle = 37 * std::numbers::pi / 180;
            test.add(inline_configuration_2d({}, 0, {10, 1}, {1, 1, 1}), offset, offset - .5,
                     rotation_about_axis({0, 0, 1}, 37));
            test.synchronize();
            const PhysicsOverlap2D request{
                PhysicsOverlap2D::Shape::Capsule,
                {.3, .6},
                {offset - 1.09 * std::sin(angle), offset - .5 + 1.09 * std::cos(angle)},
                angle};
            check_mover(std::abs(test.native.mover_penetration(request, {}) - .01) < 1e-5,
                        "Rotated native/query frames changed penetration depth");
        }
        {
            MoverGeometryFixture test;
            Body2DConfiguration compound;
            compound.shape = 3;
            compound.parts = std::make_shared<const std::vector<BoxPart2D>>(
                std::vector<BoxPart2D>{{{0, 0}, {1, 1}}, {{1, 0}, {1, 1}}});
            const auto ref = test.add(compound, offset, offset);
            test.synchronize();
            const PhysicsOverlap2D request{
                PhysicsOverlap2D::Shape::Capsule, {.3, .6}, {offset + .5, offset}};
            check_mover(std::abs(test.native.mover_penetration(request, {}) - .3) < 1e-5 &&
                            test.native.mover_penetration(request, {}, ref) == 0,
                        "Compound seam penetration or whole-body exclusion was lost");
        }
    }
}
} // namespace forge::test
