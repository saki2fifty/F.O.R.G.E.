#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void exercise(const std::filesystem::path& root, const Json& source) {
    EngineContext engine(WorldRole::Runtime, false, {particle_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto admitted = scene.document();
    RuntimeSimulation simulation(engine.world(), scene);
    const auto particles = particle_runtime(engine.world());
    check(bool(particles), "Gallery54 runtime particle module missing");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Gallery54 particle preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    std::size_t continuous = 0, bursts = 0;
    for (unsigned tick = 0; tick < 20; ++tick) {
        const double elapsed = .25 * tick;
        const auto captured = simulation.presentation(1);
        const auto render = extract_render_scene(captured);
        check(render.diagnostics.empty() && render.particles.size() == 3 &&
                  render.meshes.size() == 1 && render.cameras.size() == 1 &&
                  render.lights.size() == 1,
              "Gallery54 lost particle/floor/camera/light render consumers");
        check(captured.at("entities").size() == source.at("entities").size() &&
                  scene.document() == admitted,
              "Gallery54 created per-particle Scene entities or rewrote authored values");
        for (const auto& emitter : render.particles) {
            check(emitter.time == elapsed && valid_content_digest(emitter.revision) &&
                      emitter.generation > 0,
                  "Gallery54 compact capture lost its fixed-clock/selected-revision guard");
            const auto actor = scene.entity(emitter.entity.str());
            const auto authored =
                std::find_if(source.at("entities").begin(), source.at("entities").end(),
                             [&](const auto& row) { return row.at("id") == Json(emitter.entity); });
            check(authored != source.at("entities").end(), "Gallery54 emitter identity changed");
            const auto name = authored->at("name").get<std::string>();
            const auto samples = particles->sample(actor.id(), 1);
            if (std::string_view(name).starts_with("Initial burst")) {
                check(samples.size() == (elapsed < 4 ? 64u : 0u),
                      "Gallery54 initial burst did not die at its authored lifetime");
                ++bursts;
            } else {
                const auto born = std::uint64_t(std::floor(elapsed * 32));
                // Independent integer birth count with strict lifetime, not the sampler loop.
                std::uint64_t dead = 0;
                for (std::uint64_t birth = 1; birth <= born; ++birth)
                    if (elapsed - double(birth) / 32 >= 2.2)
                        ++dead;
                check(samples.size() == born - dead,
                      "Gallery54 continuous effect differs from its independent birth oracle");
                ++continuous;
            }
        }
        // Repeated host/presentation reads are observations, never particle-clock drivers.
        check(simulation.presentation(1) == captured,
              "Gallery54 presentation observation advanced particle time");
        simulation.tick(.25f);
    }
    check(continuous == 40 && bursts == 20, "Gallery54 did not exercise every emitter");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery root and particle scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path = std::filesystem::path("Scenes/54-cpu-mesh-particles.scene.json");
        const auto source = read(root / path);
        const auto catalog = AssetCatalog::open_project(root);
        std::size_t effects = 0;
        std::set<AssetId> referenced;
        for (const auto& row : source.at("entities"))
            if (row.at("components").contains("forge.particle_emitter"))
                referenced.insert(
                    row.at("components").at("forge.particle_emitter").at("effect").get<AssetId>());
        for (const auto& [id, record] : catalog.records())
            if (referenced.contains(id)) {
                const auto& imported = record.metadata.at("forge.import");
                check(record.type == ParticleEffectAsset::type &&
                          imported.at("platform") == "windows" &&
                          imported.at("backend") == "d3d12" && imported.at("output_version") == 1,
                      "Gallery54 selected the wrong effect cook target/version");
                ++effects;
            }
        check(effects == 3, "Gallery54 did not contain exactly its three original effects");
        exercise(root, source);
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
        check(shipped.records().size() == 4,
              "Gallery54 export included unrelated authoring content");
        check(!std::filesystem::exists(relocated / "Assets/Particles"),
              "Particle authoring sources leaked into exported runtime content");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        exercise(relocated, read(relocated / shipped.records().at(roots[0]).source));
        std::cout << "Gallery54 original effects, fixed/presentation clock, birth/death and "
                     "relocated source-offline runtime content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
