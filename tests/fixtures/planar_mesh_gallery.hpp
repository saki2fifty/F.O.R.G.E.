#pragma once
#include "asset_storage.hpp"
#include "editable_mesh_authoring.hpp"
#include "material_authoring.hpp"
#include <forge/authoring.hpp>
#include <forge/editable_mesh.hpp>
#include <fstream>

namespace forge::test_planar_gallery {
inline constexpr auto scene_path = "Scenes/199-planar-meshes.scene.json";
inline constexpr auto scene_uuid = "0390198d-7a9e-4e69-8a2a-912dbbe3d5f7";
inline constexpr std::array<const char*, 3> mesh_paths{"Assets/Planar/Quad.mesh.json",
                                                       "Assets/Planar/Polygon.mesh.json",
                                                       "Assets/Planar/Triangle.mesh.json"};
inline constexpr auto material_path = "Assets/Planar/UnlitChecker.material.json";
inline Json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Planar Gallery source missing");
    return Json::parse(input);
}
template <class Prepare>
void publish(const std::shared_ptr<ProjectLease>& lease, const std::filesystem::path& path,
             AssetId id, std::shared_ptr<const AssetImporterRegistry> registry, Prepare prepare) {
    AssetImportService imports(lease, std::move(registry), {"windows", "d3d12", "desktop"}, 1);
    auto draft = imports.prepare(path, {}, id);
    imports.submit(
        std::move(draft),
        [prepare](auto& candidate, const auto& plan, const auto&) { prepare(candidate, plan); },
        [](const auto&, const auto&) {});
    if (!imports.wait_idle(std::chrono::seconds(30)))
        throw std::runtime_error("Planar Gallery publication timed out");
    const auto done = imports.poll();
    if (done.size() != 1 || !done[0].published)
        throw std::runtime_error(done.empty() ? "Planar Gallery import receipt missing"
                                              : done[0].diagnostic);
}
inline void author(const std::filesystem::path& root) {
    if (std::filesystem::exists(root / scene_path))
        throw std::runtime_error("Planar Gallery already exists");
    std::filesystem::create_directories(root / "Assets/Planar");
    auto lease = std::make_shared<ProjectLease>(root);
    const std::array<std::array<double, 2>, 5> polygon{
        {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-2, 0}}};
    const std::array<std::array<double, 2>, 3> clockwise{{{.75, -.5}, {-.75, -.5}, {0, .75}}};
    const std::array shapes{
        EditableMeshSource::create_planar_quad(AssetId::generate(), 2, 2),
        EditableMeshSource::create_planar_polygon(AssetId::generate(), polygon),
        EditableMeshSource::create_planar_polygon(AssetId::generate(), clockwise)};
    for (unsigned i = 0; i < shapes.size(); ++i) {
        atomic_write(root / mesh_paths[i], shapes[i].document.dump(2));
        publish(lease, mesh_paths[i], shapes[i].asset(), editable_mesh_import_registry(),
                prepare_editable_mesh_publication);
    }
    auto material = MaterialSource::create(AssetId::generate());
    material.document["overrides"] = read(root / "Assets/Checker.material.json").at("overrides");
    material.document["overrides"]["model"] = "forge.gltf.unlit.v1";
    material.document["overrides"]["double_sided"] = true;
    material.validate();
    atomic_write(root / material_path, material.document.dump(2));
    publish(lease, material_path, material.asset(), material_import_registry(),
            prepare_material_publication);
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        duplicate_scene_asset(scene.document(), AssetId::parse(scene_uuid), scene.schema()));
    const auto create = [&](const char* name, const char* recipe, const Json& values, double x,
                            double y, double z) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name},
                                  {"recipe", recipe},
                                  {"component_values", values},
                                  {"position", {{"x", x}, {"y", y}, {"z", z}}}})
            .at("selected");
    };
    create("Orthographic 2D camera", "render.camera",
           {{"projection", 1}, {"orthographic_height", 6}}, 0, 0, -8);
    const std::array<const char*, 3> names{"Flat quad - 2 by 2", "Five-corner polygon",
                                           "Clockwise triangle - positive Z front"};
    const std::array<std::array<double, 2>, 3> places{{{-2, 0}, {2, 0}, {0, -1.75}}};
    for (unsigned i = 0; i < shapes.size(); ++i)
        create(names[i], "render.mesh",
               {{"mesh", shapes[i].asset()},
                {"materials", Json::array({{{"slot", "default"}, {"material", material.asset()}}})},
                {"cast_shadows", false}},
               places[i][0], places[i][1], 0);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
} // namespace forge::test_planar_gallery
