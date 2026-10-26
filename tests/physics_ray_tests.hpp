#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

void physics_ray_tests() {
    Fixture f;
    auto config = PhysicsConfig{};
    config.gravity = {};
    config.layers[1] = "Sensors";
    f.physics->configure(config);
    auto sensor = body("sensor", 3, 0);
    sensor["components"]["forge.physics_body"]["sensor"] = true;
    sensor["components"]["forge.physics_body"]["layer"] = 1;
    auto near = body("near", 2, 0);
    near["components"]["forge.physics_body"]["mask"] = 0;
    f.load({{"version", 1},
            {"entities", Json::array({body("tie-b", 0, 0), body("tie-a", 0, 0), near, sensor})}});
    const auto before = f.physics->checkpoint(), scene = f.scene.snapshot();
    const auto hits = f.physics->raycast_all({0, 5, 0}, {0, -10, 0});
    check(hits.size() == 4 && hits[0].entity == f.scene.reference("sensor") &&
              hits[1].entity == f.scene.reference("near") && hits[2].entity < hits[3].entity,
          "Multi-hit ray lost distance/identity order or applied pair masks");
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const double expected = i == 0 ? .15 : i == 1 ? .25 : .45;
        check(std::abs(hits[i].fraction - expected) < 1e-6 && hits[i].normal[1] > .99 &&
                  std::abs(hits[i].position[1] - (5 - 10 * expected)) < 1e-6,
              "Multi-hit ray copied incorrect surface geometry");
    }
    check(f.physics->raycast_all({0, 5, 0}, {0, -10, 0}, {UINT32_MAX, false}).size() == 3 &&
              f.physics->raycast_all({0, 5, 0}, {0, -10, 0}, {2, true}).size() == 1 &&
              f.physics->raycast_all({0, 5, 0}, {0, -10, 0}, {0, true}).empty() &&
              f.physics->raycast_all({4, 5, 0}, {0, -10, 0}).empty(),
          "Multi-hit ray lost layer/sensor/miss filtering");
    const auto inside = f.physics->raycast_all({0, 2, 0}, {0, -4, 0});
    check(!inside.empty() && inside[0].fraction == 0 &&
              inside[0].entity == f.scene.reference("near"),
          "Multi-hit ray changed native convex-solid inside behavior");
    for (unsigned mode = 0; mode < 3; ++mode) {
        Double3 origin{0, 5, 0}, delta{0, -10, 0};
        (mode == 1 ? delta : origin)[0] =
            mode == 2 ? 1e10 : std::numeric_limits<double>::quiet_NaN();
        reject([&] { f.physics->raycast_all(origin, delta); });
    }
    bool denied = false;
    std::thread foreign([&] {
        try {
            f.physics->raycast_all({0, 5, 0}, {0, -10, 0});
        } catch (const std::exception&) {
            denied = true;
        }
    });
    foreign.join();
    check(denied && f.physics->checkpoint() == before && f.scene.snapshot() == scene,
          "Ray reads changed native/source state or accepted a foreign owner");
    Fixture recovered;
    recovered.physics->configure(config);
    recovered.scene.restore_snapshot(scene);
    recovered.physics->restore(before);
    const auto restored = recovered.physics->raycast_all({0, 5, 0}, {0, -10, 0});
    check(restored.size() == hits.size(), "Recovered ray lost targets");
    for (std::size_t i = 0; i < hits.size(); ++i)
        check(restored[i].entity == hits[i].entity && restored[i].fraction == hits[i].fraction &&
                  restored[i].position == hits[i].position && restored[i].normal == hits[i].normal,
              "Fresh native recovery changed copied ray results");
    auto empty = scene;
    empty["entities"] = Json::array();
    f.scene.restore_snapshot(empty);
    check(f.physics->raycast_all({0, 5, 0}, {0, -10, 0}).empty(),
          "Ray exposed removed native targets");
    f.scene.restore_snapshot(scene);
    check(f.physics->raycast_all({0, 5, 0}, {0, -10, 0}).empty(),
          "Ray retargeted retired native bodies to same-UUID replacement generations");
    f.physics->synchronize(0);
    check(f.physics->raycast_all({0, 5, 0}, {0, -10, 0}).size() == 4,
          "Ray omitted newly synchronized targets");
    f.physics->stop();
    reject([&] { f.physics->raycast_all({0, 5, 0}, {0, -10, 0}); });

    Fixture crowded;
    Json rows = Json::array();
    for (std::size_t i = 0; i <= physics_ray_hit_limit; ++i)
        rows.push_back(body(("ray" + std::to_string(i)).c_str(), 0, 0));
    crowded.load({{"version", 1}, {"entities", rows}});
    const auto full = crowded.physics->checkpoint();
    reject([&] { crowded.physics->raycast_all({0, 5, 0}, {0, -10, 0}); });
    check(crowded.physics->checkpoint() == full, "Overflow ray mutated native state");
    rows.erase(rows.size() - 1);
    crowded.load({{"version", 1}, {"entities", rows}});
    check(crowded.physics->raycast_all({0, 5, 0}, {0, -10, 0}).size() == physics_ray_hit_limit,
          "Ray rejected its exact object limit or retained an earlier failure");
}
