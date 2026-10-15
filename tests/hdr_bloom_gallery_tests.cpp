#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/material_source.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void inspect(const std::filesystem::path& root, const Json& source,
             const std::array<AssetId, 2>& materials) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto admitted = scene.document();
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    for (unsigned tick = 0; tick < 3; ++tick) {
        const auto render = extract_render_scene(simulation.presentation(1));
        check(render.settings.bloom == SceneBloom{true, 1, .6f, 2} && render.cameras.size() == 1 &&
                  render.meshes.size() == 4 && render.lights.size() == 1 &&
                  render.diagnostics.empty(),
              "Bloom Gallery runtime render admission differs");
        std::set<AssetId> observed;
        for (const auto& actor : render.meshes)
            for (const auto& slot : actor.renderer.materials)
                if (std::find(materials.begin(), materials.end(), slot.material.id) !=
                    materials.end())
                    observed.insert(slot.material.id);
        check(observed == std::set<AssetId>(materials.begin(), materials.end()),
              "Bloom Gallery lost typed emissive assignments");
        simulation.tick(1.f / 60);
    }
    check(scene.document() == admitted, "Bloom runtime rewrote authored scene");
    const std::array<std::array<float, 3>, 2> colors{{{.6f, .12f, .03f}, {.02f, .3f, .6f}}};
    for (unsigned i = 0; i < materials.size(); ++i) {
        ResourcePool<MaterialAsset> pool;
        const auto ticket =
            asset_detail::request_model_pbr_material(pool, root, catalog, {materials[i]});
        check(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
              "Bloom Gallery cooked material admission failed");
        const auto material = pool.acquire(ticket);
        check(material->values.parameters.at("emissiveStrength").value[0] == 12 &&
                  material->values.parameters.at("emissiveFactor").type ==
                      MaterialParameterType::LinearColor3 &&
                  material->values.parameters.at("emissiveFactor").value[3] == 0,
              "Bloom Gallery did not load exact emissive parameters");
        for (unsigned c = 0; c < 3; ++c)
            check(std::abs(material->values.parameters.at("emissiveFactor").value[c] -
                           colors[i][c]) < 1e-7,
                  "Bloom Gallery emissive color differs");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Feature Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path = std::filesystem::path("Scenes/48-hdr-bloom.scene.json");
        const auto source = read(root / path);
        const std::array paths{"Assets/Bloom/WarmGlow.material.json",
                               "Assets/Bloom/CoolGlow.material.json"};
        std::array<AssetId, 2> materials;
        const auto catalog = AssetCatalog::open_project(root);
        for (unsigned i = 0; i < materials.size(); ++i) {
            MaterialSource material{read(root / paths[i])};
            material.validate();
            materials[i] = material.asset();
            const auto& selected = catalog.records().at(materials[i]).metadata.at("forge.import");
            check(selected.at("platform") == "windows" && selected.at("backend") == "d3d12",
                  "Bloom Gallery selected wrong material target");
        }
        inspect(root, source, materials);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
        }
        const auto package = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(package.string() + "-relocated");
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 3, "Bloom export included unrelated assets");
        for (const auto path : paths)
            check(!std::filesystem::exists(moved / path),
                  "Bloom material source leaked into export");
        const auto exported_source = read(moved / shipped.records().at(roots[0]).source);
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        inspect(moved, exported_source, materials);
        std::cout
            << "Gallery48 runtime/cooked material/scene and relocated source-free export PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
