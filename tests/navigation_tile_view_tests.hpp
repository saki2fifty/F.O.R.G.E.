#pragma once
#include "editor/framing.hpp"
#include "tile_map_cook.hpp"
#include "tile_set_cook.hpp"
namespace {
void check_tile_navigation_views(Scene& scene, const std::string& actor, float scale) {
    auto set = TileSetSource::create(AssetId::generate());
    set.document["next_id"] = 3;
    set.document["images"] = Json::array({{{"id", 1},
                                           {"name", "Cells"},
                                           {"material", AssetId::generate()},
                                           {"grid",
                                            {{"columns", 1},
                                             {"rows", 1},
                                             {"margin_x", 0},
                                             {"margin_y", 0},
                                             {"spacing_x", 0},
                                             {"spacing_y", 0}}}}});
    set.document["tiles"] =
        Json::array({{{"id", 2}, {"name", "Solid"}, {"image", 1}, {"frame", 0}}});
    auto source = TileMapValue::create(AssetId::generate(), set.asset());
    const std::array<TileMapCellEdit, 2> writes{{{1, {-4, -1}, {2, 0}}, {1, {3, -1}, {2, 0}}}};
    source = source.paint(writes);
    ui::PublishedTileMapView published;
    published.cells = std::make_shared<const TileMapData>(
        decode_cooked_tile_map(cook_tile_map(source), source.asset()));
    published.definitions = std::make_shared<const TileSetData>(
        decode_cooked_tile_set(cook_tile_set(set), set.asset()));
    published.revision = "first";
    const auto entity = scene.entity(actor);
    TileMapRenderer renderer;
    renderer.map = {source.asset()};
    entity.set<TileMapRenderer>(renderer);
    entity.set<TileMapCollider2D>({1});
    auto region = entity.get<NavigationRegion2D>();
    region.source = 1;
    entity.set<NavigationRegion2D>(region);
    const auto before = scene.document();
    const auto effective = scene.effective_document();
    const ui::PublishedTileMapReader read = [&](AssetRef<TileMapAsset> ref) {
        check(ref.id == source.asset(), "Navigation guide requested a different Map");
        return published;
    };
    ui::SpatialHelpers helpers;
    helpers.update(effective, 7, 1000, 800, read);
    check(helpers.markers().empty(), "Loading Map invented a collision footprint");
    published.current = true;
    helpers.update(effective, 7, 1000, 800, read);
    check(helpers.markers().size() == 1 && helpers.markers()[0].lines.size() == 8,
          "Ready Map did not invalidate guides or retain its separate islands");
    const auto& marker = helpers.markers()[0];
    for (const auto& [a, b] : marker.lines)
        check(std::hypot(a[0] - b[0], a[1] - b[1]) < 1.01,
              "Navigation guide connected disconnected Map islands");
    EditorCamera camera;
    camera.align(2, -1);
    camera.target = {0, 0, 0};
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 12;
    camera.distance = 20;
    const auto screen = [&](Double3 p) {
        const auto q = project_point(camera, {float(p[0]), float(p[1]), float(p[2])}, 1000, 800);
        check(bool(q), "Map island test was outside the view");
        return ImVec2{(*q)[0], (*q)[1]};
    };
    const auto edge = marker.lines.front();
    const Double3 midpoint{(edge.first[0] + edge.second[0]) * .5,
                           (edge.first[1] + edge.second[1]) * .5, 0};
    check(helpers.pick(camera, {1000, 800}, screen(midpoint), actor) == actor &&
              helpers.pick(camera, {1000, 800}, screen({0, -.5, 0}), actor).empty(),
          "Map outline picking selected a bridge or missed an island");
    const NavigationTileContours contours = [&](const auto& collider, const auto& transform,
                                                bool obstacle) {
        return navigation_detail::tile_collider_outlines(*published.cells, *published.definitions,
                                                         published.variants.get(), collider.layer,
                                                         transform, obstacle);
    };
    const auto view = navigation_region_view(effective, actor, contours);
    check(view && view->contours.size() == 2 && std::abs(view->center()[0]) < .001,
          "Map view used the entity origin or flattened contours");
    RenderBounds empty{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
    const auto bounds =
        include_selected_helper_positions(effective, {EntityId::parse(actor)}, {}, empty, contours);
    check(bounds.minimum[0] < -3.99 && bounds.maximum[0] > 3.99 &&
              camera.frame(effective, actor, 1.25f, contours) && std::abs(camera.target[0]) < .001,
          "Map framing lost its complete collision extent");
    NavigationRegionGesture gesture;
    check(!gesture.begin(scene, actor, 0, 1), "Rectangle handles edited a Map-derived region");
    published.current = false;
    helpers.update(effective, 7, 1000, 800, read);
    check(helpers.markers().empty(), "Stale Map retained an authoritative collision guide");
    published.current = true;
    published.revision = "second";
    const std::array<TileMapCellEdit, 1> erase{{{1, {3, -1}, {}}}};
    published.cells = std::make_shared<const TileMapData>(
        published.cells->with_cells(erase, *published.definitions));
    helpers.update(effective, 7, 1000, 800, read);
    check(helpers.markers().size() == 1 && helpers.markers()[0].lines.size() == 4 &&
              helpers.markers()[0].position[0] < -3 && scene.document() == before,
          "Changed Map resource failed to refresh its derived guides or changed authoring");
    (void)scale; // The caller's real interface scale also governs picking tolerance.
}
} // namespace
