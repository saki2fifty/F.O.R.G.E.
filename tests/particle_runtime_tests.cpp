#include "engine_render_resource.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_gpu_plan.hpp"
#include "particle_render_instances.hpp"
#include "particle_runtime.hpp"
#include <forge/application_state_components.hpp>
#include <forge/application_states.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid particle runtime operation accepted");
}
struct Fixture {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<ParticleRuntime> particles;
    Fixture(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false, {particle_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), particles(particle_runtime(engine.world())) {
        scene.restore_snapshot(source);
        simulation.reset_presentation();
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!particles->prepare()) {
            require(std::chrono::steady_clock::now() < deadline, "Particle preparation timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
};
void gpu_packet_admission() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.set_evaluation(ParticleEvaluation::Gpu);
    const auto data = source.compiled();
    auto cpu = data;
    cpu.version = 2;
    const AffineTransform world;
    CameraView camera{};
    for (double time : {0., .5, 2., 3., 1000000000.}) {
        const auto values = prepare_particle_gpu_values(data, time, 17, world, camera);
        const auto samples = sample_particle_effect(cpu, time, 17);
        require(values.births[0] == samples.size(),
                "GPU packet changed fixed-clock birth membership");
        if (!samples.empty()) {
            const auto ordinal =
                std::uint64_t(values.births[1]) | (std::uint64_t(values.births[2]) << 32);
            require(ordinal == samples.front().birth &&
                        std::abs(double(values.timing[0]) - samples.front().age) < 1e-5,
                    "GPU packet lost the exact ordinal or short relative age");
        }
    }
    reject([&] { (void)prepare_particle_gpu_values(cpu, .5, 17, world, camera); });
    for (double time : {-1., 1000000001., std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()})
        reject([&] { (void)prepare_particle_gpu_values(data, time, 17, world, camera); });
    auto oversized = world;
    oversized.m[3] = 1e300;
    reject([&] { (void)prepare_particle_gpu_values(data, .5, 17, oversized, camera); });
    reject([&] { (void)prepare_particle_gpu_values(data, .5, 17, world, camera, .001); });
    reject(
        [&] { (void)prepare_particle_gpu_values(data, .5, 17, world, camera, 1, {1, 1.1f, 1}); });
    auto collapsed = data;
    collapsed.size_curve = {
        {0, .1, false}, {.5, .2, false}, {std::nextafter(.5, 1.), .3, false}, {1, .1, false}};
    reject([&] { (void)prepare_particle_gpu_values(collapsed, .5, 17, world, camera); });
}
void affine_and_envelope() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    auto data = source.compiled();
    data.emission = ParticleEmission::Burst;
    data.burst_count = 32;
    data.cone_angle = 65;
    data.shape_size = {4, 7, 3};
    data.shape_radius = 5;
    data.size_curve = {{0, .1, false}, {.4, 3, true}, {1, .2, false}};
    data.color_gradient = {{0, {1, 0, 0}, false}, {.5, {0, 1, 0}, true}, {1, {0, 0, 1}, false}};
    AffineTransform emitter;
    emitter.m = {-2, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
    MeshBounds mesh{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
    for (const auto shape : {ParticleSpawnShape::Point, ParticleSpawnShape::Box,
                             ParticleSpawnShape::Sphere, ParticleSpawnShape::Disc}) {
        data.shape = shape;
        for (const double drag : {0., 1e-8, 2., 64.}) {
            data.drag = drag;
            const auto envelope = particle_effect_bounds(data, mesh, emitter);
            for (unsigned step = 0; step < 20; ++step) {
                const auto time = data.lifetime * step / 20;
                const auto samples = sample_particle_effect(data, time, 8);
                const auto instances = particle_render_instances(data, time, 8, emitter);
                require(samples.size() == instances.size(),
                        "Particle presentation changed sample count");
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    const auto position = emitter.point(
                        {samples[i].position[0], samples[i].position[1], samples[i].position[2]});
                    require(instances[i].birth == samples[i].birth &&
                                instances[i].color == samples[i].color,
                            "Particle birth/color changed at render boundary");
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        require(std::abs(instances[i].world.m[axis * 4 + 3] - position[axis]) <
                                    1e-10,
                                "Particle local motion did not follow emitter affine");
                        for (unsigned lane = 0; lane < 3; ++lane)
                            require(instances[i].world.m[axis * 4 + lane] ==
                                        emitter.m[axis * 4 + lane] * samples[i].size,
                                    "Particle size did not preserve emitter reflection/shear");
                    }
                    const auto bounds = transform_bounds(mesh, instances[i].world);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        require(
                            bounds.minimum[axis] >= envelope.minimum[axis] &&
                                bounds.maximum[axis] <= envelope.maximum[axis],
                            "Whole-lifetime particle envelope lost an actual world-space corner");
                }
            }
        }
    }
}
void instance_parameters() {
    auto data = ParticleEffectSource::create(AssetId::generate()).compiled();
    data.emission = ParticleEmission::Burst;
    data.burst_count = 32;
    data.shape = ParticleSpawnShape::Sphere;
    data.shape_radius = 4;
    data.drag = 2;
    data.size_curve = {{0, .1, false}, {.4, 3, true}, {1, .2, false}};
    data.color_gradient = {{0, {1, .5f, .75f}, false}, {1, {.5f, 1, .25f}, false}};
    AffineTransform affine;
    affine.m = {-2, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
    const MeshBounds mesh{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
    const std::array<float, 3> tint{.25f, .5f, 0};
    for (const double size : {.01, .5, 1., 4., 16.}) {
        const auto bounds = particle_effect_bounds(data, mesh, affine, size);
        for (unsigned step = 0; step < 20; ++step) {
            const auto time = data.lifetime * step / 20;
            const auto samples = sample_particle_effect(data, time, 8);
            auto modulated = samples;
            modulate_particle_samples(modulated, size, tint);
            const auto instances =
                particle_render_instances(data, time, 8, affine, 1024, size, tint);
            require(instances.size() == samples.size(), "Appearance changed particle count");
            for (std::size_t i = 0; i < samples.size(); ++i) {
                const auto& old = samples[i];
                const auto& value = modulated[i];
                require(value.birth == old.birth && value.age == old.age &&
                            value.position == old.position && value.velocity == old.velocity &&
                            value.size == old.size * size,
                        "Appearance changed birth/age/motion or ignored size curve");
                const auto position = affine.point(old.position);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    require(value.color[axis] == old.color[axis] * tint[axis] &&
                                instances[i].color[axis] == value.color[axis],
                            "Tint did not multiply evaluated gradient");
                    require(std::abs(instances[i].world.m[axis * 4 + 3] - position[axis]) < 1e-10,
                            "Emitter size scaled its particle origin or trajectory");
                    for (unsigned lane = 0; lane < 3; ++lane)
                        require(std::abs(instances[i].world.m[axis * 4 + lane] -
                                         affine.m[axis * 4 + lane] * old.size * size) < 1e-10,
                                "Size lost emitter affine or was applied twice");
                }
                for (unsigned corner = 0; corner < 8; ++corner) {
                    const auto point = instances[i].world.point(
                        {corner & 1 ? .5 : -.5, corner & 2 ? .5 : -.5, corner & 4 ? .5 : -.5});
                    for (unsigned axis = 0; axis < 3; ++axis)
                        require(point[axis] >= bounds.minimum[axis] &&
                                    point[axis] <= bounds.maximum[axis],
                                "Emitter multiplier was omitted from conservative bounds");
                }
            }
        }
    }
    auto samples = sample_particle_effect(data, .5, 8);
    const auto unchanged = samples;
    modulate_particle_samples(samples, 1, {1, 1, 1});
    require(samples == unchanged, "Default appearance changed legacy samples");
    for (const auto size : {0., .009, 16.01, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        reject([&] { modulate_particle_samples(samples, size, tint); });
        reject([&] { particle_effect_bounds(data, mesh, affine, size); });
        require(samples == unchanged, "Invalid size partially modulated caller samples");
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        for (const auto channel : {-1.f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
            auto invalid = tint;
            invalid[axis] = channel;
            reject([&] { modulate_particle_samples(samples, 2, invalid); });
            require(samples == unchanged, "Invalid tint partially modulated caller samples");
        }
}
void graph_envelope() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    ParticleGraphProgram program;
    program.shader.id = AssetId::generate();
    program.extent = {2, 3, 4};
    program.maximum_size = 5;
    source.set_graph_program(program);
    const auto data = source.compiled();
    const MeshBounds mesh{{-3.f, -.5f, -.2f}, {1.f, 2.f, .8f}};
    AffineTransform world;
    world.m = {-2, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
    CameraView camera{};
    const ParticleGpuGraphLimits limits{{2, 3, 4, 5}};
    for (double multiplier : {.01, 1., 16.}) {
        const auto bounds = particle_effect_bounds(data, mesh, world, multiplier);
        const auto values = prepare_particle_gpu_values(data, .5, 17, world, camera, multiplier);
        validate_particle_gpu_graph_envelope(values, limits);
        for (unsigned position_corner = 0; position_corner < 8; ++position_corner)
            for (unsigned mesh_corner = 0; mesh_corner < 8; ++mesh_corner)
                for (double size : {0., program.maximum_size * multiplier}) {
                    Double3 local{};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        local[axis] =
                            ((position_corner & (1u << axis)) ? 1 : -1) * program.extent[axis] +
                            size * ((mesh_corner & (1u << axis)) ? mesh.maximum[axis]
                                                                 : mesh.minimum[axis]);
                    const auto point = world.point(local);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        require(point[axis] >= bounds.minimum[axis] &&
                                    point[axis] <= bounds.maximum[axis],
                                "Graph culling envelope lost a clamped position/mesh corner under "
                                "affine modulation");
                }
    }
    reject([&] { (void)sample_particle_effect(data, .5, 17); });
    auto invalid = data;
    invalid.program->extent[0] = 1e-100;
    reject([&] { (void)particle_effect_bounds(invalid, mesh, world); });
    reject([&] { (void)prepare_particle_gpu_values(invalid, .5, 17, world, camera); });
}
void billboard_geometry() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.enable_billboards(true);
    auto data = source.compiled();
    data.emission = ParticleEmission::Burst;
    data.burst_count = 16;
    data.shape = ParticleSpawnShape::Box;
    data.shape_size = {2, 3, 4};
    data.size_curve = {{0, .2, false}, {.4, 3, true}, {1, .1, false}};
    const MeshBounds quad{{-.5f, -.5f, 0}, {.5f, .5f, 0}};
    AffineTransform emitter, side, roll;
    emitter.m = {-2, .5, 0, 10, 0, 3, 0, -2, .4, 0, 0, 7};
    side.m = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
    roll.m = {.8, -.6, 0, 0, .6, .8, 0, 0, 0, 0, 1, 0};
    for (bool collapse : {false, true}) {
        if (collapse)
            emitter.m[0] = emitter.m[4] = emitter.m[8] = 0;
        const auto envelope = particle_effect_bounds(data, quad, emitter, 2);
        for (unsigned step = 0; step < 10; ++step) {
            const auto time = data.lifetime * step / 10;
            const auto instances = particle_render_instances(data, time, 8, emitter, 1024, 2);
            auto ordinary = data;
            ordinary.version = 6;
            require(sample_particle_effect(data, time, 8) ==
                        sample_particle_effect(ordinary, time, 8),
                    "Camera alignment changed CPU births, RNG, motion or appearance");
            for (auto instance : instances) {
                const auto retained = instance.world;
                const auto width = std::hypot(retained.m[0], retained.m[4], retained.m[8]);
                const auto height = std::hypot(retained.m[1], retained.m[5], retained.m[9]);
                for (const auto& camera_world : {AffineTransform{}, side, roll})
                    for (unsigned basis : {0u, 1u})
                        for (unsigned flips = 0; flips < 4; ++flips) {
                            Camera values;
                            values.basis = basis;
                            values.flip_x = bool(flips & 1);
                            values.flip_y = bool(flips & 2);
                            const auto camera = camera_view(values, camera_world, 320, 180);
                            const auto aligned = particle_billboard_world(instance.world, camera);
                            require(instance.world == retained,
                                    "A view changed the retained CPU particle pose");
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                require(aligned.m[4 * axis + 3] == retained.m[4 * axis + 3] &&
                                            std::abs(aligned.m[4 * axis] -
                                                     camera.right[axis] * width) < 1e-12 &&
                                            std::abs(aligned.m[4 * axis + 1] -
                                                     camera.up[axis] * height) < 1e-12 &&
                                            std::abs(aligned.m[4 * axis + 2] +
                                                     camera.forward[axis] *
                                                         std::max(width, height)) < 1e-12,
                                        "Quad axes, center or dimensions disagree with the "
                                        "admitted camera");
                            }
                            const auto bounds = transform_bounds(quad, aligned);
                            for (unsigned axis = 0; axis < 3; ++axis)
                                require(
                                    bounds.minimum[axis] >= envelope.minimum[axis] &&
                                        bounds.maximum[axis] <= envelope.maximum[axis],
                                    "View-independent bounds clipped a rotated or flattened quad");
                        }
            }
        }
    }
    auto bad = emitter;
    bad.m[3] = std::numeric_limits<double>::infinity();
    reject([&] { particle_billboard_world(bad, CameraView{}); });
}
void resource_profile() {
    auto mesh = asset_detail::engine_mesh_resource(engine_primitive(0));
    validate_particle_mesh_profile(mesh);
    auto unsupported = mesh;
    unsupported.mesh.lods.front().parts.front().topology = MeshTopology::Lines;
    reject([&] { validate_particle_mesh_profile(unsupported); });
    unsupported = mesh;
    unsupported.mesh.morph_names.push_back("Original deformation");
    reject([&] { validate_particle_mesh_profile(unsupported); });
    unsupported = mesh;
    unsupported.mesh.lods.front().parts.front().joint_palette.push_back(0);
    reject([&] { validate_particle_mesh_profile(unsupported); });
    unsupported = mesh;
    unsupported.mesh.lods.front().parts.front().streams.push_back({"WEIGHTS_0", 4, {}});
    reject([&] { validate_particle_mesh_profile(unsupported); });
    ParticleEffectData cpu, alpha, opacity, billboard;
    alpha.version = 5;
    opacity.version = 6;
    billboard.version = 7;
    billboard.mesh = engine_primitive(8);
    auto material = asset_detail::engine_material_resource(engine_material());
    validate_particle_material_profile(material, cpu);
    material.values.alpha = MaterialAlpha::Mask;
    validate_particle_material_profile(material, cpu);
    material.values.alpha = MaterialAlpha::Blend;
    validate_render_material(material);
    reject([&] { validate_particle_material_profile(material, cpu); });
    reject([&] { validate_particle_material_profile(material, alpha); });
    material.values.depth_write = false;
    validate_particle_material_profile(material, alpha);
    validate_particle_material_profile(material, opacity);
    reject([&] { validate_particle_material_profile(material, cpu); });
    material.values.depth_test = false;
    reject([&] { validate_particle_material_profile(material, alpha); });
    material.values.depth_test = true;
    material.values.alpha = MaterialAlpha::Opaque;
    reject([&] { validate_particle_material_profile(material, opacity); });
    validate_particle_material_profile(material, billboard);
    billboard.opacity_end = .5;
    reject([&] { validate_particle_material_profile(material, billboard); });
    billboard.opacity_curve = {{0, 1, false}, {1, 1, false}};
    validate_particle_material_profile(material, billboard);
    material.values.alpha = MaterialAlpha::Mask;
    validate_particle_material_profile(material, billboard);
    billboard.opacity_curve[1].value = .5;
    reject([&] { validate_particle_material_profile(material, billboard); });
    material.values.alpha = MaterialAlpha::Blend;
    validate_particle_material_profile(material, billboard);
    material.values.alpha = MaterialAlpha::Opaque;
    material.values.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.5}};
    validate_render_material(material);
    reject([&] { validate_particle_material_profile(material, cpu); });
    reject([&] { validate_particle_material_profile(material, alpha); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        {
            EngineContext isolated(WorldRole::Runtime, false, {particle_module({})});
            auto particles = particle_runtime(isolated.world());
            require(particles->prepare(), "Empty runtime required an asset catalog");
            particles->tick(1.f / 60);
            isolated.world().world().entity().set<ParticleEmitter>({{AssetId::generate()}});
            reject([&] { particles->prepare(); });
        }
        require(argc == 2, "Need particle runtime scratch root");
        gpu_packet_admission();
        affine_and_envelope();
        graph_envelope();
        instance_parameters();
        billboard_geometry();
        resource_profile();
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto document = ParticleEffectDocument::create(lease, "Assets/Effect.particles.json");
        document->edit(document->revision(), "Extended runtime particle values", [](auto& value) {
            ParticleEffectSource extended{value};
            extended.document["shape"] = "box";
            extended.document["shape_size"] = {2, 1, 3};
            extended.document["drag"] = 2;
            extended.enable_lifetime(ParticleLifetimeChannel::Size, true);
            extended.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5);
            extended.document["size_curve"][1]["value"] = .5;
            extended.enable_lifetime(ParticleLifetimeChannel::Color, true);
            value = std::move(extended.document);
        });
        document->save();
        const auto effect = document->source().asset();
        AssetImportService imports(lease, particle_effect_import_registry(),
                                   {"linux", "none", "cpu"});
        imports.submit(
            imports.prepare(document->locator(), {}, effect),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_particle_effect_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(10s), "Particle publication timed out");
        const auto receipts = imports.poll();
        require(receipts.size() == 1 && receipts.front().published,
                "Particle fixture publication failed");
        auto second = ParticleEffectDocument::create(lease, "Assets/Second.particles.json");
        second->edit(second->revision(), "Distinct emitter selection",
                     [](auto& value) { value["spawn_rate"] = 16.; });
        second->save();
        auto publish = [&](const ParticleEffectDocument& selected) {
            imports.submit(
                imports.prepare(selected.locator(), {}, selected.source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_particle_effect_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            require(imports.wait_idle(10s), "Particle lifecycle fixture publication timed out");
            const auto completed = imports.poll();
            require(completed.size() == 1 && completed.front().published,
                    "Particle lifecycle fixture publication failed");
        };
        publish(*second);
        EngineContext author;
        Scene authored(author.world());
        const auto actor =
            authoring_command(authored, "entity.create", {{"recipe", "render.particles"}})
                .at("selected")
                .get<std::string>();
        authoring_command(authored, "property.set",
                          {{"entity", actor},
                           {"component", "forge.particle_emitter"},
                           {"field", "effect"},
                           {"value", effect}});
        const auto source = authored.document();
        const auto source_revision = authored.revision();
        {
            auto legacy = source;
            auto& fields = legacy["entities"][0]["components"]["forge.particle_emitter"];
            for (const auto* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"})
                fields.erase(field);
            fields["future_extension"] = {{"retained", "original opaque value"}};
            const auto original = legacy;
            EngineContext compatible;
            Scene restored(compatible.world());
            restored.restore_snapshot(legacy);
            const auto value = restored.entity(actor).get<ParticleEmitter>();
            require(value.size_multiplier == 1 && value.tint_r == 1 && value.tint_g == 1 &&
                        value.tint_b == 1 && legacy == original,
                    "Old emitter fields did not backfill defaults without editing source input");
            require(restored.document()
                            .at("entities")[0]
                            .at("components")
                            .at("forge.particle_emitter")
                            .at("future_extension") == fields.at("future_extension"),
                    "Backfilled emitter lost unknown source extensions");
        }
        for (const auto* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"}) {
            for (const auto& value : {Json(-1), Json(20), Json(nullptr), Json("1")})
                reject([&] {
                    authoring_command(authored, "property.set",
                                      {{"entity", actor},
                                       {"component", "forge.particle_emitter"},
                                       {"field", field},
                                       {"value", value}});
                });
            require(authored.document() == source && authored.revision() == source_revision,
                    "Invalid appearance changed authored history");
        }
        reject([&] {
            authoring_command(authored, "property.set",
                              {{"entity", actor},
                               {"component", "forge.particle_emitter"},
                               {"field", "playback_speed"},
                               {"value", -1}});
        });
        require(authored.document() == source && authored.revision() == source_revision,
                "Rejected emitter property changed scene history");
        {
            EngineContext prefab_author;
            Scene prefab_scene(prefab_author.world());
            prefab_scene.restore_snapshot(source);
            auto prefab = create_prefab_source(prefab_scene, actor);
            prefab_scene.set_prefab_sources({{prefab.asset(), prefab.source}});
            const auto instance = instantiate_prefab(prefab_scene, prefab.asset());
            require(!prefab_scene.entity(instance).owns<ParticleEmitter>() &&
                        prefab_scene.entity(instance).get<ParticleEmitter>().effect.id == effect,
                    "Particle prefab lost native inheritance or its logical effect reference");
            authoring_command(prefab_scene, "property.set",
                              {{"entity", instance},
                               {"component", "forge.particle_emitter"},
                               {"field", "size_multiplier"},
                               {"value", 3.}});
            require(prefab_scene.entity(instance).owns<ParticleEmitter>() &&
                        prefab_scene.entity(instance).get<ParticleEmitter>().size_multiplier == 3 &&
                        prefab_scene.undo() &&
                        !prefab_scene.entity(instance).owns<ParticleEmitter>(),
                    "Particle prefab override/Undo changed authoritative inheritance");
            Fixture inherited(root, prefab_scene.snapshot());
            inherited.ready();
            inherited.simulation.tick(.25f);
            require(
                extract_render_scene(inherited.simulation.presentation(1)).particles.size() == 2 &&
                    inherited.particles->sample(inherited.scene.entity(instance).id(), 1).size() ==
                        8,
                "Restored particle prefab failed playback or emitted its template as a third "
                "object");
        }
        {
            Fixture gated(root, source);
            configure_application_states(
                gated.scene.world(),
                {{{"app", "", "menu", {{"menu", "", ""}, {"playing", "", ""}}, false, {}, {}}}});
            initialize_application_states(gated.scene.world());
            const auto emitter = gated.scene.entity(actor);
            emitter.set<ApplicationStateGate>({true, "app", "playing"});
            gated.ready();
            gated.simulation.tick(.25f);
            require(gated.particles->presentation(emitter.id(), 1).is_null() &&
                        extract_render_scene(gated.simulation.presentation(1)).particles.empty(),
                    "Inactive emitter retained playback or rendered particles");
            control_application_states(gated.scene.world(),
                                       {{"domain", "app"}, {"state", "playing"}});
            gated.ready();
            require(gated.particles->presentation(emitter.id(), 1).at("time") == 0 &&
                        emitter.get<ParticleEmitter>().enabled,
                    "Emitter reactivation failed to use its existing restart semantics");
            gated.simulation.tick(.25f);
            require(gated.particles->sample(emitter.id(), 1).size() == 8,
                    "Reactivated emitter did not emit");
        }
        Fixture live(root, source);
        live.ready();
        const auto entity = live.scene.entity(actor).id();
        require(live.particles->sample(entity, 1).empty(),
                "Continuous emitter did not start at time zero");
        live.simulation.tick(.25f);
        auto captured = live.simulation.presentation(1);
        auto rendered = extract_render_scene(captured);
        require(
            rendered.particles.size() == 1 && rendered.particles.front().time == .25 &&
                rendered.meshes.empty() && rendered.diagnostics.empty(),
            "Compact particle capture/extraction changed time, emitted scene particles or failed");
        require(live.particles->sample(entity, 1).size() == 8 &&
                    live.particles->presentation(entity, .5).at("time") == .125,
                "Fixed-clock particle births or interpolated age wrong");
        require(live.particles->sample(entity, 1) ==
                    sample_particle_effect(document->source().compiled(), .25,
                                           live.scene.entity(actor).get<ParticleEmitter>().seed),
                "Runtime ignored extended shape, drag or lifetime values");
        for (unsigned i = 0; i < 100; ++i)
            require(live.simulation.presentation(1) == captured,
                    "Presentation read advanced emission age or changed source");
        require(authored.document() == source && authored.revision() == source_revision &&
                    captured.at("entities").size() == source.at("entities").size(),
                "Runtime particles edited authored scene or allocated particle entities");
        auto config = live.scene.entity(actor).get<ParticleEmitter>();
        const auto before_appearance = live.particles->sample(entity, 1);
        const auto playback_before = live.particles->presentation(entity, 1);
        const auto ref = live.engine.world().reference(entity).value();
        live.particles->tune_appearance(ref, "size_multiplier", 2.);
        live.particles->tune_appearance(ref, "tint_r", .25);
        live.particles->tune_appearance(ref, "tint_g", .5);
        live.particles->tune_appearance(ref, "tint_b", 0.);
        (void)live.simulation.presentation(1); // Adopt edits while held, with no tick.
        auto expected = before_appearance;
        modulate_particle_samples(expected, 2, {.25f, .5f, 0});
        require(live.particles->sample(entity, 1) == expected &&
                    live.particles->presentation(entity, 1) == playback_before &&
                    authored.document() == source && authored.revision() == source_revision,
                "Live appearance restarted/reloaded/moved particles or touched authoring");
        const auto edited = live.scene.entity(actor).get<ParticleEmitter>();
        for (const auto* field : {"effect", "seed", "enabled", "playback_speed", "missing"})
            reject([&] { live.particles->tune_appearance(ref, field, 1); });
        reject([&] { live.particles->tune_appearance(ref, "tint_r", 2); });
        reject([&] {
            live.particles->tune_appearance({AssetId::generate(), ref.entity}, "size_multiplier",
                                            2);
        });
        require(live.scene.entity(actor).get<ParticleEmitter>() == edited,
                "Rejected Live appearance changed native values");
        auto invalid = edited;
        invalid.size_multiplier = std::numeric_limits<double>::quiet_NaN();
        live.scene.entity(actor).set(invalid);
        reject([&] { live.particles->prepare(); });
        live.scene.entity(actor).set(config);
        live.particles->synchronize();
        require(live.particles->sample(entity, 1) == before_appearance &&
                    live.particles->presentation(entity, 1) == playback_before,
                "Invalid native appearance changed admitted emission state");
        for (auto& row : captured["entities"])
            if (row.at("id") == actor)
                row["components"]["forge.particle_emitter"]["size_multiplier"] = 20.;
        require(!extract_render_scene(captured).diagnostics.empty(),
                "Invalid captured appearance entered render extraction");
        config.playback_speed = 0;
        live.scene.entity(actor).set(config);
        live.simulation.tick(.25f);
        require(live.particles->presentation(entity, 1).at("time") == .25,
                "Zero speed did not freeze emission");
        config.playback_speed = 2;
        live.scene.entity(actor).set(config);
        live.simulation.tick(.25f);
        require(live.particles->presentation(entity, 1).at("time") == .75,
                "Emitter speed did not multiply fixed dt");
        config.seed = 9;
        live.scene.entity(actor).set(config);
        live.particles->synchronize();
        require(live.particles->presentation(entity, 1).at("time") == 0 &&
                    live.particles->sample(entity, 1).empty(),
                "Changed seed retained old particles");
        config.enabled = false;
        live.scene.entity(actor).set(config);
        live.particles->synchronize();
        require(live.particles->presentation(entity, 1).is_null(),
                "Disabled emitter retained playback");
        config.enabled = true;
        live.scene.entity(actor).set(config);
        live.ready();
        require(live.particles->presentation(entity, 1).at("time") == 0,
                "Reenabled emitter retained age");
        config.effect = {second->source().asset()};
        config.playback_speed = 1;
        live.scene.entity(actor).set(config);
        live.ready();
        require(live.particles->presentation(entity, 1).at("time") == 0,
                "Changed effect retained the old emission age");
        live.simulation.tick(.25f);
        require(live.particles->sample(entity, 1).size() == 4,
                "Changed effect retained the old cooked emission values");
        live.scene.entity(actor).remove<ParticleEmitter>();
        live.particles->synchronize();
        require(live.particles->presentation(entity, 1).is_null(),
                "Removed component retained transient particle state");
        config.effect = {effect};
        live.scene.entity(actor).set(config);
        live.ready();
        require(live.particles->presentation(entity, 1).at("time") == 0,
                "Reattached emitter retained removed component age");
        auto bad = captured;
        bad["entities"][0]["particle_playback"]["seed"] = 17u;
        require(!extract_render_scene(bad).diagnostics.empty(),
                "Renderer accepted stale particle seed capture");
        bad = captured;
        bad["entities"][0]["particle_playback"]["seed"] =
            (std::uint64_t(1) << 32) + captured.at("entities")[0]
                                           .at("components")
                                           .at("forge.particle_emitter")
                                           .at("seed")
                                           .get<std::uint32_t>();
        require(!extract_render_scene(bad).diagnostics.empty(),
                "Renderer narrowed an overflowing particle seed capture before checking it");
        reject([&] { live.particles->sample(entity, 2); });
        // Destruction before restore forces native generation turnover, even for the same UUID.
        authoring_command(live.scene, "entity.delete", {{"entity", actor}});
        live.particles->synchronize();
        live.scene.restore_snapshot(source);
        live.ready();
        const auto replacement = live.scene.entity(actor).id();
        require(replacement != entity && live.particles->presentation(entity, 1).is_null() &&
                    live.particles->presentation(replacement, 1).at("time") == 0,
                "Reconstructed emitter reused deleted native generation playback");
        Fixture recovered(root, live.scene.document());
        recovered.ready();
        require(
            recovered.particles->presentation(recovered.scene.entity(actor).id(), 1).at("time") ==
                0,
            "Visual recovery did not restart from the authored seed");
        auto many = source;
        for (unsigned i = 1; i < 65; ++i) {
            auto row = source.at("entities").front();
            row["id"] = EntityId::generate();
            many["entities"].push_back(std::move(row));
        }
        Fixture overflow(root, many);
        reject([&] { overflow.ready(); });
        document->edit(document->revision(), "Maximum reserved capacity",
                       [](auto& value) { value["capacity"] = 1024; });
        document->save();
        publish(*document);
        many["entities"].erase(many["entities"].begin() + 9, many["entities"].end());
        Fixture capacity_overflow(root, many);
        reject([&] { capacity_overflow.ready(); });
        many["entities"].erase(many["entities"].end() - 1);
        Fixture capacity_boundary(root, many);
        capacity_boundary.ready();
        capacity_boundary.simulation.tick(.25f);
        require(
            extract_render_scene(capacity_boundary.simulation.presentation(1)).particles.size() ==
                8,
            "8192 reserved-particle boundary rejected eight1024-capacity emitters");
        std::cout << "Particle native reflection, fixed clock, affine bounds, capture, pause and "
                     "reconstruction passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
