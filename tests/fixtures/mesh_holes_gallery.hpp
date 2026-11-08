#pragma once
#include "planar_mesh_gallery.hpp"
#include <forge/scene_identity.hpp>

namespace forge::test_mesh_holes_gallery {
inline constexpr auto scene_path = "Scenes/203-planar-mesh-holes.scene.json";
inline constexpr std::array<const char*, 3> mesh_paths{"Assets/Planar/Frame.mesh.json",
                                                       "Assets/Planar/WindowPanel.mesh.json",
                                                       "Assets/Planar/CutoutL.mesh.json"};
using Loop = std::vector<std::array<double, 2>>;
inline const std::array<Loop, 3> outlines{Loop{{-1.5, -1.5}, {1.5, -1.5}, {1.5, 1.5}, {-1.5, 1.5}},
                                          Loop{{-1.5, 1.5}, {1.5, 1.5}, {1.5, -1.5}, {-1.5, -1.5}},
                                          Loop{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}};
inline const std::array<std::vector<Loop>, 3> holes{
    std::vector<Loop>{{{-.8, -.8}, {.8, -.8}, {.8, .8}, {-.8, .8}}},
    std::vector<Loop>{{{-1.25, -.6}, {-.75, -.6}, {-.75, .6}, {-1.25, .6}},
                      {{-.25, .6}, {.25, .6}, {.25, -.6}, {-.25, -.6}},
                      {{.75, -.6}, {1.25, -.6}, {1.25, .6}, {.75, .6}}},
    std::vector<Loop>{{{.25, 1.5}, {.75, 1.5}, {.75, 2.5}, {.25, 2.5}}}};
inline constexpr std::array<std::array<double, 2>, 3> places{{{-4.5, 0}, {0, 0}, {3, -1.5}}};
using test_planar_gallery::read;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline void verify_sources(const std::filesystem::path& root) {
    for (unsigned i = 0; i < mesh_paths.size(); ++i) {
        const auto source = read(root / mesh_paths[i]);
        check(EditableMeshSource::create_planar_with_holes(source.at("asset_id"), outlines[i],
                                                           holes[i])
                      .document == source,
              "Hole saved source differs from its original factory intent");
    }
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Hole Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
    std::array<AssetId, 3> meshes;
    for (unsigned i = 0; i < outlines.size(); ++i) {
        auto source = EditableMeshSource::create_planar_with_holes(AssetId::generate(), outlines[i],
                                                                   holes[i]);
        check(!std::filesystem::exists(root / mesh_paths[i]), "Hole Mesh source already exists");
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
    create("Orthographic cutout shapes camera", "render.camera",
           {{"projection", 1}, {"orthographic_height", 7}}, 0, 0, -8);
    const std::array<const char*, 3> names{"Single opening frame", "Three windows - mixed winding",
                                           "Concave L with cutout"};
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
} // namespace forge::test_mesh_holes_gallery
