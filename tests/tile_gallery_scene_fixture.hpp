#pragma once
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "texture_authoring.hpp"
#include "tile_asset_fixture.hpp"
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <set>

namespace forge::test_tile_gallery_scene {
using test_tile_asset_io::check;
struct Prepared {
    std::filesystem::path root;
    AssetId scene;
};
// Only this Scene's published asset closure is needed. Keep unrelated Gallery
// imports out of this regression, including when the consumer is instrumented.
inline Prepared prepare(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                        const std::filesystem::path& worker, const char* source) {
    Prepared result{scratch / AssetId::generate().str() / "Project", {}};
    std::filesystem::create_directories(result.root.parent_path());
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    const auto bytes = asset_detail::read_bytes(result.root / source, 8 * 1024 * 1024);
    const auto document = Json::parse(bytes);
    result.scene = document.at("asset_id").get<AssetId>();
    const auto catalog = AssetCatalog::open_project(result.root);
    auto lease = std::make_shared<ProjectLease>(result.root);
    const auto target = desktop_texture_target();
    std::set<AssetId> prepared;
    const auto publish = [&](auto&& self, AssetId id) -> void {
        if (engine_texture_asset(id) || !prepared.insert(id).second)
            return;
        const auto& record = catalog.records().at(id);
        for (const auto& edge : record.dependency_edges)
            self(self, edge.target);
        const bool texture = record.type == TextureAsset::type;
        const bool material = record.type == MaterialAsset::type;
        const bool map = record.type == TileMapAsset::type;
        check(texture || material || map || record.type == TileSetAsset::type,
              "Unexpected tile Gallery dependency type");
        auto registry = texture    ? texture_import_registry(worker)
                        : material ? material_import_registry()
                                   : tile_asset_import_registry(worker);
        AssetImportService imports(lease, std::move(registry), target);
        imports.submit(
            imports.prepare(record.source, {}, id),
            [=](auto& candidate, const auto& plan, const auto&) {
                if (texture)
                    prepare_texture_publication(candidate, plan);
                else if (material)
                    prepare_material_publication(candidate, plan);
                else if (map)
                    prepare_tile_map_publication(candidate, plan);
                else
                    prepare_tile_set_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(30)), "Tile Gallery import stalled");
        const auto outcomes = imports.poll();
        check(outcomes.size() == 1 && outcomes[0].published,
              "Tile Gallery publication failed" +
                  (outcomes.empty() ? std::string{} : ": " + outcomes[0].diagnostic));
    };
    for (const auto& row : document.at("entities")) {
        const auto& components = row.at("components");
        if (components.contains("forge.tile_map_renderer"))
            publish(publish, components.at("forge.tile_map_renderer").at("map").get<AssetId>());
        if (components.contains("forge.sprite_renderer"))
            publish(publish, components.at("forge.sprite_renderer").at("material").get<AssetId>());
    }
    check(asset_detail::read_bytes(result.root / source, 8 * 1024 * 1024) == bytes,
          "Tile Gallery preparation changed the Scene");
    return result;
}
} // namespace forge::test_tile_gallery_scene
