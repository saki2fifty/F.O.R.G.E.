#include "asset_bytes.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_render_instances.hpp"
#include "particle_ribbon.hpp"
#include "particle_runtime.hpp"
#include "render_values.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk.hpp>
#include <forge/physics.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
static_assert(particle_scene_query_limit == FORGE_SDK_PARTICLE_IMPACT_LIMIT);
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
Json collider(const char* id, double y, bool sensor, bool planar_2d = false) {
    Json result{{"id", id},
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
    if (planar_2d) {
        auto& components = result["components"];
        components.erase("forge.physics_body");
        components.erase("forge.box_collider");
        components["forge.physics_body_2d"] = {{"motion", 0}, {"sensor", sensor}};
        components["forge.box_collider_2d"] = {{"x", 20}, {"y", .01}};
    }
    return result;
}
Json scene(AssetId effect, bool planar_2d = false) {
    return {{"version", 1},
            {"entities",
             Json::array({emitter("emitter", effect), collider("floor", -.005, false, planar_2d),
                          collider("sensor", 2, true, planar_2d)})}};
}
struct Fixture {
    EngineContext engine;
    Scene authored;
    RuntimeSimulation simulation;
    std::shared_ptr<ParticleRuntime> particles;
    static std::vector<EngineModule> modules(const std::filesystem::path& root, bool physics,
                                             const std::filesystem::path& sdk, const char* module) {
        std::vector<EngineModule> result{particle_module(root)};
        if (physics)
            result.push_back(physics_module());
        if (!sdk.empty())
            result.push_back(load_native_sdk(sdk, module, "1"));
        return result;
    }
    Fixture(const std::filesystem::path& root, const Json& source, bool physics = true,
            const std::filesystem::path& sdk = {},
            const char* module = "project.particle_impacts_probe")
        : engine(WorldRole::Runtime, false, modules(root, physics, sdk, module)),
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
void world_and_transport(const std::filesystem::path& root, AssetId effect,
                         bool planar_2d = false) {
    const auto source = scene(effect, planar_2d);
    Fixture live(root, source);
    live.ready();
    check(live.captured().world_samples && live.captured().world_samples->empty(),
          "A newly admitted Scene effect used analytic preview values before its first update");
    live.simulation.tick(.1f);
    const auto current = live.captured();
    check(current.world_samples && current.world_samples->size() == 4 &&
              std::abs(current.world_samples->front().sample.position[1] -
                       (planar_2d ? 4.58 : 4.575)) < 1e-4 &&
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
    auto source_bytes = ParticleEffectSource::parse(asset_detail::read_bytes(
        root / (planar_2d ? "Assets/XY.particles.json" : "Assets/Scene.particles.json"),
        particle_effect_byte_limit));
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
void query_budget(const std::filesystem::path& root, AssetId heavy, AssetId analytic,
                  bool planar_2d = false) {
    auto input = scene(heavy, planar_2d);
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
    auto over = scene(heavy, planar_2d);
    over["entities"].push_back(emitter("second", heavy));
    Fixture reserved(root, over);
    reject([&] { reserved.ready(); });
}
void mixed_worlds(const std::filesystem::path& root, AssetId spatial, AssetId planar) {
    auto input = scene(spatial);
    auto xy_actor = emitter("xy", planar);
    xy_actor["components"]["forge.position"]["z"] = 50;
    input["entities"].push_back(xy_actor);
    input["entities"].push_back(collider("xy-floor", .995, false, true));
    input["entities"].push_back(collider("xy-sensor", 2, true, true));
    Fixture live(root, input);
    live.ready();
    live.simulation.tick(.1f);
    const auto a = live.particles->sample(live.actor().id(), 1);
    const auto b = live.particles->sample(live.authored.entity("xy").id(), 1);
    check(a.size() == 4 && b.size() == 4 && std::abs(a[0].position[1] - 4.575) < 1e-4 &&
              std::abs(b[0].position[1] - 6.08) < 1e-4 && b[0].position[2] == 50,
          "Mixed Scene particle query spaces crossed worlds, included sensors or used depth");
    Fixture cold(root, scene(planar));
    cold.ready();
    cold.simulation.tick(.1f);
    const auto free = cold.particles->sample(cold.actor().id(), 1);
    check(free.size() == 4 && std::abs(free[0].position[1] + 9) < 1e-4,
          "An unrealized XY World borrowed 3D geometry or blocked analytic free flight");
}
void historical_trails(const std::filesystem::path& root, AssetId effect, bool planar_2d) {
    Fixture live(root, scene(effect, planar_2d));
    live.ready();
    check(live.captured().world_paths && live.captured().world_paths->paths.points.empty(),
          "World trails were reconstructed before their first fixed update");
    live.simulation.tick(.1f);
    const auto bounced = live.captured();
    check(bounced.world_samples && bounced.world_paths && bounced.world_samples->size() == 4 &&
              bounced.world_paths->paths.paths.size() == 4 &&
              particle_world_capture_birth_count(*bounced.world_samples, &*bounced.world_paths) ==
                  4,
          "World trail extraction lost native heads or double-counted their birth reservation");
    auto actor = live.actor();
    actor.set<LocalTranslation>({5, 3, 0});
    actor.set<LocalScale>({3, 3, 3});
    live.simulation.tick(.05f);
    const auto ref = *live.engine.world().reference(actor.id());
    live.particles->tune_appearance(ref, "size_multiplier", 2);
    live.particles->tune_appearance(ref, "tint_r", .25);
    const auto tuned = live.captured();
    const auto data =
        ParticleEffectSource::parse(
            asset_detail::read_bytes(root / (planar_2d ? "Assets/WorldXY.particles.json"
                                                       : "Assets/World.particles.json"),
                                     particle_effect_byte_limit))
            .compiled();
    validate_particle_world_paths(data, tuned.time, *tuned.world_paths);
    check(tuned.world.m[3] == 5 && tuned.world_paths->width_scales == std::vector<double>(4, 1),
          "Moving or scaling an emitter changed earlier World trail birth widths");
    const auto bounds =
        particle_world_paths_bounds(data, *tuned.world_paths, tuned.time, *tuned.world_samples, 2);
    check(bounds && bounds->maximum[0] < 1,
          "World trail selection bounds reapplied a moved emitter transform");
    AffineTransform camera_frame;
    camera_frame.m[11] = -20;
    const auto view = camera_view(Camera{}, camera_frame, 640, 360);
    const auto ribbons = particle_world_ribbon_segments(*tuned.world_paths, view, 2, {.25f, 1, 1});
    check(!ribbons.empty() && ribbons.front().vertices.first[0] == 0 &&
              ribbons.front().vertices.color[0][0] ==
                  tuned.world_paths->paths.points.front().color[0] * .25f &&
              tuned.world_paths->paths.points.front().size == .1,
          "World ribbon rendering lost frozen geometry or duplicated live appearance");
    auto corrupt = live.simulation.presentation(1);
    for (auto& row : corrupt["entities"])
        if (row.contains("particle_playback"))
            row["particle_playback"]["world_paths"]["points"][0][1] = "bad";
    check(!extract_render_scene(corrupt).diagnostics.empty(),
          "Render extraction admitted malformed World path positions");
    live.simulation.tick(.1f);
    const auto tail = live.captured();
    check(tail.world_samples->empty() && tail.world_paths->paths.paths.size() == 4,
          "A lifetime-expired head erased its visible World trail");
    const auto tail_bounds =
        particle_world_paths_bounds(data, *tail.world_paths, tail.time, *tail.world_samples, 2);
    check(tail_bounds.has_value(), "Tail-only World geometry lost its selection bounds");
    live.simulation.tick(.6f);
    check(live.captured().world_paths->paths.points.empty(),
          "World trails survived their lifetime plus duration");
    auto config = actor.get<ParticleEmitter>();
    config.seed += 1;
    actor.set<ParticleEmitter>(config);
    live.ready();
    check(live.captured().world_paths->paths.points.empty(),
          "A new seed retained historical World trails");
    live.simulation.tick(.01f);
    check(live.captured().world_paths->width_scales == std::vector<double>(4, 3),
          "Restarted World trails did not adopt the new birth scale");
    config.enabled = false;
    actor.set<ParticleEmitter>(config);
    live.particles->synchronize();
    check(live.particles->presentation(actor.id(), 1).is_null(),
          "Disabling an emitter retained historical World trail observations");
}
void historical_reservation(const std::filesystem::path& root, AssetId effect) {
    Fixture admitted(root, scene(effect));
    admitted.ready();
    auto over = scene(effect);
    over["entities"].push_back(emitter("second", effect));
    Fixture rejected(root, over);
    // 1024 total heads fit the native World budget, but 16384 reserved
    // segments must fail the existing 8192-instance reservation atomically.
    reject([&] { rejected.ready(); });
    auto second = rejected.authored.entity("second");
    auto config = second.get<ParticleEmitter>();
    config.enabled = false;
    second.set<ParticleEmitter>(config);
    rejected.ready();
    check(rejected.particles->presentation(rejected.actor().id(), 1).at("time") == 0,
          "World trail reservation failure partially advanced the accepted emitter clock");
}
void impact_publication(const std::filesystem::path& root, AssetId effect, bool planar) {
    Fixture live(root, scene(effect, planar));
    live.ready();
    const auto view = particle_runtime_access(live.engine.world());
    check(view && view->impact_frame() == ParticleImpactFrame{},
          "Scene impact bridge published before its first completed interval");
    live.simulation.tick(.1f);
    const auto frame = view->impact_frame();
    check(frame.tick == 1 && frame.count == 4 && !view->impact_at(4),
          "Scene impacts lost completed-tick provenance, stable slots or native birth count");
    const auto emitter = *live.engine.world().reference(live.actor().id());
    const auto collider = *live.engine.world().reference(live.authored.entity("floor").id());
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto impact = view->impact_at(i);
        check(impact && impact->emitter == emitter && impact->effect == effect &&
                  impact->seed == 17 && impact->source_generation > 0 && impact->birth == i &&
                  impact->planar_2d == planar && impact->contact.collider == collider &&
                  impact->age > 0 && impact->age < .1 &&
                  std::abs(impact->contact.position[1] - .05) < 1e-4 &&
                  impact->contact.normal == Double3{0, 1, 0} &&
                  impact->contact.incoming_velocity[1] == -120 &&
                  impact->contact.outgoing_velocity[1] == 60 &&
                  impact->contact.kind == ParticleSceneImpactKind::Bounce,
              "Published native impact lost binding, time, center, response or excluded sensors");
    }
    const auto first = view->impact_at(0);
    reject([&] { live.particles->tick(1e9f, 2); });
    check(view->impact_frame() == frame && view->impact_at(0) == first,
          "Rejected particle time partially adopted an impact broadcast");
    auto config = live.actor().get<ParticleEmitter>();
    const auto original = config;
    config.seed += 1;
    live.actor().set<ParticleEmitter>(config);
    check(!view->impact_at(0) && view->impact_frame() == frame,
          "An impact remained bound to a replaced seed or changed stable slot count");
    live.actor().set<ParticleEmitter>(original);
    check(view->impact_at(0) == first, "An unadopted field edit mutated copied impact provenance");
    live.authored.entity("floor").destruct();
    check(!view->impact_at(0) && view->impact_frame() == frame,
          "A retired collider retained an available impact or shifted stable slots");
    config = original;
    config.playback_speed = 0;
    live.actor().set<ParticleEmitter>(config);
    live.simulation.tick(.01f);
    check(view->impact_frame() == ParticleImpactFrame{2, 0} && !view->impact_at(0),
          "A frozen next interval replayed an earlier impact");
    live.simulation.reset_presentation();
    check(view->impact_frame() == ParticleImpactFrame{},
          "Visual reset retained a published impact interval");
}
void impact_sdk(const std::filesystem::path& root, AssetId effect, bool planar,
                const std::filesystem::path& sdk, bool denied, bool retire) {
    Fixture live(root, scene(effect, planar), true, sdk,
                 denied ? "project.particle_impacts_denied" : "project.particle_impacts_probe");
    live.ready();
    auto observations = [&] {
        std::vector<Json> result;
        for (const auto& entry : live.engine.services().diagnostics()) {
            const auto text = entry.at("text").get<std::string>();
            if (text.starts_with("Particle impacts: "))
                result.push_back({{"tick", entry.at("context").at("tick")},
                                  {"value", Json::parse(text.substr(18))}});
        }
        return result;
    };
    live.simulation.tick(.1f);
    auto rows = observations();
    check(rows.size() == 1 && rows[0].at("tick").get<unsigned>() == 1 &&
              rows[0].at("value").at("count").get<int>() == (denied ? -1 : 0),
          "SDK observed impacts before completion or bypassed Physics permission");
    if (retire)
        live.authored.entity("floor").destruct();
    live.simulation.tick(.01f);
    rows = observations();
    check(rows.size() == 2 && rows[1].at("tick").get<unsigned>() == 2,
          "SDK did not run in the next fixed interval");
    const auto& value = rows[1].at("value");
    check(value.at("count").get<int>() == (denied ? -1 : 4) &&
              value.at("events").size() == (denied || retire ? 0 : 4),
          "SDK changed stable impact slots, exposed denied or retired references");
    for (const auto& event : value.at("events")) {
        const auto emitter_ref = *live.engine.world().reference(live.actor().id());
        const auto collider_ref =
            *live.engine.world().reference(live.authored.entity("floor").id());
        check(event.at("tick").get<unsigned>() == 1 &&
                  event.at("space").get<unsigned>() == unsigned(planar) &&
                  event.at("kind").get<unsigned>() == 1 &&
                  event.at("effect").get<std::string>() == effect.str() &&
                  event.at("emitter_scene").get<std::string>() == emitter_ref.scene.str() &&
                  event.at("emitter").get<std::string>() == emitter_ref.entity.str() &&
                  event.at("collider_scene").get<std::string>() == collider_ref.scene.str() &&
                  event.at("collider").get<std::string>() == collider_ref.entity.str() &&
                  event.at("seed").get<unsigned>() == 17 &&
                  std::abs(event.at("position").at(1).get<double>() - .05) < 1e-4 &&
                  event.at("normal") == Json::array({0, 1, 0}) &&
                  event.at("incoming").at(1).get<double>() == -120 &&
                  event.at("outgoing").at(1).get<double>() == 60,
              "SDK copied incorrect previous-interval particle impact provenance");
    }
    auto config = live.actor().get<ParticleEmitter>();
    config.playback_speed = 0;
    live.actor().set<ParticleEmitter>(config);
    live.simulation.tick(.01f);
    rows = observations();
    check(rows.size() == 3 && rows.back().at("value").at("count").get<int>() == (denied ? -1 : 0),
          "SDK replayed a consumed visual interval on a later fixed tick");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 4 || argc == 5,
              "Need scratch root and optional allowed/denied/gallery SDK modules");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService imports(lease, particle_effect_import_registry(),
                                   {"linux", "none", "cpu"});
        auto publish = [&](const char* name, bool heavy, bool analytic, bool planar_2d = false,
                           bool world_trails = false, bool impacts = false,
                           bool billboards = false) {
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
                    if (billboards)
                        source.enable_billboards(true);
                    source.set_scene_collision(true, ParticleSceneCollision{.05, .5, 0, 4, 1});
                    if (planar_2d)
                        source.set_scene_collision_space(true);
                    if (world_trails) {
                        source.document["lifetime"] = .2;
                        if (heavy)
                            source.document["capacity"] = source.document["burst_count"] = 512;
                        source.set_world_trails(true, ParticleTrailSettings{.5, heavy ? 16u : 8u});
                    }
                    if (impacts)
                        source.set_scene_impacts(true);
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
        const auto xy = publish("Assets/XY.particles.json", false, false, true);
        const auto heavy_xy = publish("Assets/HeavyXY.particles.json", true, false, true);
        const auto world_trails = publish("Assets/World.particles.json", false, false, false, true);
        const auto world_trails_xy =
            publish("Assets/WorldXY.particles.json", false, false, true, true);
        const auto heavy_trails =
            publish("Assets/HeavyWorld.particles.json", true, false, false, true);
        world_and_transport(root, scene_effect);
        world_and_transport(root, xy, true);
        mixed_worlds(root, scene_effect, xy);
        projection_ownership(root, scene_effect);
        query_budget(root, heavy, analytic);
        query_budget(root, heavy_xy, analytic, true);
        historical_trails(root, world_trails, false);
        historical_trails(root, world_trails_xy, true);
        historical_reservation(root, heavy_trails);
        for (bool planar : {false, true})
            for (unsigned geometry = 0; geometry < 3; ++geometry) {
                const auto name = "Assets/Impact" + std::to_string(geometry) +
                                  (planar ? "XY.particles.json" : ".particles.json");
                const auto impact =
                    publish(name.c_str(), false, false, planar, geometry == 2, true, geometry == 1);
                impact_publication(root, impact, planar);
                if (argc >= 4) {
                    impact_sdk(root, impact, planar, argv[2], false, false);
                    impact_sdk(root, impact, planar, argv[3], true, false);
                    if (geometry == 0)
                        impact_sdk(root, impact, planar, argv[2], false, true);
                }
                if (argc == 5) {
                    Fixture code(root, scene(impact, planar), true, argv[4],
                                 "project.particle_impact_gallery");
                    code.ready();
                    code.simulation.tick(.1f);
                    check(code.actor().get<ParticleEmitter>().tint_g == 1,
                          "Gallery code reacted before the impact interval completed");
                    code.simulation.tick(.01f);
                    const auto tinted = code.actor().get<ParticleEmitter>();
                    check(tinted.tint_r == 1 && tinted.tint_g == .15f && tinted.tint_b == .5f,
                          "Actual Gallery System did not react to the previous completed impact");
                }
            }
        auto mixed_overflow = scene(heavy);
        mixed_overflow["entities"].push_back(emitter("planar", xy));
        Fixture reserved(root, mixed_overflow);
        reject([&] { reserved.ready(); });
        std::cout << "Published Scene particle runtime/transport/lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
