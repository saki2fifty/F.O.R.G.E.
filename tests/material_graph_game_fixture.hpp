#pragma once
#include "asset_import_service.hpp"
#include "material_authoring.hpp"
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#include <forge/material_graph.hpp>
namespace forge::test {
inline void add_graph_game_material(const std::filesystem::path& project,
                                    const std::filesystem::path& worker) {
    std::filesystem::create_directories(project / "Assets");
    auto source = MaterialGraphSource::create(AssetId::generate());
    source.document["graph"]["nodes"][0]["data"]["value"] = {.07, .55, .2, 1.};
    atomic_write(project / "Assets/Surface.shader.json", source.document.dump(2));
    auto catalog = AssetCatalog::open_project(project);
    catalog.add({source.asset(), "shader", "Assets/Surface.shader.json"});
    catalog.save(AssetCatalog::project_index(project));
    auto lease = std::make_shared<ProjectLease>(project);
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(
        asset_detail::shader_importer(worker, {asset_detail::diligent_shader_compiler_digest(),
                                               asset_detail::diligent_shader_compiler_debug()}));
    registry->seal();
    AssetImportService shaders(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"});
    // These are new assets with no live consumers yet. Preparation validates
    // their cooked content; there is no active binding compatibility to check.
    const auto no_live_consumers = [](const auto&, const auto&) {};
    shaders.submit(
        shaders.prepare("Assets/Surface.shader.json"),
        [](auto& c, const auto& p, const auto&) { prepare_shader_publication(c, p); },
        no_live_consumers);
    if (!shaders.wait_idle(std::chrono::seconds(65)))
        throw std::runtime_error("Graph game Shader compile timed out");
    const auto shader_result = shaders.poll();
    if (shader_result.size() != 1 || !shader_result[0].published)
        throw std::runtime_error(
            "Graph game Shader failed: " +
            (shader_result.empty() ? std::string("no result") : shader_result[0].diagnostic));
    auto material = MaterialSource::create(AssetId::generate());
    material.document["version"] = 2;
    material.document["overrides"]["model"] = surface_material_model;
    material.document["overrides"]["shader"] = source.asset();
    atomic_write(project / "Assets/Surface.material.json", material.document.dump(2));
    catalog = AssetCatalog::open_project(project);
    catalog.add({material.asset(), "material", "Assets/Surface.material.json"});
    catalog.save(AssetCatalog::project_index(project));
    AssetImportService materials(lease, material_import_registry(),
                                 {"windows", "d3d12", "desktop"});
    materials.submit(
        materials.prepare("Assets/Surface.material.json"),
        [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
        no_live_consumers);
    if (!materials.wait_idle(std::chrono::seconds(10)))
        throw std::runtime_error("Graph game material import timed out");
    const auto material_result = materials.poll();
    if (material_result.size() != 1 || !material_result[0].published)
        throw std::runtime_error(
            "Graph game material failed: " +
            (material_result.empty() ? std::string("no result") : material_result[0].diagnostic));
    WorldContext world;
    Scene scene(world);
    scene.load(project / "main.scene.json");
    scene.entity("cube").set<MeshRenderer>(
        {engine_primitive(0), {{"surface", {material.asset()}}}});
    scene.save(project / "main.scene.json");
}
} // namespace forge::test
