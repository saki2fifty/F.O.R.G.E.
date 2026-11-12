#pragma once
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "surface_material_fixture.hpp"
#include <chrono>

namespace forge::test {
// CPU publications exercise typed authoring admission, never GPU execution.
inline Json ui_material_authoring_fixture(const std::shared_ptr<const ProjectLease>& lease) {
    const auto root = lease->root();
    std::filesystem::create_directories(root / "Assets");
    auto catalog = AssetCatalog::open_project(root);
    const auto shader = AssetId::generate();
    const auto material = [&](const char* path, bool publish) {
        auto source = MaterialSource::create(AssetId::generate());
        source.document["version"] = 2;
        source.document["overrides"]["shader"] = AssetRef<ShaderAsset>{shader};
        asset_storage::replace(root / path, source.document.dump());
        auto current = AssetCatalog::open_project(root);
        current.add({source.asset(), "material", path});
        current.save(AssetCatalog::project_index(root));
        if (publish) {
            AssetImportService service(lease, material_import_registry(),
                                       {"windows", "d3d12", "desktop"});
            service.submit(
                service.prepare(path),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_material_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            if (!service.wait_idle(std::chrono::seconds(5)))
                throw std::runtime_error("UI Material authoring fixture publication stalled");
            const auto outcomes = service.poll();
            if (outcomes.size() != 1 || !outcomes.front().published)
                throw std::runtime_error("UI Material authoring fixture failed publication");
        }
        return source.asset();
    };
    SurfaceShaderDefinition definition;
    definition.ui_effect = true;
    definition.uv_sets = {0};
    publish_surface_fixture(root, catalog, shader, definition);
    const auto ui = material("Assets/Effect.material.json", true);
    catalog = AssetCatalog::open_project(root);
    definition.ui_effect = false;
    publish_surface_fixture(root, catalog, shader, definition, 2);
    const auto mesh = material("Assets/Mesh.material.json", true);
    const auto unpublished = material("Assets/Unpublished.material.json", false);
    return {{"ui", ui}, {"mesh", mesh}, {"unpublished", unpublished}, {"shader", shader}};
}
} // namespace forge::test
