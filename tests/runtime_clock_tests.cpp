#include <cmath>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
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
void poses() {
    PresentationPoses history;
    PresentationPoses::Nodes nodes{{1, {}}};
    history.reset(nodes);
    nodes[1].local.translation.x = 10;
    nodes[1].local.scale = {3, 3, 3};
    nodes[1].local.rotation = rotation_about_axis({0, 1, 0}, 90);
    history.capture(nodes);
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
    history.capture(nodes);
    expect_near(history.evaluate(.5).at(1).affine.m[2], 3, 3e-7);
    history.snap(1);
    expect_near(history.evaluate(0).at(1).affine.m[3], 10);
    nodes = {{1, {}}, {2, {{}, 1, true}}, {3, {{}, 0, true}}};
    history.reset(nodes);
    nodes[1].local.translation.x = 10;
    nodes[2].local.translation.x = 4;
    nodes[3].local.translation.x = 6;
    history.capture(nodes);
    auto values = history.evaluate(.5);
    expect_near(values.at(2).affine.m[3], 7); // parent 5 + child 2, each applied once.
    expect_near(values.at(3).affine.m[3], 3); // World bound.
    nodes[4] = {{{12, 0, 0}, {}, {}}};
    history.capture(nodes);
    expect_near(history.evaluate(0).at(4).affine.m[3], 12); // spawn.
    nodes[2].parent = 3;
    nodes[2].local.translation.x = 100;
    history.capture(nodes);
    expect_near(history.evaluate(0).at(2).affine.m[3], 106); // reparent reset.
    nodes[2].local.translation.x = 500;
    history.capture(nodes);
    history.snap(2);
    expect_near(history.evaluate(0).at(2).affine.m[3], 506); // teleport.
    nodes[2].parent_resolved = false;
    history.capture(nodes);
    check(!history.evaluate(.5).at(2).resolved, "missing explicit target");
    // Interpolate the local rotation before hierarchy composition, not child world matrices.
    nodes = {{1, {}}, {2, {{{2, 0, 0}, {}, {}}, 1, true}}};
    history.reset(nodes);
    nodes[1].local.rotation = rotation_about_axis({0, 1, 0}, 90);
    history.capture(nodes);
    expect_near(history.evaluate(.5).at(2).affine.m[3], std::sqrt(2.), 2e-7);
    expect_near(history.evaluate(.5).at(2).affine.m[11], -std::sqrt(2.), 2e-7);
    nodes.clear();
    for (std::uint64_t id = 1; id <= 512; ++id)
        nodes[id] = {{{1, 0, 0}, {}, {}}, id - 1, true};
    history.reset(nodes);
    for (auto& [id, node] : nodes)
        node.local.translation.x = 3;
    history.capture(nodes);
    expect_near(history.evaluate(.5).at(512).affine.m[3], 1024);
    rejects([&] { history.evaluate(-.1); });
    rejects([&] { history.evaluate(1.1); });
}
void live(const char* module_path) {
    Module module;
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
    module.load(std::filesystem::absolute(module_path));
    RuntimeSimulation simulation(engine.world(), scene, module);
    unsigned system_calls = 0;
    float system_dt = 0;
    auto extra = scene.world()
                     .system()
                     .kind(scene.world().lookup("forge.runtime.Gameplay"))
                     .run([&](flecs::iter& it) {
                         ++system_calls;
                         system_dt = it.delta_time();
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
              rendered.meshes.size() == 1 && rendered.diagnostics.empty(),
          "Runtime copied render components missing");
    const auto cameras = prepare_game_cameras(rendered, 800, 600);
    check(cameras.cameras.size() == 1 && cameras.diagnostics.empty(),
          "Runtime camera selection failed");
    expect_near(cameras.cameras[0].view.position[0], 1. / 120, 1e-9);
    expect_near(rendered.lights[0].light.position[0], 2 + 1. / 120, 1e-9);
    expect_near(rendered.meshes[0].world.m[3], 2 + 1. / 120, 1e-9);
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
        check(argc == 2, "module path required");
        clocks();
        signed_scale_interpolation();
        poses();
        live(argv[1]);
        std::cout << "Clock, poses, live pipeline/presentation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
