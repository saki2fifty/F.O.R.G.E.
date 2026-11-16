#include "physics_2d_one_way_tests.hpp"
#include "physics_2d_polygon_one_way_tests.hpp"
#include <iostream>

using namespace forge;
using namespace forge::physics_detail;
using namespace forge::test;
namespace {
void crossing(double origin, double degrees, float sign, unsigned motion, unsigned shape,
              bool painted, bool front, bool fast, double platform_speed = 0) {
    const double radians = degrees * std::numbers::pi / 180;
    const Double2 normal{-std::sin(radians) * sign, std::cos(radians) * sign};
    const LocalRotation rotation{0, 0, float(std::sin(radians / 2)), float(std::cos(radians / 2))};
    MoverNativeFixture test;
    PhysicsBody2D platform;
    platform.one_way = platform.sensor = true;
    platform.motion = motion;
    const auto sensor =
        test.add(one_way_floor(platform, painted, sign), origin - (painted ? 7 * normal[0] : 0),
                 origin - (painted ? 7 * normal[1] : 0), rotation);
    test.desired.at(test.id(sensor)).pose.scale.y = sign;
    PhysicsBody2D body;
    body.motion = 2;
    body.layer = 1;
    body.allow_sleeping = false;
    body.lock_rotation = true;
    body.bullet = fast;
    const auto dimensions = shape == 0   ? std::array{.4f, .5f}
                            : shape == 1 ? std::array{.25f, 0.f}
                                         : std::array{.2f, .3f};
    const double start = front ? 2 : -2;
    const auto actor = test.add(inline_configuration_2d(body, shape, dimensions, {1, 1, 1}),
                                origin + start * normal[0], origin + start * normal[1], rotation);
    test.synchronize();
    const Double2 ray_start{origin - 3 * normal[0], origin - 3 * normal[1]};
    const Double2 ray_displacement{6 * normal[0], 6 * normal[1]};
    const auto hit = test.native.raycast(ray_start, ray_displacement, {1, true});
    check_mover(hit && hit->entity == sensor &&
                    !test.native.raycast(ray_start, ray_displacement, {1, false}),
                "Directional sensor changed geometric query inclusion");
    const double speed = (front ? -1 : 1) * (fast ? 80 : 4);
    one_way_velocity(test, actor, {speed * normal[0], speed * normal[1]});
    unsigned begins = 0, ends = 0;
    for (unsigned step = 0; step != (fast ? 8u : 90u); ++step) {
        auto& target = test.desired.at(test.id(sensor)).pose.translation;
        target.x += platform_speed * normal[0] / 60;
        target.y += platform_speed * normal[1] / 60;
        test.advance();
        for (std::size_t i = 0; i < test.native.event_count(); ++i) {
            const auto event = test.native.event(i);
            check_mover(event && event->sensor && event->first == sensor &&
                            event->second == actor && event->tick == test.tick,
                        "Directional sensor changed copied event roles/tick");
            begins += event->begin;
            ends += !event->begin;
        }
    }
    const auto state = test.native.state(test.id(actor), actor);
    const double distance =
        (state->position[0] - origin) * normal[0] + (state->position[1] - origin) * normal[1];
    if (begins != unsigned(front) || ends != unsigned(front)) {
        std::cerr << "Sensor crossing " << origin << ' ' << degrees << ' ' << sign << ' ' << motion
                  << ' ' << shape << ' ' << painted << ' ' << front << ' ' << fast
                  << ": begin=" << begins << " end=" << ends << '\n';
        throw std::runtime_error(
            "Directional sensor lost entry/separation or reported back passage");
    }
    check_mover(front ? distance < -1 : distance > 1,
                "Directional sensor applied solid response to its visitor");
}
struct SensorFixture : MoverNativeFixture {
    EntityRef sensor, actor;
    SensorFixture() {
        PhysicsBody2D region;
        region.one_way = region.sensor = true;
        sensor = add(one_way_floor(region, false), 0, 0);
        PhysicsBody2D visitor;
        visitor.motion = 2;
        visitor.allow_sleeping = false;
        actor = add(inline_configuration_2d(visitor, 1, {.25f, 0}, {1, 1, 1}), 0, 2);
        synchronize();
        one_way_velocity(*this, actor, {0, -2});
    }
    void enter() {
        bool entered = false;
        for (unsigned i = 0; i < 120 && !entered; ++i) {
            advance();
            entered = native.event_count() != 0;
        }
        check_mover(entered && native.event_count() == 1 && native.event(0)->begin,
                    "Sensor fixture did not enter its directional overlap");
    }
};
void recovery() {
    SensorFixture test;
    test.enter();
    const auto sensor = test.sensor, actor = test.actor;
    const auto image = test.image();
    MoverNativeFixture recovered(test.config);
    recovered.desired.emplace(101, test.desired.at(test.id(sensor)));
    recovered.desired.emplace(202, test.desired.at(test.id(actor)));
    recovered.tick = test.tick;
    recovered.native.restore(image.metadata, image.bytes, recovered.desired, recovered.tick);
    check_mover(recovered.native.event_count() == 0,
                "Sensor recovery replayed a previously adopted Begin");
    unsigned begins = 0, ends = 0;
    for (unsigned i = 0; i != 60; ++i) {
        test.advance();
        recovered.advance();
        check_mover(test.native.state(test.id(actor), actor) ==
                            recovered.native.state(recovered.id(actor), actor) &&
                        test.native.event_count() == recovered.native.event_count(),
                    "Recovered sensor diverged from uninterrupted motion/events");
        for (std::size_t j = 0; j != test.native.event_count(); ++j) {
            const auto event = test.native.event(j);
            check_mover(event == recovered.native.event(j),
                        "Recovered sensor changed canonical identities/epochs/tick");
            begins += event->begin;
            ends += !event->begin;
        }
    }
    check_mover(begins == 0 && ends == 1,
                "Recovered sensor lost overlap continuity or separation End");
}
void lifecycle() {
    SensorFixture test;
    test.enter();
    one_way_velocity(test, test.actor, {0, 0});
    for (unsigned i = 0; i < 10; ++i) {
        test.advance();
        check_mover(test.native.event_count() == 0,
                    "Stationary admitted overlap repeated Begin or lost continuity");
    }
    one_way_velocity(test, test.actor, {0, 2});
    unsigned ends = 0;
    for (unsigned i = 0; i < 60; ++i) {
        test.advance();
        for (std::size_t j = 0; j < test.native.event_count(); ++j) {
            check_mover(!test.native.event(j)->begin,
                        "Reversing an admitted sensor invented another Begin");
            ++ends;
        }
    }
    check_mover(ends == 1, "Reversed sensor passage lost geometric separation End");
    one_way_velocity(test, test.actor, {0, -2});
    test.enter();
    const auto previous_epoch = test.native.event(0)->first_epoch;
    test.desired.at(test.id(test.sensor)).config.body.mask = 0;
    test.advance();
    check_mover(test.native.event_count() == 1 && !test.native.event(0)->begin &&
                    test.native.event(0)->first == test.sensor &&
                    test.native.event(0)->first_epoch == previous_epoch &&
                    test.native.state(test.id(test.sensor), test.sensor)->epoch != previous_epoch,
                "Sensor filter replacement lost the retired overlap's original epoch/End");
    for (unsigned i = 0; i < 30; ++i) {
        test.advance();
        check_mover(test.native.event_count() == 0, "Masked directional sensor admitted a visitor");
    }
}
void character_passage() {
    for (bool front : {false, true}) {
        MoverNativeFixture test;
        PhysicsBody2D region;
        region.one_way = region.sensor = true;
        region.one_way_margin = .2f;
        const auto sensor = test.add(one_way_floor(region, false), 0, 0);
        const auto actor = test.actor(0, front ? 2 : -2);
        test.synchronize();
        test.native.move_mover(test.id(actor), actor, {0, front ? -2. : 2.});
        unsigned begins = 0, ends = 0;
        for (unsigned i = 0; i < 150; ++i) {
            test.advance();
            for (std::size_t j = 0; j < test.native.event_count(); ++j) {
                const auto event = test.native.event(j);
                check_mover(event->sensor && event->first == sensor && event->second == actor,
                            "Character sensor passage lost copied event roles");
                begins += event->begin;
                ends += !event->begin;
            }
        }
        if (begins != unsigned(front) || ends != unsigned(front)) {
            std::cerr << "Character sensor front=" << front << " begin=" << begins
                      << " end=" << ends << '\n';
            throw std::runtime_error("Character sensor used stationary native velocity");
        }
    }
}
void custom_boundaries() {
    auto source = one_way_contour_source();
    source.document["next_id"] = 4;
    source.document["tiles"].push_back(
        {{"id", 3}, {"name", "Sloped sensor"}, {"image", 1}, {"frame", 0}});
    source.set_collision(3, {{"kind", "polygon"}, {"points", {{0, 0}, {1, 0}, {1, .5}, {0, 1}}}});
    const auto definitions = decode_cooked_tile_set(cook_tile_set(source), source.asset());
    unsigned cases = 0;
    for (std::uint64_t tile : {2u, 3u})
        for (unsigned flags = 0; flags != 8; ++flags) {
            auto value = TileMapValue::create(AssetId::generate(), source.asset());
            auto metadata = value.metadata();
            metadata["cell_size"] = {4, 4};
            value = value.with_metadata(metadata);
            const TileMapCellEdit edit{1, {0, 0}, {tile, flags}};
            value = value.paint(std::span(&edit, 1));
            const auto map = decode_cooked_tile_map(cook_tile_map(value), value.asset());
            for (float xs : {-1.f, 1.f})
                for (float ys : {-1.f, 1.f})
                    for (unsigned motion : {0u, 1u})
                        for (unsigned probe = 0; probe != (tile == 2 ? 3u : 1u); ++probe)
                            for (bool front : {false, true})
                                for (bool fast : {false, true}) {
                                    MoverNativeFixture test;
                                    PhysicsBody2D region;
                                    region.one_way = region.sensor = true;
                                    region.motion = motion;
                                    const LocalScale scale{xs, ys, 1};
                                    const auto config = tile_map_configuration_2d(
                                        region, map, 1, scale, &definitions);
                                    const auto sensor = test.add(config, 0, 0);
                                    test.desired.at(test.id(sensor)).pose.scale = scale;
                                    PhysicsBody2D body;
                                    body.motion = 2;
                                    body.allow_sleeping = false;
                                    body.bullet = fast;
                                    const double x = tile == 3 || probe == 2 ? 2
                                                     : probe == 1            ? 1.2
                                                                             : .4;
                                    const double y = !front       ? -1.5
                                                     : probe == 1 ? 2
                                                     : probe == 2 ? 3
                                                                  : 6;
                                    const auto actor = test.add(
                                        inline_configuration_2d(body, 1, {.15f, 0}, {1, 1, 1}),
                                        x * xs, y * ys);
                                    test.synchronize();
                                    one_way_velocity(test, actor,
                                                     {0, (front ? -1 : 1) * (fast ? 80 : 4) * ys});
                                    unsigned begins = 0, ends = 0;
                                    for (unsigned i = 0; i != (fast ? 12u : 180u); ++i) {
                                        test.advance();
                                        for (std::size_t j = 0; j < test.native.event_count();
                                             ++j) {
                                            const auto event = test.native.event(j);
                                            check_mover(event->sensor && event->first == sensor &&
                                                            event->second == actor,
                                                        "Custom sensor lost native event roles");
                                            begins += event->begin;
                                            ends += !event->begin;
                                        }
                                    }
                                    if ((begins != 0) != front || begins != ends) {
                                        std::cerr << "Custom sensor " << tile << ' ' << flags << ' '
                                                  << xs << ' ' << ys << ' ' << motion << ' '
                                                  << probe << ' ' << front << ' ' << fast
                                                  << " begin=" << begins << " end=" << ends << '\n';
                                        throw std::runtime_error(
                                            "Custom sensor lost original-front "
                                            "admission/lifecycle");
                                    }
                                    ++cases;
                                }
        }
    std::cout << cases << " cooked custom slope/hole/island/orientation sensor cases passed\n";
}
void placement_resets() {
    for (bool move_sensor : {false, true}) {
        SensorFixture test;
        test.enter();
        const auto target = move_sensor ? test.sensor : test.actor;
        BodyCommand command{};
        command.kind = BodyCommandKind::Teleport;
        command.ref = target;
        command.planar = true;
        command.p = {
            0, move_sensor ? test.native.state(test.id(test.actor), test.actor)->position[1] : 0,
            7};
        std::vector<std::uint64_t> snaps;
        test.native.apply(test.id(target), command, snaps);
        test.desired.at(test.id(target)).pose.translation = command.p;
        test.adopt();
        test.advance();
        check_mover(test.native.event_count() == 1 && !test.native.event(0)->begin,
                    "Discontinuous placement inherited an old sensor overlap witness");
        for (unsigned i = 0; i != 3; ++i) {
            test.advance();
            check_mover(test.native.event_count() == 0,
                        "Deep placed overlap invented new front-side eligibility");
        }
    }
}
void capacities() {
    const auto prepare = [](MoverNativeFixture& test, unsigned count, unsigned visitors) {
        Body2DConfiguration region;
        region.body.one_way = region.body.sensor = true;
        region.shape = 3;
        auto parts = std::make_shared<std::vector<BoxPart2D>>();
        for (unsigned i = 0; i != count; ++i)
            parts->push_back({{float(i) * .02f - 41, 0}, {.01f, .01f}});
        region.parts = std::move(parts);
        test.add(region, 0, 0);
        PhysicsBody2D body;
        body.motion = 2;
        body.sensor = visitors > 1;
        body.allow_sleeping = false;
        for (unsigned i = 0; i != visitors; ++i)
            test.add(inline_configuration_2d(body, 0, {100, 1}, {1, 1, 1}), 0, .5);
        test.synchronize();
    };
    MoverNativeFixture maximum;
    prepare(maximum, 4095, 1);
    maximum.advance();
    check_mover(maximum.native.shape_size() == 4096 && maximum.native.event_count() == 4095,
                "Directional sensor lost complete maximum-shape pair admission");
    for (unsigned i = 0; i != 3; ++i) {
        maximum.advance();
        check_mover(maximum.native.event_count() == 0,
                    "Maximum directional overlap snapshot exceeded work or replayed Begin");
    }
    MoverNativeFixture overflow;
    prepare(overflow, 4094, 2);
    check_mover(overflow.native.shape_size() == 4096,
                "Directional pair overflow fixture exceeded shape admission first");
    const auto refused = [](auto&& operation) {
        try {
            operation();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Directional sensor pair overflow adopted partial state");
    };
    refused([&] { overflow.advance(); });
    refused([&] { overflow.native.adopt_events(overflow.tick); });
    refused([&] { overflow.image(); });
    std::cout << "Directional sensor maximum4096shapes/4095pairs and overflow guards passed\n";
}
} // namespace
int main() {
    try {
        unsigned cases = 0;
        for (double origin : {0., -999900., 999900.})
            for (double degrees : {0., 30.})
                for (float sign : {-1.f, 1.f})
                    for (unsigned motion : {0u, 1u})
                        for (unsigned shape = 0; shape != 3; ++shape)
                            for (bool painted : {false, true})
                                for (bool front : {false, true})
                                    for (bool fast : {false, true}) {
                                        crossing(origin, degrees, sign, motion, shape, painted,
                                                 front, fast);
                                        ++cases;
                                    }
        std::cout << cases << " native directional sensor crossing/query cases passed\n";
        recovery();
        std::cout << "Directional sensor fresh recovery/native overlap continuation passed\n";
        lifecycle();
        for (bool painted : {false, true})
            for (float sign : {-1.f, 1.f})
                for (double speed : {-1., 1.})
                    for (bool front : {false, true})
                        crossing(0, 30, sign, 1, 1, painted, front, false, speed);
        std::cout << "Directional sensor reversal/filter/retirement and16moving cases passed\n";
        character_passage();
        custom_boundaries();
        placement_resets();
        capacities();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
