#include "asset_bytes.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_render_instances.hpp"
#include "particle_runtime.hpp"
#include "render_values.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/physics.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Action> void reject(Action&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Scene particle runtime operation was admitted");
}
Json emitter(const char* id, AssetId effect) {
    ParticleEmitter value;
    value.effect.id = effect;
    value.seed = 17;
    return {{"id", id},
            {"name", id},
            {"components",
             {{"forge.position", {{"x", 0}, {"y", 3}, {"z", 0}}},
              {"forge.particle_emitter", detail::render_value(value)}}}};
}
Json collider(const char* id, double y, bool sensor) {
    return {{"id", id},
            {"name", id},
            {"components",
             {{"forge.position", {{"x", 0}, {"y", y}, {"z", 0}}},
              {"forge.physics_body",
               {{"motion", 0},
                {"density", 1000},
                {"mass", 0},
                {"friction", .5},
                {"restitution", 0},
                {"gravity_factor", 0},
                {"sensor", sensor}}},
              {"forge.box_collider", {{"x", 20}, {"y", .01}, {"z", 20}}}}}};
}
Json scene(AssetId effect) {
    return {{"version", 1},
            {"entities", Json::array({emitter("emitter", effect), collider("floor", -.005, false),
                                      collider("sensor", 2, true)})}};
}
struct Fixture {
    EngineContext engine;
    Scene authored;
    RuntimeSimulation simulation;
    std::shared_ptr<ParticleRuntime> particles;
    Fixture(const std::filesystem::path& root, const Json& source, bool physics = true)
        : engine(WorldRole::Runtime, false,
                 physics ? std::vector<EngineModule>{physics_module(), particle_module(root)}
                         : std::vector<EngineModule>{particle_module(root)}),
          authored(engine.world()), simulation(engine.world(), authored),
          particles(particle_runtime(engine.world())) {
        authored.restore_snapshot(source);
        if (physics)
            std::static_pointer_cast<PhysicsRuntime>(engine.services().physics())->synchronize(0);
        simulation.reset_presentation();
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!particles->prepare()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Scene particle preparation timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
    flecs::entity actor() { return authored.entity("emitter"); }
    RenderParticleEmitter captured(double alpha = 1) {
        const auto result = extract_render_scene(simulation.presentation(alpha));
        check(result.diagnostics.empty() && result.particles.size() == 1,
              "Scene particle extraction failed or created extra emitters");
        return result.particles.front();
    }
};
void world_and_transport(const std::filesystem::path& root, AssetId effect) {
    const auto source = scene(effect);
    Fixture live(root, source);
    live.ready();
    check(live.captured().world_samples && live.captured().world_samples->empty(),
          "A newly admitted Scene effect used analytic preview values before its first update");
    live.simulation.tick(.1f);
    const auto current = live.captured();
    check(current.world_samples && current.world_samples->size() == 4 &&
              std::abs(current.world_samples->front().sample.position[1] - 4.575) < 1e-4 &&
              current.world_samples->front().sample.velocity[1] == 60,
          "Runtime particles tunneled through a thin Scene floor or hit an excluded sensor");
    const auto before = live.captured(.2), after = live.captured(.3);
    check(before.world_samples->front().sample.velocity[1] < 0 &&
              after.world_samples->front().sample.velocity[1] > 0 &&
              before.world_samples->front().sample.position[1] > .05 &&
              after.world_samples->front().sample.position[1] > .05,
          "Runtime presentation did not retain both sides of a swept impact");
    const auto immutable = live.simulation.presentation(.3);
    for (unsigned i = 0; i < 3; ++i)
        check(live.simulation.presentation(.3) == immutable,
              "Repeated paused presentation changed the World collision observation");
    auto actor = live.actor();
    actor.set<LocalTranslation>({5, 3, 0});
    live.simulation.tick(.05f);
    const auto moved = live.captured();
    check(moved.world.m[3] == 5 && moved.world_samples->front().sample.position[0] == 0,
          "Moving the emitter dragged an already born world-space particle");
    const auto ref = *live.engine.world().reference(actor.id());
    live.particles->tune_appearance(ref, "size_multiplier", 2);
    live.particles->tune_appearance(ref, "tint_r", .25);
    const auto tuned = live.captured();
    const auto raw = tuned.world_samples->front().sample;
    const auto sample = live.particles->sample(actor.id(), 1).front();
    check(raw.size == .1 && sample.size == .2 && sample.color[0] == raw.color[0] * .25f,
          "World capture applied live appearance before the shared modulation boundary");
    // The renderer consumes the same immutable effect used by this native World.
    auto source_bytes = ParticleEffectSource::parse(
        asset_detail::read_bytes(root / "Assets/Scene.particles.json", particle_effect_byte_limit));
    const auto data = source_bytes.compiled();
    const auto draws = particle_world_render_instances(
        data, *tuned.world_samples, tuned.time, tuned.emitter.size_multiplier,
        {tuned.emitter.tint_r, tuned.emitter.tint_g, tuned.emitter.tint_b});
    const auto bounds = particle_world_bounds({{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}}, draws, false);
    check(draws.size() == 4 && draws[0].world.m[3] == 0 && draws[0].world.m[0] == .2 &&
              draws[0].color == sample.color && bounds && bounds->maximum[0] < 1,
          "World draw/culling reapplied the moved emitter or duplicated live modulation");
    auto corrupted = live.simulation.presentation(1);
    for (auto& row : corrupted["entities"])
        if (row.contains("particle_playback"))
            row["particle_playback"]["world_samples"][0]["position"][0] = "bad";
    check(!extract_render_scene(corrupted).diagnostics.empty(),
          "Presentation extraction admitted malformed World motion");
    auto wrong = *tuned.world_samples;
    wrong[0].sample.age += .01;
    reject([&] { (void)particle_world_render_instances(data, wrong, tuned.time); });
    const auto retained = live.particles->presentation(actor.id(), 1);
    reject([&] { live.particles->tick(1e9f); });
    check(live.particles->presentation(actor.id(), 1) == retained,
          "Rejected time overflow changed the admitted Scene particle frame");
    auto config = actor.get<ParticleEmitter>();
    config.playback_speed = 0;
    actor.set<ParticleEmitter>(config);
    live.simulation.tick(.05f);
    const auto frozen = live.particles->presentation(actor.id(), 1);
    live.simulation.tick(.05f);
    check(live.particles->presentation(actor.id(), 1) == frozen,
          "Frozen playback changed World particles while Scene physics advanced");
    config.seed = 18;
    config.playback_speed = 1;
    actor.set<ParticleEmitter>(config);
    live.ready();
    check(live.particles->presentation(actor.id(), 1).at("time") == 0 &&
              live.particles->sample(actor.id(), 1).empty(),
          "Seed replacement retained an old World contact history");
    live.simulation.tick(.01f);
    check(live.particles->sample(actor.id(), 1).front().position[0] == 5,
          "Restarted births did not use the newly adopted emitter pose");
    config.enabled = false;
    actor.set<ParticleEmitter>(config);
    live.particles->synchronize();
    check(live.particles->presentation(actor.id(), 1).is_null(),
          "Disabling a Scene emitter retained a live particle observation");
    config.enabled = true;
    actor.set<ParticleEmitter>(config);
    live.ready();
    live.simulation.tick(.01f);
    live.simulation.reset_presentation();
    check(live.particles->sample(actor.id(), 1).empty(),
          "Visual reset retained Scene response histories");
    Fixture unavailable(root, source, false);
    reject([&] { unavailable.ready(); });
}
void projection_ownership(const std::filesystem::path& root, AssetId effect) {
    auto opaque = scene(effect);
    const Json foreign{{"time", 9999}, {"world_samples", "foreign opaque input"}};
    opaque["entities"][0]["particle_playback"] = foreign;
    Fixture live(root, opaque);
    bool retained = false;
    const auto authored = live.authored.document();
    for (const auto& row : authored.at("entities"))
        if (row.contains("particle_playback"))
            retained |= row.at("particle_playback") == foreign;
    check(retained, "Authored round trip discarded an unknown source field");
    const auto projected = live.authored.effective_document();
    for (const auto& row : projected.at("entities"))
        check(!row.contains("particle_playback"),
              "Opaque source data became authoritative particle playback in a stopped projection");
    live.ready();
    check(live.captured().world_samples->empty(),
          "A live World inherited foreign source particle playback before its first update");
}
void query_budget(const std::filesystem::path& root, AssetId heavy, AssetId analytic) {
    auto input = scene(heavy);
    input["entities"][0]["components"]["forge.position"]["y"] = 100;
    input["entities"].push_back(emitter("analytic", analytic));
    Fixture live(root, input);
    live.ready();
    const auto heavy_id = live.actor().id(), cpu_id = live.authored.entity("analytic").id();
    const auto previous = live.particles->presentation(heavy_id, 1);
    const auto cpu = live.particles->presentation(cpu_id, 1);
    reject([&] { live.particles->tick(.1f); });
    check(live.particles->presentation(heavy_id, 1) == previous &&
              live.particles->presentation(cpu_id, 1) == cpu,
          "World query overflow partially advanced Scene/analytic clocks or contact state");
    auto config = live.actor().get<ParticleEmitter>();
    config.playback_speed = .01;
    live.actor().set<ParticleEmitter>(config);
    live.particles->tick(.1f);
    check(live.particles->sample(heavy_id, 1).size() == 1024 &&
              live.particles->presentation(cpu_id, 1).at("time").get<double>() > .09,
          "A rejected World candidate could not resume under the admitted work budget");
    auto over = scene(heavy);
    over["entities"].push_back(emitter("second", heavy));
    Fixture reserved(root, over);
    reject([&] { reserved.ready(); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Scene particle runtime scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService imports(lease, particle_effect_import_registry(),
                                   {"linux", "none", "cpu"});
        auto publish = [&](const char* name, bool heavy, bool analytic) {
            auto document = ParticleEffectDocument::create(lease, name);
            document->edit(document->revision(), "Scene particle fixture", [&](auto& value) {
                ParticleEffectSource source{value};
                if (!analytic) {
                    source.document["emission"] = "burst";
                    source.document["capacity"] = heavy ? 1024 : 4;
                    source.document["burst_count"] = heavy ? 1024 : 4;
                    source.document["direction"] = {0, -1, 0};
                    source.document["cone_angle"] = 0;
                    source.document["speed_min"] = source.document["speed_max"] = heavy ? 0 : 120;
                    source.document["acceleration"] = {0, heavy ? -10000 : 0, 0};
                    source.document["size_start"] = source.document["size_end"] = .1;
                    source.set_scene_collision(true, ParticleSceneCollision{.05, .5, 0, 4, 1});
                }
                value = std::move(source.document);
            });
            document->save();
            imports.submit(
                imports.prepare(document->locator(), {}, document->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_particle_effect_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            check(imports.wait_idle(10s), "Scene particle publication timed out");
            const auto result = imports.poll();
            check(result.size() == 1 && result.front().published,
                  "Scene particle publication failed");
            return document->source().asset();
        };
        const auto scene_effect = publish("Assets/Scene.particles.json", false, false);
        const auto heavy = publish("Assets/Heavy.particles.json", true, false);
        const auto analytic = publish("Assets/Analytic.particles.json", false, true);
        world_and_transport(root, scene_effect);
        projection_ownership(root, scene_effect);
        query_budget(root, heavy, analytic);
        std::cout << "Published Scene particle runtime/transport/lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
