#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "fog_volume_prefab_tests.hpp"
#include "runtime_package.hpp"
#include "standalone_manifest.hpp"
#include <forge/authoring.hpp>
#include <forge/fog_volume.hpp>
#include <forge/render_components.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
using namespace forge;
namespace {
constexpr auto path = "Scenes/160-local-fog-volumes.scene.json";
constexpr auto identity = "b12d967b-d650-493a-855e-b2ab0c254952";
void check(bool condition, const char* why) {
    if (!condition)
        throw std::runtime_error(why);
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / path), "Fog Gallery already exists");
    WorldContext world;
    Scene scene(world);
    auto initial = scene.snapshot();
    initial["asset_id"] = identity;
    scene.replace(initial);
    auto create = [&](const char* name, const char* recipe, LocalTranslation position) {
        const auto id =
            authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
                .at("selected")
                .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>(position);
        return entity;
    };
    create("Floor", "primitive.0", {0, -.5, 6})
        .set<LocalScale>({12, 1, 18})
        .set<Tint>({.28f, .3f, .35f});
    for (double x : {-3., 0., 3.})
        for (double z : {2., 5., 9.})
            create("Depth marker", "primitive.0", {x, 1, z})
                .set<LocalScale>({.9, 2, .9})
                .set<Tint>({.85f, .7f, .35f});
    FogVolume box;
    box.half_extent_x = 1.8;
    box.half_extent_y = 2.5;
    box.half_extent_z = 4;
    box.color_r = .12f;
    box.color_g = .4f;
    box.color_b = .75f;
    box.density = .3f;
    create("Blue box mist - adjust Density", "render.fog_volume", {-3, 1, 5}).set<FogVolume>(box);
    auto sphere = box;
    sphere.shape = std::uint32_t(FogVolumeShape::Sphere);
    sphere.color_r = .7f;
    sphere.color_g = .28f;
    sphere.color_b = .12f;
    sphere.density = .22f;
    create("Warm ellipsoid mist - adjust Half extents", "render.fog_volume", {3, 1, 5})
        .set<FogVolume>(sphere);
    Camera camera;
    camera.far_plane = 100;
    create("Game camera", "render.camera", {0, 4, -12})
        .set<Camera>(camera)
        .set<LocalRotation>(rotation_from_euler({9, 0, 0}));
    Light sun;
    sun.intensity = 3;
    create("Sun", "render.light", {0, 6, -5}).set<Light>(sun);
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
}
void inspect(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    test_fog_prefab::exercise(check);
    WorldContext world;
    Scene scene(world);
    scene.load(root / path);
    check(scene.asset_id().str() == identity, "Fog Gallery identity changed");
    const auto source = scene.document();
    const auto frame = extract_render_scene(scene.effective_document());
    check(frame.fog_volumes.size() == 2 && frame.diagnostics.empty() && frame.cameras.size() == 1,
          "Fog Gallery is missing its native volumes or Game camera");
    check(frame.fog_volumes[0].volume.shape != frame.fog_volumes[1].volume.shape,
          "Fog Gallery does not demonstrate both shapes");
    std::filesystem::create_directories(scratch);
    const auto destination = scratch / AssetId::generate().str();
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{destination};
    const std::array roots{scene.asset_id()};
    package_runtime_content(root, destination, roots, {"windows", "d3d12"});
    const auto catalog = open_runtime_content(destination, {"windows", "d3d12"});
    const auto& record = catalog.records().at(scene.asset_id());
    check(record.metadata.at("forge.runtime_document").at("runtime_features") ==
              Json{{"fog_volumes", fog_volume_profile_version}},
          "Fog package omitted runtime support");
    validate_runtime_compatibility(catalog, {{"runtime_fog_volumes", fog_volume_profile_version}});
    bool rejected = false;
    try {
        validate_runtime_compatibility(catalog, Json::object());
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Fog Gallery accepted an older kit");
    const auto packaged = asset_detail::parse_bounded_json(
        asset_detail::read_bytes(ProjectPaths(destination).resolve(record.source), 1024 * 1024),
        1024 * 1024);
    WorldContext offline;
    Scene restored(offline);
    restored.replace(packaged);
    check(restored.document() == source, "Packaged Fog values or identities changed");
    std::cout
        << "Fog Gallery native extraction, source-free content and kit compatibility passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author")
            author(std::filesystem::absolute(argv[2]));
        else if (argc == 4 && std::string_view(argv[1]) == "--inspect")
            inspect(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        else
            throw std::runtime_error("Need --author project or --inspect project scratch");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
