#include "physics_2d.hpp"
#include <algorithm>
#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D observation/retirement operation was accepted");
}
Json row(std::string id, double x, double y, unsigned motion, bool sensor = false) {
    const Json components = {{"forge.local_translation", {{"x", x}, {"y", y}, {"z", 7}}},
                             {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                             {"forge.physics_body_2d", {{"motion", motion}, {"sensor", sensor}}},
                             {"forge.circle_collider_2d", {{"radius", .5}}}};
    return {{"id", id},
            {"name", "Observation body"},
            {"spatial", {{"mode", "world"}}},
            {"components", components}};
}
struct Fixture {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene{engine.world()};
    RuntimeSimulation simulation{engine.world(), scene};
    std::shared_ptr<PhysicsRuntime> physics =
        std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    std::string first = EntityId::generate().str(), second = EntityId::generate().str();
    explicit Fixture(bool gravity = false) {
        auto config = PhysicsConfig{};
        if (!gravity)
            config.gravity = {};
        physics->configure(config);
    }
    void load(Json entities) {
        scene.restore_snapshot(
            {{"version", 5}, {"asset_id", AssetId::generate()}, {"entities", entities}});
        physics->synchronize(0);
        simulation.reset_presentation();
    }
    EntityRef ref(const std::string& id) { return scene.reference(id); }
    void tick(unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i)
            simulation.tick(1.f / 60);
    }
};
void rays_and_overlaps() {
    for (double offset : {0., -999990., 999990.}) {
        Fixture test;
        auto sensor = row(test.first, offset, 2, 0, true);
        auto solid = row(test.second, offset, 0, 2);
        solid["components"]["forge.physics_body_2d"]["mask"] = 0;
        test.load(Json::array({sensor, solid}));
        const auto nearest = test.physics->raycast_2d({offset, 4}, {0, -8});
        require(nearest && nearest->entity == test.ref(test.first) &&
                    std::abs(nearest->fraction - .1875) < 1e-5,
                "2D closest ray lost sensor/position/fraction at a supported offset");
        const auto filtered = test.physics->raycast_2d({offset, 4}, {0, -8}, {UINT32_MAX, false});
        require(filtered && filtered->entity == test.ref(test.second) && filtered->normal[1] > .99,
                "Sensor exclusion or independent query/collision-mask adaptation failed");
        require(!test.physics->raycast_2d({offset, 4}, {0, -8}, {0, true}),
                "Empty 2D query mask hit");
        PhysicsOverlap2D overlap;
        overlap.origin = {offset, 1};
        overlap.dimensions = {3, 0};
        auto refs = test.physics->overlap_shape_2d(overlap);
        require(refs.size() == 2 && std::is_sorted(refs.begin(), refs.end()),
                "2D overlap did not return unique canonical ordering");
        refs = test.physics->overlap_shape_2d(overlap, {UINT32_MAX, false});
        require(refs == std::vector<EntityRef>{test.ref(test.second)},
                "2D overlap sensor exclusion failed");
        overlap.shape = PhysicsOverlap2D::Shape::Box;
        overlap.dimensions = {2, 6};
        overlap.angle = .3;
        require(test.physics->overlap_shape_2d(overlap).size() == 2, "Rotated box overlap failed");
        overlap.shape = PhysicsOverlap2D::Shape::Capsule;
        overlap.dimensions = {.5, 4};
        overlap.angle = 0;
        require(test.physics->overlap_shape_2d(overlap).size() == 2, "Capsule overlap failed");
        rejects([&] { test.physics->raycast_2d({offset, 4}, {}); });
        rejects([&] { test.physics->raycast_2d({1000001, 0}, {1, 0}); });
        overlap.dimensions[0] = 0;
        rejects([&] { test.physics->overlap_shape_2d(overlap); });
        test.tick(180);
        const auto sleeping = test.physics->body_state_2d(test.ref(test.second));
        require(sleeping && !sleeping->active, "Query fixture did not settle");
        (void)test.physics->raycast_2d({offset, 4}, {0, -8});
        require(test.physics->body_state_2d(test.ref(test.second)) == sleeping,
                "2D ray woke a sleeping body");
    }
    Fixture layered;
    auto config = PhysicsConfig{};
    config.gravity = {};
    config.layers[1] = "Other";
    layered.physics->configure(config);
    auto first = row(layered.first, 0, 2, 0), second = row(layered.second, 0, 0, 0);
    second["components"]["forge.physics_body_2d"]["layer"] = 1;
    layered.load(Json::array({first, second}));
    const auto selected = layered.physics->raycast_2d({0, 4}, {0, -8}, {2, false});
    require(selected && selected->entity == layered.ref(layered.second),
            "Named project query layer was ignored");
}
void swept_shapes() {
    Fixture empty;
    PhysicsSweep2D cast;
    require(!empty.physics->shape_cast_2d(cast), "Empty 2D sweep invented a hit");
    cast.dimensions[0] = 0;
    rejects([&] { empty.physics->shape_cast_2d(cast, {0, false}); });
    cast.dimensions[0] = .2;
    cast.ignore = EntityRef{};
    rejects([&] { empty.physics->shape_cast_2d(cast); });
    cast.ignore.reset();
    cast.displacement[0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { empty.physics->shape_cast_2d(cast); });
    cast.displacement = {};
    cast.origin = {1000000, 0};
    cast.displacement[0] = 1;
    rejects([&] { empty.physics->shape_cast_2d(cast, {0, true}); });
    require(!empty.physics->status().at("native_2d_realized").get<bool>(),
            "Empty or rejected sweep realized a world");
    std::array<double, 3> fractions{};
    for (double offset : {0., -999990., 999990.}) {
        Fixture test;
        const auto own_id = EntityId::generate().str();
        auto own = row(own_id, offset, 4, 2), sensor = row(test.first, offset, 2, 0, true),
             solid = row(test.second, offset, 0, 0);
        solid["components"]["forge.physics_body_2d"]["mask"] = 0;
        test.load(Json::array({own, sensor, solid}));
        for (unsigned kind = 0; kind < 3; ++kind) {
            cast = {};
            cast.shape = PhysicsSweep2D::Shape(kind);
            cast.dimensions =
                kind == 0 ? Double2{.4, .8} : (kind == 1 ? Double2{.2, 0} : Double2{.2, .6});
            cast.origin = {offset, 4};
            cast.displacement = {0, -8};
            const auto initial = test.physics->shape_cast_2d(cast);
            require(initial && initial->entity == test.ref(own_id) && initial->fraction == 0 &&
                        initial->normal == Double2{},
                    "2D sweep lost its native initial-overlap semantics");
            cast.ignore = test.ref(own_id);
            const auto sensor_hit = test.physics->shape_cast_2d(cast);
            require(sensor_hit && sensor_hit->entity == test.ref(test.first),
                    "2D sweep did not exclude its canonical body or include a sensor");
            const auto hit = test.physics->shape_cast_2d(cast, {UINT32_MAX, false});
            require(hit && hit->entity == test.ref(test.second) && hit->normal[1] > .99 &&
                        std::abs(hit->position[0] - offset) < .001 &&
                        hit->fraction > sensor_hit->fraction && hit->fraction < .5,
                    "Swept geometry, independent mask or sensor exclusion failed");
            if (offset == 0)
                fractions[kind] = hit->fraction;
            else
                require(std::abs(hit->fraction - fractions[kind]) < 1e-6,
                        "2D sweep lost precision at a supported distant origin");
            require(!test.physics->shape_cast_2d(cast, {0, true}), "Empty sweep mask hit");
            cast.ignore.reset();
            cast.ignore_initial_overlap = true;
            const auto beyond = test.physics->shape_cast_2d(cast, {UINT32_MAX, false});
            require(beyond && beyond->entity == hit->entity && beyond->fraction == hit->fraction,
                    "Ignoring an initial overlap clipped away a later solid hit");
            cast.displacement = {};
            require(!test.physics->shape_cast_2d(cast),
                    "Zero sweep failed to ignore the initial overlap");
            cast.ignore_initial_overlap = false;
            require(test.physics->shape_cast_2d(cast)->fraction == 0,
                    "Zero sweep lost its initial overlap");
        }
        test.tick(180);
        const auto own_ref = test.ref(own_id);
        const auto sleeping = test.physics->body_state_2d(own_ref);
        require(sleeping && !sleeping->active, "Sweep fixture did not settle");
        test.physics->set_linear_velocity_2d(own_ref, {1, 0});
        const auto before = test.physics->status();
        cast = {};
        cast.origin = {offset, 5};
        cast.displacement = {0, -10};
        (void)test.physics->shape_cast_2d(cast);
        require(test.physics->body_state_2d(own_ref) == sleeping &&
                    test.physics->status() == before,
                "Sweep flushed commands, advanced time or woke a sleeping body");
        test.tick();
        require(test.physics->body_state_2d(own_ref)->linear_velocity[0] > .9,
                "Sweep damaged the pending body command");
    }
    Fixture rotated;
    auto floor = row(rotated.first, 0, 0, 0);
    floor["components"].erase("forge.circle_collider_2d");
    floor["components"]["forge.box_collider_2d"] = {{"x", 20}, {"y", 1}};
    auto config = PhysicsConfig{};
    config.gravity = {};
    config.layers[1] = "Floor";
    rotated.physics->configure(config);
    floor["components"]["forge.physics_body_2d"]["layer"] = 1;
    rotated.load(Json::array({floor}));
    cast = {};
    cast.shape = PhysicsSweep2D::Shape::Box;
    cast.dimensions = {2, .2};
    cast.origin = {0, 5};
    cast.displacement = {0, -10};
    const auto flat = rotated.physics->shape_cast_2d(cast, {2, false});
    cast.angle = 1.5707963267948966;
    const auto upright = rotated.physics->shape_cast_2d(cast, {2, false});
    require(flat && upright && flat->fraction - upright->fraction > .08 &&
                !rotated.physics->shape_cast_2d(cast, {1, false}),
            "2D sweep ignored orientation or named layers");
    cast.ignore = EntityRef{AssetId::generate(), rotated.ref(rotated.first).entity};
    require(rotated.physics->shape_cast_2d(cast, {2, false}).has_value(),
            "2D sweep exclusion ignored the Scene part of canonical identity");
    cast.ignore = rotated.ref(rotated.first);
    require(!rotated.physics->shape_cast_2d(cast), "Single excluded body still hit");
    cast.ignore.reset();
    cast.shape = PhysicsSweep2D::Shape(3);
    rejects([&] { rotated.physics->shape_cast_2d(cast); });
    cast.shape = PhysicsSweep2D::Shape::Circle;
    rejects([&] { rotated.physics->shape_cast_2d(cast); }); // Unused height.
    cast.dimensions = {.005, 0};
    require(rotated.physics->shape_cast_2d(cast).has_value(), "Smallest circle sweep failed");
    cast.shape = PhysicsSweep2D::Shape::Box;
    cast.dimensions = {.01, .01};
    require(rotated.physics->shape_cast_2d(cast).has_value(), "Smallest box sweep failed");
    cast.shape = PhysicsSweep2D::Shape::Capsule;
    cast.dimensions = {.005, .01};
    require(rotated.physics->shape_cast_2d(cast).has_value(), "Small capsule sweep failed");
    cast.angle = std::numeric_limits<double>::infinity();
    rejects([&] { rotated.physics->shape_cast_2d(cast); });
}
void overlap_capacity() {
    Fixture test;
    Json rows = Json::array();
    for (unsigned i = 0; i < 257; ++i)
        rows.push_back(row(EntityId::generate().str(), double(i % 20) * 2, double(i / 20) * 2, 0));
    test.load(rows);
    PhysicsOverlap2D query;
    query.shape = PhysicsOverlap2D::Shape::Box;
    query.origin = {20, 20};
    query.dimensions = {100, 100};
    rejects([&] { test.physics->overlap_shape_2d(query); });
    query.shape = PhysicsOverlap2D::Shape::Circle;
    query.origin = {0, 0};
    query.dimensions = {.5, 0};
    require(test.physics->overlap_shape_2d(query).size() == 1,
            "Rejected broad overlap damaged later queries");
}
void steady_sensor_profile() {
    Fixture test;
    Json rows = Json::array();
    for (unsigned i = 0; i < 4; ++i) {
        auto sensor = row(EntityId::generate().str(), 0, 0, 0, true);
        sensor["components"].erase("forge.circle_collider_2d");
        sensor["components"]["forge.box_collider_2d"] = {{"x", 1000}, {"y", 1000}};
        rows.push_back(std::move(sensor));
    }
    for (unsigned i = 0; i < 1020; ++i)
        rows.push_back(
            row(EntityId::generate().str(), double(i % 32) * 2 - 32, double(i / 32) * 2 - 32, 2));
    test.load(rows);
    test.tick();
    const auto begins = test.physics->contact_count_2d();
    require(begins > 2048 && begins <= 4096,
            "Maximum-body sensor fixture missed its admitted overlap profile");
    std::cout << "Steady sensor profile initial begins=" << begins << '\n';
    test.tick(2);
    require(test.physics->contact_count_2d() == 0,
            "Unchanged admitted sensor overlaps replayed events");
    for (unsigned i = 4; i < rows.size(); ++i) {
        const auto entity = test.scene.entity(rows[i].at("id").get<std::string>());
        auto config = entity.get<PhysicsBody2D>();
        config.mass = 2;
        entity.set<PhysicsBody2D>(config);
    }
    test.tick();
    std::size_t begins_replacement = 0, ends_retired = 0;
    for (std::size_t i = 0; i < test.physics->contact_count_2d(); ++i) {
        const auto event = *test.physics->contact_event_2d(i);
        require(event.sensor && event.first_available,
                "Dense sensor transition lost its sensor binding");
        if (event.begin) {
            require(event.second_available, "Dense sensor Begin lost its replacement epoch");
            ++begins_replacement;
        } else {
            require(!event.second_available,
                    "Dense sensor End resolved through a replacement epoch");
            ++ends_retired;
        }
    }
    require(begins_replacement == 4080 && ends_retired == 4080,
            "Dense replacement transitions were truncated or replayed stable sensor pairs");
}
void sensor_epochs_and_expiration() {
    Fixture test;
    auto sensor = row(test.first, 0, 0, 0, true), visitor = row(test.second, 0, 0, 2);
    sensor["components"].erase("forge.circle_collider_2d");
    sensor["components"]["forge.box_collider_2d"] = {{"x", 4}, {"y", 4}};
    test.load(Json::array({sensor, visitor}));
    test.tick();
    require(test.physics->contact_count_2d() == 1, "Sensor Begin was not copied");
    const auto begin = test.physics->contact_event_2d(0);
    require(begin && begin->sensor && begin->begin && begin->first == test.ref(test.first) &&
                begin->second == test.ref(test.second) && begin->first_available &&
                begin->second_available,
            "Sensor/visitor identity, order or availability is wrong");
    require(test.physics->contact_event_2d(0) == begin && !test.physics->contact_event_2d(1),
            "Event slots consumed or fabricated data");
    test.scene.entity(test.second).set<LocalRotation>(rotation_about_axis({1, 0, 0}, 10));
    rejects([&] { test.physics->synchronize(0); });
    require(test.physics->contact_event_2d(0) == begin,
            "Rejected preflight expired adopted events");
    test.scene.entity(test.second).set<LocalRotation>({});
    auto config = test.scene.entity(test.second).get<PhysicsBody2D>();
    config.mass = 2;
    test.scene.entity(test.second).set<PhysicsBody2D>(config);
    test.physics->synchronize(0);
    require(test.physics->contact_count_2d() == 0,
            "Admitted replacement retained the old broadcast");
    const auto replacement = test.physics->body_state_2d(test.ref(test.second));
    require(replacement && replacement->epoch != begin->second_epoch,
            "Replacement reused the old epoch");
    test.tick();
    bool retired_end = false, replacement_begin = false;
    for (std::size_t i = 0; i < test.physics->contact_count_2d(); ++i) {
        const auto event = *test.physics->contact_event_2d(i);
        if (!event.begin && event.second_epoch == begin->second_epoch)
            retired_end = event.sensor && event.first_available && !event.second_available &&
                          event.second == begin->second;
        if (event.begin && event.second_epoch == replacement->epoch)
            replacement_begin = event.sensor && event.first_available && event.second_available;
    }
    require(retired_end && replacement_begin,
            "Retired native End resolved through a replacement epoch");
    test.scene.delete_subtree(test.second);
    for (std::size_t i = 0; i < test.physics->contact_count_2d(); ++i)
        require(!test.physics->contact_event_2d(i)->second_available,
                "Removed authored target remained available");
    test.tick();
    test.tick();
    require(test.physics->contact_count_2d() == 0, "Prior-tick native sensor events replayed");
    test.physics->step(1.f / 60);
    const auto state = test.physics->status();
    rejects([&] { test.physics->step(1.f / 60); });
    rejects([&] { test.physics->synchronize(0); });
    require(test.physics->status() == state,
            "Skipped-adoption rejection advanced the shared clock");
    test.physics->adopt();
    test.tick();
}
void solid_retirement() {
    Fixture test(true);
    auto floor = row(test.first, 0, -.5, 0), falling = row(test.second, 0, 2, 2);
    floor["components"].erase("forge.circle_collider_2d");
    floor["components"]["forge.box_collider_2d"] = {{"x", 20}, {"y", 1}};
    test.load(Json::array({floor, falling}));
    bool saw_begin = false;
    for (unsigned tick = 0; tick < 120; ++tick) {
        test.tick();
        for (std::size_t i = 0; i < test.physics->contact_count_2d(); ++i) {
            const auto event = *test.physics->contact_event_2d(i);
            saw_begin |=
                event.begin && !event.sensor && event.first_available && event.second_available;
        }
    }
    require(saw_begin, "Solid contact Begin was not copied");
    const auto ref = test.ref(test.first);
    const auto old_epoch = test.physics->body_state_2d(ref)->epoch;
    auto config = test.scene.entity(test.first).get<PhysicsBody2D>();
    config.friction = .8f;
    test.scene.entity(test.first).set<PhysicsBody2D>(config);
    test.tick();
    bool saw_end = false;
    for (std::size_t i = 0; i < test.physics->contact_count_2d(); ++i) {
        const auto event = *test.physics->contact_event_2d(i);
        if (!event.begin && !event.sensor)
            saw_end |=
                (event.first == ref && event.first_epoch == old_epoch && !event.first_available) ||
                (event.second == ref && event.second_epoch == old_epoch && !event.second_available);
    }
    require(saw_end, "Solid retired End lost its prior epoch");
}
void bounded_zero_dt_retirement() {
    auto config = PhysicsConfig{};
    config.gravity = {};
    physics_detail::Native2D native(config);
    const EntityRef ref{AssetId::generate(), EntityId::generate()};
    physics_detail::Body2DDesired body{ref, {PhysicsBody2D{2}, 0, {1, 1}}, {}};
    physics_detail::Desired2D desired{{1, body}};
    std::vector<std::uint64_t> snaps;
    native.validate(desired);
    native.synchronize(desired, desired, 0, {}, snaps);
    for (unsigned i = 0; i < 16384; ++i) {
        desired.at(1).config.body.mass = i % 2 ? 2 : 1;
        native.validate(desired);
        native.synchronize(desired, desired, 0, {}, snaps);
        snaps.clear();
    }
    const auto before = native.state(1, ref);
    desired.at(1).config.body.mass = 3;
    rejects([&] { native.validate(desired); });
    require(native.state(1, ref) == before, "Retirement overflow partially replaced a body");
    native.step(1.f / 60, 1);
    native.adopt_events(1);
    native.validate(desired);
    native.synchronize(desired, desired, 0, {}, snaps);
    require(native.state(1, ref)->epoch != before->epoch,
            "Adoption did not release the bounded retirement window");
}
} // namespace
int main() {
    try {
        rays_and_overlaps();
        swept_shapes();
        overlap_capacity();
        steady_sensor_profile();
        sensor_epochs_and_expiration();
        solid_retirement();
        bounded_zero_dt_retirement();
        std::cout
            << "2D filtered queries, copied current/retired events and bounded retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
