#pragma once
#include "editor/navigation_region_gesture.hpp"
#include "editor/projected_axis_drag.hpp"
#include <forge/prefab_authoring.hpp>
inline void check_region_gestures(const std::filesystem::path& root) {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const auto id = authoring_command(scene, "entity.create", {{"name", "Editable region"}})
                        .at("selected")
                        .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", id}, {"component", "forge.navigation_region_2d"}});
    scene.entity(id).set<NavigationRegion2D>({true, false, 20, 6});
    scene.entity(id).set<LocalScale>({-2, 3, 0});
    scene.entity(id).set<LocalRotation>({0, 0, .38268343f, .92387953f});
    scene.entity(id).set<LocalTranslation>({4, -3, 37});
    NavigationRegionGesture gesture;
    const auto initial = scene.snapshot();
    for (unsigned dimension : {0u, 1u})
        for (int side : {-1, 1}) {
            const auto before = scene.snapshot();
            const auto revision = scene.revision();
            const auto value = scene.entity(id).get<NavigationRegion2D>();
            check(gesture.begin(scene, id, dimension, side) && gesture.update(1.25, false, 1),
                  "Valid mirrored/rotated/zero-depth region resize was rejected");
            check(scene.snapshot() == before && scene.revision() == revision,
                  "Region resize preview wrote authored state");
            const auto preview = scene.preview_document(gesture.preview(before));
            const auto shown = navigation_region_view(preview, id);
            const float expected = (dimension == 0 ? value.width : value.height) + 2.5f * side;
            check(shown &&
                      (dimension == 0 ? shown->region.width : shown->region.height) == expected,
                  "Detached resize preview lost its selected extent");
            check(gesture.commit(scene), "Changed region did not commit");
            const auto after = scene.snapshot();
            const auto changed = scene.entity(id).get<NavigationRegion2D>();
            check((dimension == 0 ? changed.width : changed.height) == expected &&
                      (dimension == 0 ? changed.height == value.height
                                      : changed.width == value.width) &&
                      scene.entity(id).get<LocalTranslation>() == LocalTranslation{4, -3, 37},
                  "Region resize changed another extent or its center/visual depth");
            check(scene.undo() && scene.snapshot() == before && scene.redo() &&
                      scene.snapshot() == after,
                  "One region resize did not round-trip through Undo/Redo");
            scene.undo();
        }
    check(scene.snapshot() == initial, "Repeated region resizes accumulated authored changes");
    const auto parent_id =
        authoring_command(scene, "entity.create", {{"kind", 4}, {"name", "Region parent"}})
            .at("selected")
            .get<std::string>();
    auto parent = scene.entity(parent_id);
    parent.set<LocalScale>({-2, 3, 1});
    parent.set<LocalRotation>({0, 0, .38268343f, .92387953f});
    parent.set<LocalTranslation>({10, 20, 30});
    scene.entity(id).set<LocalScale>({1, 1, 0});
    scene.entity(id).set<LocalRotation>({});
    scene.entity(id).set<LocalTranslation>({0, 0, 7});
    scene.entity(id).set<SpatialBinding>({SpatialMode::Explicit, scene.reference(parent_id)});
    const auto parented = scene.snapshot();
    check(gesture.begin(scene, id, 0, 1), "Valid scaled spatial parent blocked a region edit");
    const auto region_view = gesture.original();
    const auto axis = region_view.axis(0);
    auto anchor = region_view.center(), destination = anchor;
    for (unsigned i = 0; i < 3; ++i) {
        anchor[i] += 10 * axis[i];
        destination[i] = anchor[i] + .75 * axis[i];
    }
    EditorCamera camera;
    camera.align(2, -1);
    camera.target = {10, 20, 0};
    camera.distance = 60;
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 100;
    const auto pixel = [&](Double3 point) {
        return project_point(camera, {float(point[0]), float(point[1]), float(point[2])}, 1000,
                             800);
    };
    const auto first = pixel(anchor), last = pixel(destination);
    check(first && last, "Parented region handle was not projectable");
    const auto delta = projected_axis_drag(camera, anchor, axis, *first, *last, 1000, 800);
    check(delta && std::abs(*delta - .75) < .0001 && gesture.update(*delta, false, 1) &&
              gesture.commit(scene) &&
              std::abs(scene.entity(id).get<NavigationRegion2D>().width - 21.5) < .0001 &&
              parent.get<LocalScale>() == LocalScale{-2, 3, 1},
          "Projected region drag lost local units through a mirrored spatial parent");
    check(scene.undo() && scene.snapshot() == parented,
          "Parented region Undo changed its spatial owner");
    scene.restore_snapshot(initial);
    const auto neutral_revision = scene.revision();
    check(gesture.begin(scene, id, 0, 1) && gesture.update(0, false, 1) && !gesture.commit(scene) &&
              scene.revision() == neutral_revision,
          "Neutral region drag created a history entry");
    check(gesture.begin(scene, id, 0, 1) && !gesture.update(1000, false, 1) &&
              !gesture.update(-1000, false, 1),
          "Out-of-range region extent was accepted");
    gesture.cancel();
    check(scene.snapshot() == initial, "Rejected region drag changed the Scene");
    check(gesture.begin(scene, id, 0, 1) && gesture.update(.3, true, .5) && gesture.commit(scene) &&
              scene.entity(id).get<NavigationRegion2D>().width == 20.5f,
          "Local metre snapping did not round the centered extent");
    scene.undo();
    for (float boundary : {.01f, 1000.f}) {
        check(gesture.begin(scene, id, 0, 1) &&
                  gesture.update((double(boundary) - 20) / 2, false, 1) && gesture.commit(scene) &&
                  scene.entity(id).get<NavigationRegion2D>().width == boundary,
              "Reflected float extent boundary could not be authored by the gesture");
        scene.undo();
    }
    check(gesture.begin(scene, id, 0, 1) && gesture.update(1, false, 1),
          "Stale resize fixture failed");
    authoring_command(scene, "entity.rename", {{"entity", id}, {"name", "Renamed"}});
    const auto renamed = scene.snapshot();
    bool rejected = false;
    try {
        gesture.commit(scene);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && !gesture.active() && scene.snapshot() == renamed,
          "Region resize crossed a Scene revision change");
    scene.undo();
    check(gesture.begin(scene, id, 1, -1) && gesture.update(-1, false, 1), "Cancel fixture failed");
    gesture.cancel();
    check(scene.snapshot() == initial && !gesture.active(), "Cancelled resize wrote its preview");
    scene.entity(id).set<LocalScale>({0, 1, 0});
    check(!gesture.begin(scene, id, 0, 1), "Collapsed XY geometry acquired resize authority");
    scene.restore_snapshot(initial);
    scene.entity(id).set<LocalTranslation>({4080, 0, 0});
    scene.entity(id).set<LocalRotation>({});
    scene.entity(id).set<LocalScale>({1, 1, 0});
    scene.entity(id).set<NavigationRegion2D>({true, false, 10, 6});
    const auto bounded = scene.snapshot();
    check(gesture.begin(scene, id, 0, 1) && !gesture.update(6, false, 1),
          "Region resize crossed the shared world XY geometry boundary");
    gesture.cancel();
    check(scene.snapshot() == bounded, "Rejected world-bounds resize wrote authored state");
    scene.restore_snapshot(initial);
    check(gesture.begin(scene, id, 0, 1) && gesture.update(1, false, 1) && gesture.commit(scene),
          "Persistence resize fixture failed");
    const auto saved = root / "region-resize.scene.json";
    scene.save(saved);
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    reopened.load(saved);
    check(reopened.snapshot() == scene.snapshot(), "Region resize did not survive Save/reopen");
    scene.restore_snapshot(initial);
    const auto prefab = create_prefab_source(scene, id);
    scene.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    check(!scene.entity(instance).owns<NavigationRegion2D>(),
          "Region Prefab did not begin inherited");
    const auto inherited = scene.snapshot();
    check(gesture.begin(scene, instance, 0, 1) && gesture.update(1, false, 1) &&
              gesture.commit(scene) &&
              scene.entity(instance).get<NavigationRegion2D>().width == 22 &&
              scene.prefab_sources().at(prefab.asset()) == prefab.source,
          "Region resize failed to author a property override without changing its Prefab source");
    check(scene.undo() && scene.snapshot() == inherited &&
              !scene.entity(instance).owns<NavigationRegion2D>(),
          "Region resize Undo did not restore inherited component intent");
}
