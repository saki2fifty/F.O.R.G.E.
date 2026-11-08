#pragma once
#include "planar_mesh_gallery.hpp"
#include <forge/scene_identity.hpp>

namespace forge::test_concave_gallery {
inline constexpr auto scene_path = "Scenes/202-concave-planar-meshes.scene.json";
inline constexpr std::array<const char*, 3> mesh_paths{"Assets/Planar/LShape.mesh.json",
                                                       "Assets/Planar/ClockwiseL.mesh.json",
                                                       "Assets/Planar/Arrow.mesh.json"};
inline const std::array<std::vector<std::array<double, 2>>, 3> outlines{
    std::vector<std::array<double, 2>>{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}},
    std::vector<std::array<double, 2>>{{0, 3}, {1, 3}, {1, 1}, {3, 1}, {3, 0}, {0, 0}},
    std::vector<std::array<double, 2>>{
        {-1, -.5}, {0, -.5}, {0, -1}, {2, 0}, {0, 1}, {0, .5}, {-1, .5}}};
inline constexpr std::array<std::array<double, 2>, 3> places{{{-5, -1.5}, {-1, -1.5}, {4, 0}}};
using test_planar_gallery::read;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline void verify_sources(const std::filesystem::path& root) {
    // Replay old factories against their already-shipped source intent. This
    // guards the shared constructor extraction, not just triangle counts.
    const auto quad = read(root / test_planar_gallery::mesh_paths[0]);
    const auto polygon = read(root / test_planar_gallery::mesh_paths[1]);
    const auto triangle = read(root / test_planar_gallery::mesh_paths[2]);
    const std::array<std::array<double, 2>, 5> convex{
        {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-2, 0}}};
    const std::array<std::array<double, 2>, 3> clockwise{{{.75, -.5}, {-.75, -.5}, {0, .75}}};
    check(EditableMeshSource::create_planar_quad(quad.at("asset_id"), 2, 2).document == quad &&
              EditableMeshSource::create_planar_polygon(polygon.at("asset_id"), convex).document ==
                  polygon &&
              EditableMeshSource::create_planar_polygon(triangle.at("asset_id"), clockwise)
                      .document == triangle,
          "Shared construction changed previously shipped flat source intent");
    for (unsigned i = 0; i < mesh_paths.size(); ++i) {
        const auto source = read(root / mesh_paths[i]);
        check(EditableMeshSource::create_planar_outline(source.at("asset_id"), outlines[i])
                      .document == source,
              "Concave saved source differs from its original factory intent");
    }
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Concave Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
    std::array<AssetId, 3> meshes;
    for (unsigned i = 0; i < outlines.size(); ++i) {
        auto source = EditableMeshSource::create_planar_outline(AssetId::generate(), outlines[i]);
        check(!std::filesystem::exists(root / mesh_paths[i]), "Concave Mesh source already exists");
        atomic_write(root / mesh_paths[i], source.document.dump(2));
        meshes[i] = source.asset();
        test_planar_gallery::publish(lease, mesh_paths[i], source.asset(),
                                     editable_mesh_import_registry(),
                                     prepare_editable_mesh_publication);
    }
    const auto material =
        read(root / test_planar_gallery::material_path).at("asset_id").get<AssetId>();
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(duplicate_scene_asset(scene.document(), AssetId::generate(), scene.schema()));
    const auto create = [&](const char* name, const char* recipe, const Json& values, double x,
                            double y, double z) {
        authoring_command(scene, "entity.create",
                          {{"name", name},
                           {"recipe", recipe},
                           {"component_values", values},
                           {"position", {{"x", x}, {"y", y}, {"z", z}}}});
    };
    create("Orthographic concave shapes camera", "render.camera",
           {{"projection", 1}, {"orthographic_height", 7}}, 0, 0, -8);
    const std::array<const char*, 3> names{"Concave L - counterclockwise input",
                                           "Concave L - clockwise input", "Concave arrow"};
    for (unsigned i = 0; i < meshes.size(); ++i)
        create(names[i], "render.mesh",
               {{"mesh", meshes[i]},
                {"materials", Json::array({{{"slot", "default"}, {"material", material}}})},
                {"cast_shadows", false}},
               places[i][0], places[i][1], 0);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    verify_sources(root);
}
} // namespace forge::test_concave_gallery
