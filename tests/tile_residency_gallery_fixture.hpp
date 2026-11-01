#pragma once
#include "runtime_tile_map.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include "transform_animation_authoring.hpp"
#include <forge/runtime_world.hpp>
#include <thread>

namespace forge::test_tile_residency {
using test_tile_asset_io::check;
inline constexpr auto source = "Scenes/126-tile-chunk-residency.scene.json";
inline auto prepare(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                    const std::filesystem::path& worker) {
    auto result = test_tile_gallery_scene::prepare(gallery, scratch, worker, source);
    const auto document =
        Json::parse(asset_detail::read_bytes(result.root / source, 8 * 1024 * 1024));
    const auto catalog = AssetCatalog::open_project(result.root);
    auto lease = std::make_shared<ProjectLease>(result.root);
    for (const auto& row : document.at("entities")) {
        const auto& components = row.at("components");
        if (!components.contains("forge.transform_animator"))
            continue;
        const auto id = components.at("forge.transform_animator").at("clip").get<AssetId>();
        const auto& record = catalog.records().at(id);
        AssetImportService imports(lease, transform_animation_import_registry(),
                                   desktop_texture_target());
        imports.submit(
            imports.prepare(record.source, {}, id),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(30)), "Residency camera Clip import stalled");
        const auto outcomes = imports.poll();
        check(outcomes.size() == 1 && outcomes.front().published,
              "Residency camera Clip publication failed" +
                  (outcomes.empty() ? std::string{} : ": " + outcomes.front().diagnostic));
    }
    return result;
}
inline void ready(RuntimeWorld& world) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Residency Gallery resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
inline RenderScene sample(RuntimeWorld& world, unsigned tick) {
    auto frame = extract_render_scene(world.simulation.presentation(1));
    detail::bind_runtime_tile_maps(world.engine.world(), frame);
    check(frame.diagnostics.empty() && frame.tile_maps.size() == 1 && frame.cameras.size() == 1,
          "Residency Gallery lost its Map/camera composition");
    const auto region = tick == 1200 ? 0 : tick / 240;
    const auto& camera = frame.cameras.front().world;
    const auto& map = frame.tile_maps.front();
    check(region < 5 && std::abs(camera.m[3] - (7.5 + region * 64)) < .002 &&
              std::abs(camera.m[7]) < .002 && std::abs(camera.m[11] + 12) < .002,
          "Residency Gallery camera Clip did not visit the authored regions");
    check(map.cells && map.definitions && map.cells->occupied() == 322 &&
              map.cells->chunk_count() == 10 && map.cells->cell(1, {256, -4}).tile == 3 &&
              !map.cells->cell(1, {32, 0}).tile,
          "Camera movement changed complete offscreen logical cells");
    return frame;
}
} // namespace forge::test_tile_residency
