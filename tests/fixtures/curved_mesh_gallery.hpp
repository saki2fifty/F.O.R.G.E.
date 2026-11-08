#pragma once
#include "planar_mesh_gallery.hpp"
#include <forge/scene_identity.hpp>

namespace forge::test_curved_mesh_gallery {
inline constexpr auto scene_path = "Scenes/204-curved-planar-meshes.scene.json";
inline constexpr std::array<const char*, 3> mesh_paths{"Assets/Planar/Ellipse.mesh.json",
                                                       "Assets/Planar/CircleRing.mesh.json",
                                                       "Assets/Planar/CubicOutline.mesh.json"};
using Loop = std::vector<std::array<double, 2>>;
inline constexpr double handle = .5522847498307936;
inline const std::vector<MeshCurveKnot> knots{{{1.2, 0}, {0, -.6}, {0, .6}},
                                              {{0, 1}, {handle, 0}, {-handle, 0}},
                                              {{-1, 0}, {0, handle}, {0, -handle}}};
inline constexpr std::array<std::array<double, 2>, 3> places{{{-4.25, 0}, {0, 0}, {4.5, 0}}};
inline EditableMeshSource source_for(unsigned shape, AssetId id) {
    if (shape == 0)
        return EditableMeshSource::create_planar_ellipse(id, {1.8, 1}, 32);
    if (shape == 1)
        return EditableMeshSource::create_planar_ellipse(id, {1.5, 1.5}, 32, .65);
    if (shape == 2)
        return EditableMeshSource::create_planar_curve(id, knots, .01);
    throw std::runtime_error("Unknown curved Gallery shape");
}
inline Loop reference_curve() {
    // Independent dense Bernstein curve; never use the production tessellator.
    Loop points;
    for (unsigned i = 0; i < knots.size(); ++i)
        for (unsigned sample = 0; sample < 128; ++sample) {
            const auto& a = knots[i];
            const auto& b = knots[(i + 1) % knots.size()];
            const double t = sample / 128., u = 1 - t;
            std::array<double, 2> p{};
            for (unsigned axis = 0; axis < 2; ++axis)
                p[axis] =
                    u * u * u * a.point[axis] + 3 * u * u * t * (a.point[axis] + a.outgoing[axis]) +
                    3 * u * t * t * (b.point[axis] + b.incoming[axis]) + t * t * t * b.point[axis];
            points.push_back(p);
        }
    return points;
}
using test_planar_gallery::read;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline void verify_sources(const std::filesystem::path& root) {
    for (unsigned i = 0; i < mesh_paths.size(); ++i) {
        const auto source = read(root / mesh_paths[i]);
        check(source_for(i, source.at("asset_id")).document == source,
              "Curved saved source differs from its original factory intent");
    }
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Curved Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
    std::array<AssetId, 3> meshes;
    for (unsigned i = 0; i < mesh_paths.size(); ++i) {
        auto source = source_for(i, AssetId::generate());
        check(!std::filesystem::exists(root / mesh_paths[i]), "Curved Mesh source already exists");
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
    create("Orthographic curved shapes camera", "render.camera",
           {{"projection", 1}, {"orthographic_height", 7}}, 0, 0, -8);
    const std::array<const char*, 3> names{"Sampled ellipse", "Circle ring",
                                           "Cubic rounded triangle"};
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
} // namespace forge::test_curved_mesh_gallery
