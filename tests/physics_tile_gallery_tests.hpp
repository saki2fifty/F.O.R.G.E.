#pragma once
#include "runtime_tile_map.hpp"
#include <forge/game_content.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_world.hpp>

inline void test_painted_collision_gallery(const std::filesystem::path& root,
                                           forge::AssetId scene_id, forge::AssetId map_id) {
    using namespace forge;
    using namespace forge::detail;
    const auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    const auto saved_source = load_game_scene(root, {scene_id});
    world.scene.restore_snapshot(saved_source);
    const auto authored = world.scene.document();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        require(std::chrono::steady_clock::now() < deadline, "Painted Gallery preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef floor, amber;
    std::vector<std::pair<EntityRef, double>> actors;
    for (const auto& row : authored.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        const auto ref = world.scene.reference(id);
        const auto entity = world.scene.entity(id);
        if (entity.has<TileMapCollider2D>()) {
            floor = ref;
            require(entity.get<TileMapCollider2D>().layer == 1 &&
                        entity.get<TileMapRenderer>().map.id == map_id,
                    "Gallery floor lost its selected Map/layer");
        }
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Amber")) {
            amber = ref;
            actors.emplace_back(ref, 1.45);
        } else if (name.starts_with("Cyan"))
            actors.emplace_back(ref, 2.4375);
        else if (name.starts_with("Rose"))
            actors.emplace_back(ref, -2.55);
        else if (name.starts_with("Green"))
            actors.emplace_back(ref, -2.5625);
    }
    require(bool(floor.entity) && bool(amber.entity) && actors.size() == 4 &&
                world.physics()->status().at("bodies_2d") == 5 &&
                world.physics()->status().at("shapes_2d") == 8,
            "Gallery did not use one painted compound floor and four dynamic actors");
    const auto cells = runtime_tile_map_snapshot(world.engine.world(), floor);
    require(cells->cells.occupied() == 23 && cells->cells.layers().size() == 2,
            "Gallery painted floors/decorative cells differ from the experiment");
    for (unsigned tick = 0; tick < 360; ++tick)
        world.simulation.tick(1.f / 60);
    for (const auto& [actor, expected] : actors) {
        const auto state = world.physics()->body_state_2d(actor);
        require(state && std::abs(state->position[1] - expected) < .025,
                "Gallery actor did not land on its intended painted surface");
    }
    const auto ray = world.physics()->raycast_2d({-5.5, 2}, {0, -8});
    require(ray && ray->entity == floor && std::abs(ray->fraction - .625) < .001,
            "Decorative painted arrows unexpectedly blocked the ordinary physics ray");
    const std::array<TileMapCellEdit, 3> erase{
        {{1, {-5, 0}, {}}, {1, {-4, 0}, {}}, {1, {-3, 0}, {}}}};
    const auto token = request_runtime_tile_cells(world.engine.world(), "project.painted_gallery",
                                                  floor, cells->generation, erase);
    (void)world.simulation.presentation(0);
    require(
        inspect_runtime_tile_edit(world.engine.world(), "project.painted_gallery", token).state ==
            RuntimeTileEditState::Pending,
        "Gallery removal changed cells while paused");
    world.simulation.tick(1.f / 60);
    require(
        inspect_runtime_tile_edit(world.engine.world(), "project.painted_gallery", token).state ==
            RuntimeTileEditState::Applied,
        "Gallery floor removal was not admitted on the fixed step");
    release_runtime_tile_edit(world.engine.world(), "project.painted_gallery", token);
    for (unsigned tick = 0; tick < 120; ++tick)
        world.simulation.tick(1.f / 60);
    require(std::abs(world.physics()->body_state_2d(amber)->position[1] + 2.55) < .025 &&
                cells->cells.cell(1, {-4, 0}).tile == 2,
            "Removing the painted platform failed to release the actor or changed a retained view");
    const auto reset = request_runtime_tile_reset(world.engine.world(), "project.painted_gallery",
                                                  floor, cells->generation);
    world.simulation.tick(1.f / 60);
    require(
        inspect_runtime_tile_edit(world.engine.world(), "project.painted_gallery", reset).state ==
            RuntimeTileEditState::Applied,
        "Gallery reset did not apply");
    require(runtime_tile_map_snapshot(world.engine.world(), floor)->cells.cell(1, {-4, 0}).tile ==
                2,
            "Gallery reset did not restore its published platform");
    require(load_game_scene(root, {scene_id}) == saved_source,
            "Gallery simulation wrote runtime transforms or cells into the saved Scene");
    release_runtime_tile_edit(world.engine.world(), "project.painted_gallery", reset);
}
