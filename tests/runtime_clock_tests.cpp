#include "runtime_entity_tests.hpp"
#include <array>
#include <cmath>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
namespace {
void check(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
void expect_near(double a, double b, double tolerance = 1e-8) {
    check(std::abs(a - b) <= tolerance, "numeric mismatch");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
RuntimeClock::Time time_ms(int n) { return RuntimeClock::Time{} + std::chrono::milliseconds(n); }
void presentation_time() {
    RuntimeClock clock({10, .25, 8});
    const auto tick = [](float) {};
    expect_near(clock.presentation_seconds(), 0);
    clock.resume(time_ms(0));
    clock.advance(time_ms(50), tick);
    expect_near(clock.presentation_seconds(), 0);
    clock.advance(time_ms(150), tick);
    expect_near(clock.presentation_seconds(), .05);
    clock.advance(time_ms(250), tick);
    expect_near(clock.presentation_seconds(), .15);
    clock.pause(time_ms(250));
    expect_near(clock.presentation_seconds(), .2);
    clock.advance(time_ms(4000), tick);
    expect_near(clock.presentation_seconds(), .2);
    clock.step(tick);
    expect_near(clock.presentation_seconds(), .3);
    clock.resume(time_ms(4000));
    // Resume collapses pose history: zero alpha must not rewind procedural geometry.
    clock.advance(time_ms(4050), tick);
    expect_near(clock.presentation_seconds(), .3);
    clock.advance(time_ms(4150), tick);
    expect_near(clock.presentation_seconds(), .35);
    clock.set_time_scale(0);
    clock.advance(time_ms(4250), tick);
    expect_near(clock.presentation_seconds(), .35);
    clock.set_time_scale(2);
    clock.advance(time_ms(4300), tick);
    expect_near(clock.presentation_seconds(), .45);
    clock.restore_tick(99, time_ms(4300));
    expect_near(clock.presentation_seconds(), 9.9);
    clock.resume(time_ms(4300));
    expect_near(clock.presentation_seconds(), 9.9);
    expect_near(clock.status().at("presentation_seconds"), clock.presentation_seconds());

    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "wind"}, {"name", "Wind"}, {"components", Json::object()}}})}});
    RuntimeSimulation simulation(engine.world(), scene);
    MeshRenderer renderer;
    renderer.mesh = {AssetId::generate()};
    renderer.wind.enabled = true;
    scene.entity("wind").set<MeshRenderer>(renderer);
    const auto authored = scene.document();
    const auto zero = simulation.presentation(1, 0);
    check(zero.at("simulation_seconds") == 0 && extract_render_scene(zero).simulation_seconds == 0,
          "Initial borrowed clock sample was missing or treated as false");
    const auto sampled = simulation.presentation(.5, .375);
    check(extract_render_scene(sampled).simulation_seconds == .375 && scene.document() == authored,
          "Borrowed geometry sample changed source intent or was lost in rendering extraction");
    rejects([&] { simulation.presentation(1, -1); });
    rejects([&] { simulation.presentation(1, std::numeric_limits<double>::quiet_NaN()); });
    for (const Json bad : {Json(true), Json(-1), Json("time")}) {
        auto malformed = sampled;
        malformed["simulation_seconds"] = bad;
        rejects([&] { extract_render_scene(malformed); });
    }
    renderer.wind = MeshWind{};
    scene.entity("wind").set<MeshRenderer>(renderer);
    check(simulation.presentation(1, 42) == simulation.presentation(1),
          "Default wind changed the legacy snapshot envelope or byte budget");
}
void signed_scale_interpolation() {
    PresentationPoses poses;
    std::map<std::uint64_t, TransformNode> nodes;
    nodes[1] = {{{}, {}, {1, 2, -3}}, 0, true};
    nodes[2] = {{{1, 2, 3}, {}, {-1, 1, 1}}, 1, true};
    poses.reset(nodes);
    nodes[1].local.scale = {-1, 0, 3};
    poses.capture(nodes);
    for (double alpha : {0.0, .25, .5, .75, 1.0}) {
        const auto result = poses.evaluate(alpha);
        const auto& parent = result.at(1);
        check(parent.resolved && result.at(2).resolved, "Signed interpolation became unresolved");
        expect_near(parent.affine.m[0], 1 - 2 * alpha);
        expect_near(parent.affine.m[5], 2 - 2 * alpha);
        expect_near(parent.affine.m[10], -3 + 6 * alpha);
        expect_near(result.at(2).affine.m[0], -(1 - 2 * alpha));
        expect_near(result.at(2).affine.m[3], 1 - 2 * alpha);
    }
    const auto midpoint = poses.evaluate(.5).at(1).affine;
    check(midpoint.m[0] == 0 && midpoint.m[10] == 0, "Scale interpolation clamped around zero");
}
void clocks() {
    RuntimeClock clock({100, .25, 8});
    unsigned calls = 0;
    auto tick = [&](float dt) {
        expect_near(dt, .01, 1e-9);
        ++calls;
    };
    clock.resume(time_ms(0));
    clock.advance(time_ms(9), tick);
    check(calls == 0, "early tick");
    clock.advance(time_ms(10), tick);
    check(clock.tick() == 1, "one tick");
    clock.advance(time_ms(35), tick);
    check(calls == 3, "catch up");
    expect_near(clock.alpha(), .5);
    clock.pause(time_ms(35));
    clock.advance(time_ms(5000), tick);
    check(calls == 3, "paused debt");
    expect_near(clock.alpha(), 1);
    clock.step(tick);
    clock.step(tick);
    check(calls == 5 && clock.paused(), "single step");
    clock.resume(time_ms(6000));
    clock.advance(time_ms(6005), tick);
    check(calls == 5, "resume debt");
    clock.advance(time_ms(6010), tick);
    check(calls == 6, "resume tick");
    rejects([&] { clock.step(tick); });
    clock.advance(time_ms(8015), tick);
    check(calls == 14, "unbounded catch up");
    check(clock.status()["dropped_ticks"] == 17, "dropped debt");
    expect_near(clock.status()["clamped_seconds"], 1.755);
    expect_near(clock.alpha(), 0);
    RuntimeClock fractional({100, .25, 8});
    fractional.resume(time_ms(0));
    fractional.advance(time_ms(95), tick);
    check(fractional.tick() == 8 && fractional.status()["dropped_ticks"] == 1, "whole debt");
    expect_near(fractional.alpha(), .5);
    RuntimeClock normal;
    normal.resume(time_ms(0));
    unsigned n = 0;
    normal.advance(RuntimeClock::Time{} + std::chrono::nanoseconds(16666666), [&](float) { ++n; });
    check(n == 0, "before default tick");
    normal.advance(RuntimeClock::Time{} + std::chrono::nanoseconds(16666667), [&](float dt) {
        expect_near(dt, 1. / 60, 1e-9);
        ++n;
    });
    check(n == 1, "default fixed tick");
    RuntimeClock failure;
    failure.pause(time_ms(0));
    rejects([&] { failure.step([](float) { throw std::runtime_error("tick failed"); }); });
    check(failure.tick() == 0, "failed tick counted");
    for (double hz : {0., -1., 241., std::numeric_limits<double>::infinity()})
        rejects([&] { RuntimeClock invalid({hz}); });
}
void poses(bool owned) {
    PresentationPoses history;
    const auto capture = [&](const PresentationPoses::Nodes& source) {
        const auto original = source;
        if (owned)
            history.capture_owned(PresentationPoses::Nodes(source));
        else
            history.capture(source);
        check(source == original, "Capture changed caller-owned pose data");
    };
    PresentationPoses::Nodes nodes{{1, {}}};
    history.reset(nodes);
    nodes[1].local.translation.x = 10;
    nodes[1].local.scale = {3, 3, 3};
    nodes[1].local.rotation = rotation_about_axis({0, 1, 0}, 90);
    capture(nodes);
    const auto half = history.evaluate(.5).at(1).affine;
    expect_near(half.m[3], 5);
    expect_near(half.m[0], std::sqrt(2.), 3e-7);
    expect_near(half.m[2], std::sqrt(2.), 3e-7);
    expect_near(history.evaluate(0).at(1).affine.m[3], 0);
    expect_near(history.evaluate(1).at(1).affine.m[3], 10);
    expect_near(history.evaluate(1 - 1e-9).at(1).affine.m[3], 10 - 1e-8);
    // A sign-flipped quaternion is the same rotation, not a long revolution.
    history.reset(nodes);
    auto q = nodes[1].local.rotation;
    nodes[1].local.rotation = {-q.x, -q.y, -q.z, -q.w};
    capture(nodes);
    expect_near(history.evaluate(.5).at(1).affine.m[2], 3, 3e-7);
    history.snap(1);
    expect_near(history.evaluate(0).at(1).affine.m[3], 10);
    nodes = {{1, {}}, {2, {{}, 1, true}}, {3, {{}, 0, true}}};
    history.reset(nodes);
    nodes[1].local.translation.x = 10;
    nodes[2].local.translation.x = 4;
    nodes[3].local.translation.x = 6;
    capture(nodes);
    auto values = history.evaluate(.5);
    expect_near(values.at(2).affine.m[3], 7); // parent 5 + child 2, each applied once.
    expect_near(values.at(3).affine.m[3], 3); // World bound.
    nodes[4] = {{{12, 0, 0}, {}, {}}};
    capture(nodes);
    expect_near(history.evaluate(0).at(4).affine.m[3], 12); // spawn.
    nodes[2].parent = 3;
    nodes[2].local.translation.x = 100;
    capture(nodes);
    expect_near(history.evaluate(0).at(2).affine.m[3], 106); // reparent reset.
    nodes[2].local.translation.x = 500;
    capture(nodes);
    history.snap(2);
    expect_near(history.evaluate(0).at(2).affine.m[3], 506); // teleport.
    nodes[2].parent_resolved = false;
    capture(nodes);
    check(!history.evaluate(.5).at(2).resolved, "missing explicit target");
    // Interpolate the local rotation before hierarchy composition, not child world matrices.
    nodes = {{1, {}}, {2, {{{2, 0, 0}, {}, {}}, 1, true}}};
    history.reset(nodes);
    nodes[1].local.rotation = rotation_about_axis({0, 1, 0}, 90);
    capture(nodes);
    expect_near(history.evaluate(.5).at(2).affine.m[3], std::sqrt(2.), 2e-7);
    expect_near(history.evaluate(.5).at(2).affine.m[11], -std::sqrt(2.), 2e-7);
    nodes.clear();
    for (std::uint64_t id = 1; id <= 512; ++id)
        nodes[id] = {{{1, 0, 0}, {}, {}}, id - 1, true};
    history.reset(nodes);
    for (auto& [id, node] : nodes)
        node.local.translation.x = 3;
    capture(nodes);
    expect_near(history.evaluate(.5).at(512).affine.m[3], 1024);
    rejects([&] { history.evaluate(-.1); });
    rejects([&] { history.evaluate(1.1); });
    // Sparse keys exercise head/middle/tail retirement and hinted insertion.
    nodes = {{1, {{{1, 0, 0}, {}, {}}}},
             {3, {{{3, 0, 0}, {}, {}}}},
             {5, {{{5, 0, 0}, {}, {}}}},
             {7, {{{7, 0, 0}, {}, {}}}}};
    history.reset(nodes);
    nodes = {{2, {{{20, 0, 0}, {}, {}}}},
             {3, {{{30, 0, 0}, {}, {}}}},
             {6, {{{60, 0, 0}, {}, {}}}},
             {9, {{{90, 0, 0}, {}, {}}}}};
    capture(nodes);
    values = history.evaluate(.5);
    check(values.size() == 4 && !values.contains(1) && !values.contains(5) && !values.contains(7),
          "Retired pose keys survived capture");
    expect_near(values.at(3).affine.m[3], 16.5);
    for (const auto id : {2u, 6u, 9u})
        expect_near(values.at(id).affine.m[3], id * 10.);
    const auto fresh_generation = (std::uint64_t{1} << 32) | 3;
    nodes.erase(3);
    nodes[fresh_generation] = {{{123, 0, 0}, {}, {}}};
    capture(nodes);
    values = history.evaluate(0);
    check(values.size() == 4 && !values.contains(3), "Recycled generation reused old history");
    expect_near(values.at(fresh_generation).affine.m[3], 123);
    nodes.clear();
    capture(nodes);
    check(history.evaluate(.5).empty(), "Empty capture retained old samples");
}
void simulation_rates() {
    for (const auto [rate, expected] : std::array<std::pair<double, unsigned>, 6>{
             {{0, 0}, {.25, 0}, {.5, 1}, {1, 2}, {2, 4}, {4, 8}}}) {
        RuntimeClock clock({100, .25, 8});
        check(clock.time_scale() == 1 && clock.status().at("time_scale") == 1,
              "Fresh clock did not start at normal rate");
        clock.set_time_scale(rate);
        clock.resume(time_ms(0));
        unsigned calls = 0;
        clock.advance(time_ms(20), [&](float dt) {
            check(dt == .01f, "Simulation rate changed the positive fixed physics delta");
            ++calls;
        });
        check(calls == expected && clock.tick() == expected,
              "Scaled fake-steady interval produced the wrong fixed tick count");
        expect_near(clock.alpha(), rate == .25 ? .5 : 0);
        check(clock.status().at("dropped_ticks") == 0, "Rate discarded admitted short interval");
    }
    RuntimeClock switched({100, .25, 8});
    unsigned calls = 0;
    auto tick = [&](float dt) {
        check(dt == .01f, "Switch changed fixed delta");
        ++calls;
    };
    switched.resume(time_ms(0));
    switched.advance(time_ms(5), tick);
    switched.set_time_scale(.5);
    switched.advance(time_ms(10), tick);
    expect_near(switched.alpha(), .75);
    switched.set_time_scale(0);
    switched.advance(time_ms(1010), tick);
    check(calls == 0 && !switched.paused(), "Zero rate advanced or implicitly paused the clock");
    expect_near(switched.alpha(), .75);
    expect_near(switched.status().at("clamped_seconds"), .75);
    switched.set_time_scale(2);
    switched.advance(time_ms(1015), tick);
    check(calls == 1, "Rate switch lost retained fractional progress");
    expect_near(switched.alpha(), .75);
    const auto unchanged = switched.status();
    for (const auto value : {-1., 4.001, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { switched.set_time_scale(value); });
        check(switched.status() == unchanged, "Invalid rate changed clock state or counters");
    }
    switched.pause(time_ms(1015));
    switched.set_time_scale(0);
    switched.step(tick);
    check(calls == 2 && switched.paused() && switched.time_scale() == 0,
          "Single Step at zero rate did not advance exactly one fixed tick");
    switched.resume(time_ms(5000));
    switched.advance(time_ms(5020), tick);
    check(calls == 2, "Resume at zero rate replayed paused wall time");
    RuntimeClock bounded({100, .25, 8});
    bounded.set_time_scale(4);
    bounded.resume(time_ms(0));
    bounded.advance(time_ms(500), tick);
    check(bounded.tick() == 8 && bounded.status().at("dropped_ticks") == 92,
          "Scaled virtual debt bypassed the existing catch-up bound");
    expect_near(bounded.status().at("clamped_seconds"), .25);
    expect_near(bounded.alpha(), 0);
}
void unscaled_time() {
    RuntimeClock clock({100, .25, 8});
    auto observation = [&] { return clock.status(); };
    auto verify = [&](double seconds, double delta, std::uint64_t sequence) {
        const auto status = observation();
        expect_near(status.at("unscaled_time"), seconds);
        expect_near(status.at("unscaled_delta"), delta);
        check(status.at("unscaled_sequence") == sequence, "Wrong real-time sample sequence");
    };
    verify(0, 0, 0);
    clock.sample_time(time_ms(1000)); // Origin is an observation, never epoch time.
    clock.resume(time_ms(1000));
    unsigned calls = 0;
    auto tick = [&](float dt) {
        check(dt == .01f, "Unscaled observation changed fixed delta");
        ++calls;
    };
    clock.sample_time(time_ms(1020));
    clock.advance(time_ms(1020), tick);
    verify(.02, .02, 1);
    check(calls == 2, "Observation advanced or suppressed simulation");
    const auto copy = observation();
    clock.sample_time(time_ms(1020));
    check(observation() == copy, "Repeated timestamp erased interval or counted twice");
    rejects([&] { clock.sample_time(time_ms(1019)); });
    rejects([&] { clock.advance(time_ms(1019), tick); });
    check(observation() == copy, "Backwards timestamp partially changed clock");
    clock.pause(time_ms(1020));
    clock.advance(time_ms(2020), tick);
    verify(1.02, 1, 2);
    check(calls == 2, "Paused real-time observation ticked gameplay");
    const auto paused = observation();
    clock.step(tick);
    verify(1.02, 1, 2);
    check(calls == 3 && observation().at("tick") == paused.at("tick").get<unsigned>() + 1,
          "Step did not retain separate real and fixed timelines");
    clock.set_time_scale(0);
    clock.resume(time_ms(2020));
    clock.advance(time_ms(3020), tick);
    verify(2.02, 1, 3);
    check(calls == 3, "Zero rate advanced gameplay");
    clock.set_time_scale(.25);
    clock.advance(time_ms(3060), tick);
    verify(2.06, .04, 4);
    check(calls == 4, "Slow motion changed real interval or fixed count");
    clock.set_time_scale(4);
    clock.advance(time_ms(5060), tick);
    verify(4.06, 2, 5); // Raw observation includes time rejected by the sim clamp.
    check(calls == 12 && observation().at("dropped_ticks") == 92,
          "Real time bypassed bounded catch-up/debt policy");
    clock.restore_tick(7, time_ms(5060));
    verify(4.06, 2, 5);
    clock.sample_time(time_ms(6060));
    verify(5.06, 1, 6);
    check(clock.tick() == 7 && clock.paused(), "Recovery reset real-time owner or ran ECS");
    RuntimeClock fresh;
    fresh.sample_time(time_ms(6060));
    check(fresh.status().at("unscaled_time") == 0 && fresh.status().at("unscaled_sequence") == 0,
          "Fresh clock retained another owner's observation");
}
void partial_pose_publish() {
    PresentationPoses history;
    PresentationPoses::Nodes nodes{{1, {}}, {2, {}}};
    history.reset(nodes);
    nodes.at(1).local.translation.x = 10;
    nodes.at(2).local.translation.x = 6;
    history.capture(nodes);
    nodes.at(1).local.translation.x = 100;
    history.snap(1, nodes.at(1));
    for (const auto alpha : {0., .5, 1.}) {
        const auto result = history.evaluate(alpha);
        expect_near(result.at(1).affine.m[3], 100);
        expect_near(result.at(2).affine.m[3], 6 * alpha);
    }
}
void live() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "root"},
                        {"name", "Root"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}},
                       {{"id", "child"},
                        {"name", "Child"},
                        {"parent", "root"},
                        {"components", {{"forge.position", {{"x", 2}, {"y", 0}, {"z", 0}}}}}}})}});
    auto root = scene.entity(scene.canonical_id("root")),
         child = scene.entity(scene.canonical_id("child"));
    root.set<Camera>({});
    child.set<Light>({});
    MeshRenderer mesh;
    mesh.mesh.id = AssetId::generate();
    child.set<MeshRenderer>(mesh);
    child.set<SpatialBinding>({SpatialMode::FollowStructure, {}});
    RuntimeSimulation simulation(engine.world(), scene);
    unsigned system_calls = 0;
    float system_dt = 0;
    auto extra = scene.world()
                     .system()
                     .kind(scene.world().lookup("forge.runtime.Gameplay"))
                     .run([&](flecs::iter& it) {
                         ++system_calls;
                         system_dt = it.delta_time();
                         scene.translate(it.delta_time(), 0, 0);
                     });
    extra.add<FixedSimulation>();
    rejects([&] { simulation.tick(0); });
    simulation.tick(1.f / 60);
    check(system_calls == 1, "Flecs system not scheduled");
    expect_near(system_dt, 1. / 60, 1e-9);
    expect_near(ecs_get_world_info(scene.world())->delta_time, 1. / 60, 1e-9);
    const auto current = scene.document();
    const auto revision = scene.revision();
    auto before = simulation.presentation(0), half = simulation.presentation(.5),
         after = simulation.presentation(1);
    expect_near(before["entities"][0]["world_affine"][3], 0);
    expect_near(half["entities"][0]["world_affine"][3], 1. / 120, 1e-9);
    expect_near(after["entities"][0]["world_affine"][3], 1. / 60, 1e-9);
    expect_near(half["entities"][1]["world_affine"][3], 2 + 1. / 120, 1e-9);
    const auto rendered = extract_render_scene(Json::parse(half.dump()));
    check(rendered.cameras.size() == 1 && rendered.lights.size() == 1 &&
              rendered.meshes.size() == 2 && rendered.diagnostics.empty(),
          "Runtime copied render components missing");
    const auto cameras = prepare_game_cameras(rendered, 800, 600);
    check(cameras.cameras.size() == 1 && cameras.diagnostics.empty(),
          "Runtime camera selection failed");
    expect_near(cameras.cameras[0].view.position[0], 1. / 120, 1e-9);
    expect_near(rendered.lights[0].light.position[0], 2 + 1. / 120, 1e-9);
    const auto child_id = EntityId::parse(scene.canonical_id("child"));
    const auto child_mesh = std::find_if(rendered.meshes.begin(), rendered.meshes.end(),
                                         [&](const auto& item) { return item.entity == child_id; });
    check(child_mesh != rendered.meshes.end() && child_mesh->renderer.mesh == mesh.mesh,
          "Runtime mesh selection lost its persistent entity identity");
    expect_near(child_mesh->world.m[3], 2 + 1. / 120, 1e-9);
    check(scene.document() == current && scene.revision() == revision,
          "presentation mutated simulation");
    child.set<SpatialBinding>({SpatialMode::Explicit, scene.reference(scene.canonical_id("root"))});
    simulation.reset_presentation();
    expect_near(simulation.presentation(.1)["entities"][1]["world_affine"][3], 2 + 1. / 60, 1e-9);
    child.set<SpatialBinding>({SpatialMode::World, {}});
    simulation.reset_presentation();
    expect_near(simulation.presentation(0)["entities"][1]["world_affine"][3], 2);
    // Explicit discontinuity/load resets samples to authoritative current, independent of alpha.
    root.set<LocalTranslation>({100, 0, 0});
    simulation.reset_presentation();
    expect_near(simulation.presentation(0)["entities"][0]["world_affine"][3], 100);
    unsigned intervals = 0, rates = 0, timeouts = 0;
    const auto phase = scene.world().lookup("forge.runtime.Gameplay");
    auto interval =
        scene.world().system().kind(phase).interval(.05f).run([&](flecs::iter&) { ++intervals; });
    auto rate = scene.world().system().kind(phase).rate(3).run([&](flecs::iter&) { ++rates; });
    auto timeout = scene.world().system().kind(phase).run([&](flecs::iter&) { ++timeouts; });
    ecs_set_timeout(scene.world(), timeout.id(), .03f);
    for (auto system : {interval, rate, timeout})
        system.add<FixedSimulation>();
    for (int i = 0; i < 6; ++i)
        simulation.tick(1.f / 60);
    check(intervals == 2 && rates == 2 && timeouts == 1,
          "Native timers/rate filters did not follow fixed pipeline");
    RuntimeClock paused_clock;
    const auto calls = intervals;
    paused_clock.advance(RuntimeClock::Time{} + std::chrono::seconds(1),
                         [&](float dt) { simulation.tick(dt); });
    check(intervals == calls, "Paused wall time progressed ECS timers");
    paused_clock.step([&](float dt) { simulation.tick(dt); });
    check(rates == 2 && timeouts == 1, "Step incorrectly advanced rate or restarted one-shot");
    ecs_stop_timer(scene.world(), interval.id());
    for (int i = 0; i < 3; ++i)
        simulation.tick(1.f / 60);
    check(intervals == calls, "Stopped native timer continued ticking");
    for (auto system : {interval, rate, timeout})
        system.destruct();
    extra.destruct();
}
} // namespace
int main(int argc, char** argv) {
    try {
        clocks();
        presentation_time();
        simulation_rates();
        unscaled_time();
        signed_scale_interpolation();
        poses(false);
        poses(true);
        partial_pose_publish();
        live();
        test_runtime_entities();
        std::cout << "Clock, poses, live pipeline/presentation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
