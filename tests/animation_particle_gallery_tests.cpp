#include "particle_effect_authoring.hpp"
#include "particle_render_instances.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    if (std::abs(a - b) >= .00001)
        throw std::runtime_error("Animated particle numeric mismatch: " + std::to_string(a) +
                                 " versus " + std::to_string(b));
}
Json read(const std::filesystem::path& path) {
    Json j;
    std::ifstream(path) >> j;
    return j;
}
double ramp(double time) {
    const auto t = std::fmod(time, 2);
    return t <= 1 ? t : 2 - t;
}
void exercise(const std::filesystem::path& root, const Json& source,
              const ParticleEffectData& data) {
    EngineContext engine(WorldRole::Runtime, false,
                         {animation_module(root), particle_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto particles = particle_runtime(engine.world());
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    animation->catalog(catalog);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose() || !particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery60 resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    std::string effect_revision;
    std::uint64_t effect_generation = 0;
    for (unsigned tick = 0; tick <= 24; ++tick) {
        const auto elapsed = tick * .25;
        const auto capture = simulation.presentation(1);
        const auto render = extract_render_scene(capture);
        check(render.diagnostics.empty() && render.particles.size() == 2 &&
                  render.meshes.size() == 1 && render.cameras.size() == 1 &&
                  render.lights.size() == 1,
              "Gallery60 render consumers missing");
        const auto base = sample_particle_effect(data, elapsed, 101);
        for (const auto& emitter : render.particles) {
            const auto entity = scene.entity(emitter.entity.str());
            const bool animated = entity.has<TransformAnimator>();
            const auto blend = ramp(elapsed);
            const double size = animated ? .5 + 1.5 * blend : 1;
            const std::array<float, 3> tint =
                animated ? std::array{float(1 - .9 * blend), float(.2 + .8 * blend),
                                      float(.1 + .15 * blend)}
                         : std::array{1.f, 1.f, 1.f};
            near(emitter.time, elapsed);
            near(emitter.emitter.size_multiplier, size);
            near(emitter.emitter.tint_r, tint[0]);
            near(emitter.emitter.tint_g, tint[1]);
            near(emitter.emitter.tint_b, tint[2]);
            check(emitter.emitter.seed == 101 && emitter.generation > 0 &&
                      valid_content_digest(emitter.revision),
                  "Animated appearance changed seed/resource admission");
            if (effect_revision.empty()) {
                effect_revision = emitter.revision;
                effect_generation = emitter.generation;
            }
            check(emitter.revision == effect_revision && emitter.generation == effect_generation,
                  "Appearance animation restarted/replaced a shared effect");
            const auto live = entity.get<ParticleEmitter>();
            const auto samples = particles->sample(entity.id(), 1);
            check(samples.size() == base.size(), "Animation changed particle births/lifetime");
            for (std::size_t i = 0; i < base.size(); ++i) {
                check(samples[i].position == base[i].position &&
                          samples[i].velocity == base[i].velocity &&
                          samples[i].birth == base[i].birth && samples[i].age == base[i].age,
                      "Appearance animation changed particle trajectory/age");
                near(samples[i].size, base[i].size * size);
                for (unsigned c = 0; c < 3; ++c)
                    near(samples[i].color[c], base[i].color[c] * tint[c]);
            }
            const auto instances = particle_render_instances(
                data, emitter.time, 101, emitter.world, 4096, emitter.emitter.size_multiplier,
                {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
            check(instances.size() == base.size(),
                  "Copied animated appearance lost render instances");
            if (tick && animated) {
                const auto interpolated = extract_render_scene(simulation.presentation(.5));
                for (const auto& item : interpolated.particles)
                    if (item.entity == emitter.entity) {
                        near(item.time, elapsed - .125);
                        near(item.emitter.size_multiplier, .5 + 1.5 * ramp(elapsed - .125));
                    }
            }
            check(entity.get<ParticleEmitter>() == live,
                  "Copied interpolation mutated live appearance");
        }
        check(simulation.presentation(1) == capture,
              "Repeated presentation advanced animation/emission");
        simulation.tick(.25f);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch directory");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{scratch};
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/60-animated-particle-appearance.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        std::filesystem::path effect_source, clip_source;
        for (const auto& [id, row] : catalog.records()) {
            if (row.source == "Assets/Particles/SharedJet.particles.json")
                effect_source = row.source;
            if (row.source == "Assets/AnimationTiming/ParticleAppearance.animation.json")
                clip_source = row.source;
        }
        const auto data = ParticleEffectSource{read(root / effect_source)}.compiled();
        exercise(root, source, data);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease writer(root);
            prepare_runtime_content_catalog(writer, roots, {});
        }
        const auto exported = scratch / "export";
        package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto moved = scratch / "relocated";
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 3 && !std::filesystem::exists(moved / effect_source) &&
                  !std::filesystem::exists(moved / clip_source),
              "Gallery60 export lost closure or leaked editable sources");
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(moved, read(moved / shipped.records().at(roots[0]).source), data);
        std::cout << "Gallery60 animation/particle clock, seed/revision/motion, interpolated "
                     "instances and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
