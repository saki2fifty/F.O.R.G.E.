#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>
void physics_overlap_tests() {
    {
        Fixture f;
        auto config = PhysicsConfig{};
        config.layers[1] = "Solid";
        config.layers[2] = "Zone";
        f.physics->configure(config);
        auto a = body("a", 0, 0), b = body("b", 0, 0), far = body("far", 0, 0);
        a["components"]["forge.position"]["x"] = -.5;
        b["components"]["forge.position"]["x"] = .5;
        far["components"]["forge.position"]["x"] = 5;
        a["components"]["forge.physics_body"]["layer"] = 1;
        b["components"]["forge.physics_body"]["layer"] = 2;
        b["components"]["forge.physics_body"]["sensor"] = true;
        f.load({{"version", 1}, {"entities", Json::array({a, b, far})}});
        const auto ar = *f.engine.world().reference(f.scene.entity("a").id());
        const auto br = *f.engine.world().reference(f.scene.entity("b").id());
        auto expected = std::vector<EntityRef>{ar, br};
        std::sort(expected.begin(), expected.end());
        PhysicsOverlap query;
        query.dimensions = {.25, 0, 0};
        const auto before = f.physics->checkpoint();
        const auto scene = f.scene.snapshot();
        check(f.physics->overlap_shape(query, {}) == expected &&
                  f.physics->overlap_shape(query, {}) == expected,
              "Instantaneous overlap lost static/sensor identities, order or repeatability");
        check(f.physics->overlap_shape(query, {UINT32_MAX, false}) == std::vector<EntityRef>{ar} &&
                  f.physics->overlap_shape(query, {1u << 1, true}) == std::vector<EntityRef>{ar} &&
                  f.physics->overlap_shape(query, {1u << 2, false}).empty() &&
                  f.physics->overlap_shape(query, {0, true}).empty(),
              "Volume query ignored named layer or sensor filtering");
        for (const auto kind : {PhysicsOverlap::Shape::Box, PhysicsOverlap::Shape::Capsule,
                                PhysicsOverlap::Shape::Cylinder}) {
            auto primitive = query;
            primitive.shape = kind;
            primitive.dimensions =
                kind == PhysicsOverlap::Shape::Box ? Double3{1, 1, 1} : Double3{.25, 1, 0};
            check(f.physics->overlap_shape(primitive, {}) == expected,
                  "Volume primitive dimension conventions disagree with known solids");
        }
        auto miss = query;
        miss.origin = {20, 0, 0};
        check(f.physics->overlap_shape(miss, {}).empty(), "Miss fabricated a volume target");
        bool rejected = false;
        std::thread foreign([&] {
            try {
                f.physics->overlap_shape(query, {});
            } catch (const std::exception&) {
                rejected = true;
            }
        });
        foreign.join();
        check(rejected && f.physics->checkpoint() == before && f.scene.snapshot() == scene,
              "Volume reads accepted foreign ownership or changed native/authored state");
        for (unsigned mode = 0; mode < 5; ++mode) {
            auto invalid = query;
            if (mode == 0)
                invalid.shape = PhysicsOverlap::Shape(4);
            else if (mode == 1)
                invalid.dimensions[0] = 0;
            else if (mode == 2)
                invalid.dimensions[1] = 1;
            else if (mode == 3)
                invalid.origin.x = std::numeric_limits<double>::quiet_NaN();
            else
                invalid.rotation = {0, 0, 0, 0};
            reject([&] { f.physics->overlap_shape(invalid, {}); });
        }
        Fixture recovered;
        recovered.physics->configure(config);
        recovered.scene.restore_snapshot(scene);
        recovered.physics->restore(before);
        check(recovered.physics->overlap_shape(query, {}) == expected,
              "Fresh native recovery changed sorted volume identities");
        auto empty = scene;
        empty["entities"] = Json::array();
        f.scene.restore_snapshot(empty);
        check(f.physics->overlap_shape(query, {}).empty(), "Volume exposed retired targets");
        f.scene.restore_snapshot(scene);
        check(f.physics->overlap_shape(query, {}).empty(),
              "Volume associated old native bodies with new same-UUID generations");
        f.physics->synchronize(0);
        check(f.physics->overlap_shape(query, {}) == expected,
              "Volume lost newly realized canonical generations");
        f.physics->stop();
        reject([&] { f.physics->overlap_shape(query, {}); });
    }
    {
        Fixture f;
        f.load({{"version", 1}, {"entities", Json::array({body("target", 1.25, 0)})}});
        PhysicsOverlap query;
        query.shape = PhysicsOverlap::Shape::Box;
        query.dimensions = {2, .2, .2};
        check(f.physics->overlap_shape(query, {}).empty(), "Unrotated thin box hit distant Y");
        query.rotation = {0, 0, float(std::sin(std::numbers::pi / 4)),
                          float(std::cos(std::numbers::pi / 4))};
        check(f.physics->overlap_shape(query, {}).size() == 1,
              "Rotated native volume ignored orientation");
    }
    {
        Fixture f;
        f.load(source());
        f.tick(360);
        const auto ref = *f.engine.world().reference(f.scene.entity("cube").id());
        check(!f.physics->body_state(ref)->active, "Sleeping volume precondition missing");
        const auto before = f.physics->checkpoint();
        PhysicsOverlap query;
        query.origin = {0, y(f), 0};
        query.dimensions = {.1, 0, 0};
        check(f.physics->overlap_shape(query, {}) == std::vector<EntityRef>{ref} &&
                  !f.physics->body_state(ref)->active && f.physics->checkpoint() == before,
              "Volume missed or woke the sleeping solid after cache End");
    }
    {
        Fixture f;
        const Json character = {{"id", "character"},
                                {"name", "character"},
                                {"components",
                                 {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                  {"forge.character_controller",
                                   {{"enabled", true},
                                    {"shape", 0},
                                    {"radius", .35},
                                    {"height", 1.1},
                                    {"crouch_height", .45},
                                    {"mass", 70},
                                    {"max_strength", 100},
                                    {"max_slope", 50},
                                    {"step_height", .4},
                                    {"step_forward", .15},
                                    {"floor_probe", .5},
                                    {"gravity_factor", 0},
                                    {"layer", 0},
                                    {"mask", UINT32_MAX}}}}}};
        f.load({{"version", 1}, {"entities", Json::array({character})}});
        const auto ref = *f.engine.world().reference(f.scene.entity("character").id());
        PhysicsOverlap query;
        query.origin = {0, .9, 0};
        query.dimensions = {.1, 0, 0};
        check(f.physics->overlap_shape(query, {}) == std::vector<EntityRef>{ref},
              "Volume lost the realized character inner-body identity");
    }
    {
        Fixture f;
        auto input = Json{{"version", 1}, {"entities", Json::array()}};
        for (unsigned i = 0; i <= physics_overlap_limit; ++i)
            input["entities"].push_back(body(("crowded" + std::to_string(i)).c_str(), 0, 0));
        f.load(input);
        PhysicsOverlap query;
        query.dimensions = {1, 0, 0};
        const auto before = f.physics->checkpoint();
        reject([&] { f.physics->overlap_shape(query, {}); });
        check(f.physics->checkpoint() == before,
              "Bounded overlap overflow mutated native state or returned partial success");
    }
}
