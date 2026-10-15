#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <cmath>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& file) {
    Json result;
    std::ifstream(file) >> result;
    return result;
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::map<AssetId, ParticleEffectData>& values) {
    EngineContext engine(WorldRole::Runtime, false, {particle_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto admitted = scene.document();
    RuntimeSimulation simulation(engine.world(), scene);
    const auto particles = particle_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery56 preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    std::array<bool, 4> shapes{};
    bool drag = false, size_curve = false, color_gradient = false;
    for (unsigned tick = 0; tick < 24; ++tick) {
        const double elapsed = .25 * tick;
        const auto captured = simulation.presentation(1);
        const auto render = extract_render_scene(captured);
        check(render.diagnostics.empty() && render.particles.size() == 6 &&
                  render.meshes.size() == 1 && render.cameras.size() == 1 &&
                  render.lights.size() == 1,
              "Gallery56 lost its six effects or presentation consumers");
        for (const auto& emitter : render.particles) {
            check(emitter.time == elapsed && valid_content_digest(emitter.revision) &&
                      emitter.generation > 0,
                  "Gallery56 lost compact fixed-clock selection guards");
            const auto& data = values.at(emitter.emitter.effect.id);
            const auto samples = particles->sample(scene.entity(emitter.entity.str()).id(), 1);
            const auto born = std::uint64_t(elapsed * 24);
            const auto dead = elapsed < 3 ? 0u : std::uint64_t((elapsed - 3) * 24);
            check(samples.size() == born - dead,
                  "Gallery56 lifetime/birth count differs from integer oracle");
            check(samples == sample_particle_effect(data, elapsed, 101),
                  "Gallery56 runtime ignored cooked shape/drag/lifetime values");
            shapes[unsigned(data.shape)] = true;
            drag |= data.drag > 0;
            size_curve |= !data.size_curve.empty();
            color_gradient |= !data.color_gradient.empty();
            for (const auto& sample : samples) {
                // Recover birth position independently from the analytic equation.
                std::array<double, 3> origin{};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    if (data.drag == 0) {
                        const auto initial_velocity =
                            sample.velocity[axis] - data.acceleration[axis] * sample.age;
                        origin[axis] = sample.position[axis] - initial_velocity * sample.age -
                                       .5 * data.acceleration[axis] * sample.age * sample.age;
                    } else {
                        // The paired jets have zero acceleration and the same seed/cone/speed.
                        const auto initial_velocity =
                            sample.velocity[axis] * std::exp(data.drag * sample.age);
                        origin[axis] =
                            sample.position[axis] -
                            initial_velocity * (1 - std::exp(-data.drag * sample.age)) / data.drag;
                    }
                }
                if (data.shape == ParticleSpawnShape::Point)
                    check(std::hypot(origin[0], origin[1], origin[2]) < 1e-9,
                          "Gallery56 point or drag jet acquired a spawn volume");
                else if (data.shape == ParticleSpawnShape::Box)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        check(std::abs(origin[axis]) <= data.shape_size[axis] * .5 + 1e-9,
                              "Gallery56 Box uses radius instead of full dimensions");
                else if (data.shape == ParticleSpawnShape::Sphere)
                    check(std::hypot(origin[0], origin[1], origin[2]) <= data.shape_radius + 1e-9,
                          "Gallery56 Sphere escaped its volume");
                else
                    check(std::abs(origin[1]) < 1e-9 &&
                              std::hypot(origin[0], origin[2]) <= data.shape_radius + 1e-9,
                          "Gallery56 Disc is outside its emitter-axis plane");
                check(sample.size >= .04 - 1e-9 && sample.size <= .3 + 1e-9,
                      "Gallery56 size curve escaped its independently authored range");
            }
        }
        check(scene.document() == admitted &&
                  captured.at("entities").size() == source.at("entities").size() &&
                  simulation.presentation(1) == captured,
              "Gallery56 observations changed time, source or Scene entity ownership");
        simulation.tick(.25f);
    }
    check(std::all_of(shapes.begin(), shapes.end(), [](bool value) { return value; }) && drag &&
              size_curve && color_gradient,
          "Gallery56 omitted an authored shape, drag or lifetime consumer");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery root and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path =
            std::filesystem::path("Scenes/56-particle-shapes-and-lifetime.scene.json");
        const auto source = read(root / path);
        const auto catalog = AssetCatalog::open_project(root);
        std::map<AssetId, ParticleEffectData> values;
        for (const auto& entity : source.at("entities"))
            if (entity.at("components").contains("forge.particle_emitter")) {
                const auto effect = entity.at("components")
                                        .at("forge.particle_emitter")
                                        .at("effect")
                                        .get<AssetRef<ParticleEffectAsset>>();
                const auto& record = catalog.records().at(effect.id);
                const auto& selected = record.metadata.at("forge.import");
                check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12" &&
                          selected.at("output_version") == 2 &&
                          selected.at("importer_revision") == particle_effect_recipe,
                      "Gallery56 selected the wrong effect target/recipe");
                const auto document = read(root / record.source);
                values.emplace(effect.id, ParticleEffectSource{document}.compiled());
            }
        check(values.size() == 6, "Gallery56 effect identities are missing or duplicated");
        exercise(root, source, values);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto exported = std::filesystem::path(root.string() + "-export");
        (void)package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto relocated = std::filesystem::path(exported.string() + "-relocated");
        std::filesystem::rename(exported, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        check(shipped.records().size() == 7 &&
                  !std::filesystem::exists(relocated / "Assets/Particles/Shapes"),
              "Gallery56 export leaked unrelated assets or editable effect sources");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        exercise(relocated, read(relocated / shipped.records().at(roots[0]).source), values);
        std::cout << "Gallery56 shapes, analytic motion, lifetime curves and relocated source-free "
                     "execution passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
