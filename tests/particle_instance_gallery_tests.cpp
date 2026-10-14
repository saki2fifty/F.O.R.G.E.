#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void exercise(const std::filesystem::path& root, const Json& source,
              const ParticleEffectData& data) {
    EngineContext engine(WorldRole::Runtime, false, {particle_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto particles = particle_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery57 effect admission timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    for (unsigned tick = 0; tick < 24; ++tick) {
        const auto elapsed = .25 * tick;
        const auto capture = simulation.presentation(1);
        const auto render = extract_render_scene(capture);
        check(render.diagnostics.empty() && render.particles.size() == 6 &&
                  render.meshes.size() == 1 && render.cameras.size() == 1 &&
                  render.lights.size() == 1,
              "Gallery57 presentation consumers missing");
        std::set<AssetId> references;
        std::set<std::string> revisions;
        std::set<double> sizes;
        std::set<std::array<float, 3>> colors;
        const auto base = sample_particle_effect(data, elapsed, 101);
        const auto births = std::uint64_t(elapsed * 24);
        const auto deaths = elapsed < 3 ? 0u : std::uint64_t((elapsed - 3) * 24);
        check(base.size() == births - deaths, "Gallery57 independent emission phase oracle failed");
        for (const auto& emitter : render.particles) {
            const auto& config = emitter.emitter;
            references.insert(config.effect.id);
            revisions.insert(emitter.revision);
            sizes.insert(config.size_multiplier);
            colors.insert({config.tint_r, config.tint_g, config.tint_b});
            check(emitter.time == elapsed && config.seed == 101 && emitter.generation > 0 &&
                      valid_content_digest(emitter.revision),
                  "Gallery57 lost compact fixed-clock guards");
            const auto actual = particles->sample(scene.entity(emitter.entity.str()).id(), 1);
            check(actual.size() == base.size(), "Appearance changed shared emission count");
            const std::array tint{config.tint_r, config.tint_g, config.tint_b};
            for (std::size_t i = 0; i < actual.size(); ++i) {
                check(actual[i].position == base[i].position &&
                          actual[i].velocity == base[i].velocity && actual[i].age == base[i].age &&
                          actual[i].birth == base[i].birth &&
                          actual[i].size == base[i].size * config.size_multiplier,
                      "Gallery57 appearance changed shared motion or ignored size curve");
                for (unsigned axis = 0; axis < 3; ++axis)
                    check(actual[i].color[axis] == base[i].color[axis] * tint[axis],
                          "Gallery57 RGB did not multiply asset color");
            }
        }
        check(references.size() == 1 && revisions.size() == 1 &&
                  sizes == std::set<double>{.5, 1, 2} && colors.size() == 4,
              "Gallery57 copied effect selections or lost authored variants");
        check(scene.document() == source && simulation.presentation(1) == capture,
              "Gallery57 observations changed source or drove emission time");
        simulation.tick(.25f);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery source and scratch root");
        const auto original = std::filesystem::absolute(argv[1]);
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        std::filesystem::copy(original, root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/57-particle-emitter-appearance.scene.json");
        const auto catalog = AssetCatalog::open_project(root);
        const auto effect = source.at("entities")[0]
                                .at("components")
                                .at("forge.particle_emitter")
                                .at("effect")
                                .get<AssetId>();
        const auto& record = catalog.records().at(effect);
        check(record.metadata.at("forge.import").at("output_version") == 2,
              "Gallery57 effect is not the extended immutable cook");
        const auto data = ParticleEffectSource{read(root / record.source)}.compiled();
        exercise(root, source, data);
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
        check(shipped.records().size() == 2 && !std::filesystem::exists(relocated / record.source),
              "Gallery57 export duplicated assets or leaked editable effect source");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        exercise(relocated, read(relocated / shipped.records().at(roots[0]).source), data);
        std::cout
            << "Gallery57 shared emitter appearance and relocated source-free playback passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
