#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
#include <iostream>
using namespace forge;
namespace {
constexpr auto path = "Scenes/161-fixed-ecs-workers.scene.json";
constexpr auto identity = "16100000-0161-4161-8161-000000000001";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / path), "Worker example already exists");
    WorldContext context;
    Scene scene(context);
    auto initial = scene.document();
    initial["asset_id"] = identity;
    scene.replace(initial);
    const auto create = [&](const std::string& name, const char* recipe,
                            LocalTranslation position) {
        const auto id =
            authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
                .at("selected")
                .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>(position);
        return entity;
    };
    create("Floor", "primitive.0", {0, -.6, 3})
        .set<LocalScale>({18, .2, 12})
        .set<Tint>({.18f, .22f, .28f});
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            create("Motion cube " + std::to_string(row * 4 + column + 1) +
                       " - add Parallel Motion after building the example",
                   "primitive.0", {double(column) * 2 - 3, 0, double(row) * 2})
                .set<LocalScale>({.6, .6, .6})
                .set<Tint>({row % 2 ? .9f : .2f, .65f, row % 2 ? .25f : .95f});
    Camera camera;
    camera.far_plane = 100;
    create("Game camera", "render.camera", {0, 8, -14})
        .set<Camera>(camera)
        .set<LocalRotation>(rotation_from_euler({22, 0, 0}));
    Light light;
    light.intensity = 3;
    create("Sun", "render.light", {0, 6, -5}).set<Light>(light);
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
}
void inspect(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    WorldContext context;
    Scene scene(context);
    scene.load(root / path);
    check(scene.asset_id().str() == identity, "Worker Gallery identity changed");
    const auto original = scene.document();
    unsigned cubes = 0;
    for (const auto& row : original.at("entities"))
        if (row.at("name").get<std::string>().starts_with("Motion cube ")) {
            check(row.at("components").contains("forge.local_translation"),
                  "Motion cube has no owned position");
            ++cubes;
        }
    check(cubes == 16, "Worker Gallery must supply sixteen editable motion cubes");
    std::filesystem::create_directories(scratch);
    const auto destination = scratch / AssetId::generate().str();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{destination};
    const std::array roots{scene.asset_id()};
    package_runtime_content(root, destination, roots, {"linux", "none"});
    auto catalog = open_runtime_content(destination, {"linux", "none"});
    Scene restored(context);
    restored.load(destination / catalog.records().at(scene.asset_id()).source);
    check(restored.document() == original,
          "Source-free worker stage lost its identities or values");
    std::cout
        << "Original worker example stage/source-free content passed; C++ motion requires setup\n";
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
