#pragma once
#include "editor/spatial_helpers.hpp"
inline void check_region_helpers(forge::Scene& scene, const std::string& region, float scale) {
    using namespace forge;
    const auto original = scene.document();
    auto source = scene.effective_document();
    const auto row =
        std::find_if(source.at("entities").begin(), source.at("entities").end(),
                     [&](const auto& candidate) { return candidate.at("id") == region; });
    check(row != source.at("entities").end(), "Region helper fixture entity is missing");
    const auto view = navigation_region_view(*row);
    check(bool(view), "Valid effective region has no detached footprint");
    EditorCamera camera;
    camera.align(2, -1);
    camera.target = {0, 0, 0};
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 20;
    camera.distance = 20;
    const ImVec2 area{1000, 800};
    const auto point = [&](Double3 value) {
        const auto projected = project_point(
            camera, {float(value[0]), float(value[1]), float(value[2])}, area.x, area.y);
        check(bool(projected), "Region test point was outside the view");
        return ImVec2{(*projected)[0], (*projected)[1]};
    };
    const auto edge = point({(view->quad[0][0] + view->quad[1][0]) * .5,
                             (view->quad[0][1] + view->quad[1][1]) * .5, 0});
    ui::SpatialHelpers helpers;
    helpers.visible = false; // The separate camera/light toggle does not hide navigation.
    std::uint64_t generation = 1;
    helpers.update(source, generation++, 1000, 800);
    check(helpers.markers().size() == 1 && helpers.markers()[0].navigation,
          "Region did not reuse the existing helper cache");
    check(helpers.pick(camera, area, edge).empty() &&
              helpers.pick(camera, area, edge, region) == region &&
              helpers.pick(camera, area, edge, {}, true) == region,
          "Region outline picking disagrees with selected/overlay visibility");
    check(helpers.pick(camera, area, point(view->center()), {}, true).empty(),
          "A filled navigation region stole ordinary object selection");
    auto close = edge;
    close.x += 3 * scale; // This edge is horizontal; offset its vertical screen coordinate instead.
    close.y += 3 * scale;
    check(helpers.pick(camera, area, close, {}, true) == region,
          "Region outline hit tolerance did not scale with the UI");
    close.y = edge.y + 8 * scale;
    check(helpers.pick(camera, area, close, {}, true).empty(),
          "Region edge picking exceeded its scaled tolerance");
    row->at("components")["forge.node_selectability"] = {{"selectable", false}};
    helpers.update(source, generation++, 1000, 800);
    check(helpers.markers().size() == 1 && helpers.pick(camera, area, edge, {}, true).empty(),
          "Locked region outline remained selectable");
    row->at("components").erase("forge.node_selectability");
    row->at("components")["forge.node_visibility"] = {{"visible", false}};
    helpers.update(source, generation++, 1000, 800);
    check(helpers.markers().empty(), "Hidden region retained an authoring outline");
    row->at("components").erase("forge.node_visibility");
    row->at("components")["forge.navigation_region_2d"]["enabled"] = false;
    helpers.update(source, generation++, 1000, 800);
    check(helpers.markers().size() == 1 && !helpers.markers()[0].enabled &&
              helpers.pick(camera, area, edge, {}, true) == region,
          "Disabled region lost its muted editable outline");
    row->at("components")["forge.navigation_region_2d"]["width"] = 0;
    helpers.update(source, generation++, 1000, 800);
    check(helpers.markers().empty(), "Invalid region invented substitute guide geometry");
    auto overlapping = scene.effective_document();
    auto duplicate = overlapping.at("entities").front();
    const auto duplicate_id = EntityId::generate().str();
    duplicate["id"] = duplicate_id;
    overlapping["entities"].push_back(duplicate);
    const auto expected = std::min(region, duplicate_id);
    helpers.update(overlapping, generation++, 1000, 800);
    check(helpers.pick(camera, area, edge, {}, true) == expected,
          "Overlapping region edges did not choose a stable identity");
    std::reverse(overlapping["entities"].begin(), overlapping["entities"].end());
    helpers.update(overlapping, generation++, 1000, 800);
    check(helpers.pick(camera, area, edge, {}, true) == expected,
          "Region edge tie selection changed with Scene row order");
    const auto camera_id = EntityId::generate().str();
    auto icon = overlapping["entities"].front();
    icon["id"] = camera_id;
    icon["components"] = nlohmann::json::object();
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == "forge.camera")
            icon["components"][type.name] = type.defaults;
    const auto edge_world = Double3{(view->quad[0][0] + view->quad[1][0]) * .5,
                                    (view->quad[0][1] + view->quad[1][1]) * .5, 0};
    icon["world_affine"] = affine_transform({{edge_world[0], edge_world[1], 0}, {}, {1, 1, 1}}).m;
    overlapping["entities"].push_back(icon);
    helpers.visible = true;
    helpers.update(overlapping, generation++, 1000, 800);
    check(helpers.pick(camera, area, edge, {}, true) == camera_id,
          "Region outline stole input from an existing camera helper icon");
    EditorCamera perspective;
    perspective.target = {0, 0, 0};
    perspective.yaw = 0;
    perspective.pitch = 0;
    perspective.distance = 1;
    check(bool(ui::SpatialHelpers::project_line(perspective, area, {-.1, 0, -.99}, {.1, 0, 0})) &&
              !ui::SpatialHelpers::project_line(perspective, area, {-.1, 0, -.99}, {.1, 0, -.99}),
          "Drawing and picking disagree on near-clipped helper lines");
    auto framed = source;
    auto& framed_row =
        *std::find_if(framed["entities"].begin(), framed["entities"].end(),
                      [&](const auto& candidate) { return candidate.at("id") == region; });
    framed_row["components"]["forge.navigation_region_2d"] = {
        {"enabled", true}, {"obstacle", false}, {"width", 20}, {"height", 6}};
    framed_row["world_affine"] = std::array<double, 12>{2, 0, 0, 100, 0, 1, 0, 0, 0, 0, 1, 37};
    check(camera.frame(framed, region, area.x / area.y) && camera.target[0] == 100 &&
              camera.target[2] == 0 && camera.orthographic_height > 40,
          "Framing a region without art lost its extent or used authored visual depth");
    check(scene.document() == original, "Detached region picking changed authored state");
}
