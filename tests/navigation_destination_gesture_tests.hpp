#pragma once
#include "editor/navigation_destination_gesture.hpp"
#include "editor/projected_axis_drag.hpp"
#include <forge/prefab_authoring.hpp>
#include <limits>
inline void check_destination_gestures(const std::filesystem::path& project) {
    using namespace forge;
    for (const auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
        EngineContext engine;
        Scene scene(engine.world());
        const auto parent = authoring_command(scene, "entity.create", {{"kind", 4}})
                                .at("selected")
                                .get<std::string>();
        const auto actor = authoring_command(scene, "entity.create", {{"kind", 4}})
                               .at("selected")
                               .get<std::string>();
        scene.entity(parent).set<LocalTranslation>({4, -3, 5});
        scene.entity(parent).set<LocalScale>({-2, 3, 0});
        scene.entity(parent).set<LocalRotation>({0, 0, .38268343f, .92387953f});
        authoring_command(scene, "entity.reparent",
                          {{"entity", actor}, {"parent", parent}, {"mode", "keep_local"}});
        NavigationAgent agent;
        agent.navmesh = {AssetId::generate()};
        agent.has_destination = true;
        agent.destination_x = 3;
        agent.destination_y = 2;
        agent.destination_z = 99;
        scene.entity(actor).set<NavigationAgent>(agent);
        const auto view = [&] {
            return navigation_destination_view(scene.effective_document(), actor, agent.navmesh.id,
                                               plane, project);
        };
        check(bool(view()), "World destination under a singular/mirrored parent was not viewable");
        NavigationDestinationGesture gesture;
        const auto before = scene.document();
        for (int axis : {-1, 0, 1}) {
            const auto revision = scene.revision();
            const auto current = *view();
            check(gesture.begin(scene, current, axis) && gesture.update({1.2, -.4, .9}, false, 1),
                  "Valid destination property gesture rejected");
            check(scene.document() == before && scene.revision() == revision,
                  "Destination preview mutated authoritative Scene/history");
            const auto shown = scene.preview_document(gesture.preview(before));
            const auto preview =
                navigation_destination_view(shown, actor, agent.navmesh.id, plane, project);
            check(preview && preview->origin == current.origin &&
                      preview->agent.destination_z ==
                          (plane == NavigationPlane::PlanarXY || axis == 0 ? 99. : 99.9) &&
                      preview->agent.destination_y ==
                          (plane == NavigationPlane::Spatial3D || axis == 0 ? 2. : 1.6),
                  "Destination preview changed transforms or the preserved coordinate");
            const auto pending = gesture.preview(before);
            check(!gesture.update({std::numeric_limits<double>::infinity(), 0, 0}, false, 1) &&
                      !gesture.update({0, 0, 0}, true, 0) && gesture.preview(before) == pending,
                  "Invalid destination update lost last valid detached preview");
            check(gesture.commit(scene), "Changed destination did not commit");
            const auto committed = scene.document();
            check(committed != before && scene.undo() && scene.document() == before &&
                      scene.redo() && scene.document() == committed && scene.undo(),
                  "Destination property gesture was not one complete Undo/Redo step");
            // Undo changes the Scene revision; the next begin must capture it.
            if (axis != 1) {
                const auto next = scene.revision();
                check(gesture.begin(scene, *view(), -1) && gesture.update({0, 0, 0}, false, 1) &&
                          !gesture.commit(scene) && scene.revision() == next,
                      "No-op destination gesture created history");
            }
        }
        check(gesture.begin(scene, *view(), -1) && gesture.update({.6, .6, .6}, true, 1),
              "World coordinate destination snap rejected");
        const auto snapped = gesture.target().agent;
        check(snapped.destination_x == 4 &&
                  snapped.destination_y == (plane == NavigationPlane::PlanarXY ? 3 : 2) &&
                  snapped.destination_z == (plane == NavigationPlane::Spatial3D ? 100 : 99),
              "Destination snapping changed the wrong plane or used parent-local values");
        auto other = *view();
        other.project /= "copied project";
        check(!gesture.matches(other),
              "Copied project scope matched an active destination gesture");
        other = *view();
        other.plane = plane == NavigationPlane::PlanarXY ? NavigationPlane::Spatial3D
                                                         : NavigationPlane::PlanarXY;
        check(!gesture.matches(other),
              "Changed navigation plane matched an active destination gesture");
        gesture.cancel();
        check(scene.document() == before, "Cancelled destination changed source");
        check(gesture.begin(scene, *view(), -1), "Stale destination fixture did not begin");
        authoring_command(scene, "entity.rename", {{"entity", actor}, {"name", "External change"}});
        const auto changed = scene.document();
        bool rejected = false;
        try {
            gesture.commit(scene);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && !gesture.active() && scene.document() == changed,
              "Stale destination commit changed another edit");

        const auto prefab = create_prefab_source(scene, actor);
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance = instantiate_prefab(scene, prefab.asset());
        const auto inherited = scene.snapshot();
        const auto inherited_view = navigation_destination_view(
            scene.effective_document(), instance, agent.navmesh.id, plane, project);
        check(inherited_view && !scene.entity(instance).owns<NavigationAgent>() &&
                  gesture.begin(scene, *inherited_view, 0) &&
                  gesture.update({.25, 0, 0}, false, 1) && gesture.commit(scene),
              "Inherited navigation destination could not be manipulated");
        const auto overridden = scene.snapshot();
        for (const auto& row : overridden.at("entities"))
            if (row.at("id") == instance) {
                const auto& mask = row.at("property_overrides").at("forge.navigation_agent");
                check(mask.size() == 1 && mask.contains("destination_x"),
                      "Destination axis authored unrelated Prefab field overrides");
            }
        check(scene.prefab_sources().at(prefab.asset()) == prefab.source && scene.undo() &&
                  scene.snapshot() == inherited &&
                  !scene.entity(instance).owns<NavigationAgent>() && scene.redo() &&
                  scene.snapshot() == overridden,
              "Destination Undo/Redo failed to preserve Prefab source/inheritance");
        authoring_command(scene, "property.revert",
                          {{"entity", instance},
                           {"component", "forge.navigation_agent"},
                           {"field", "destination_x"}});
        check(!scene.entity(instance).owns<NavigationAgent>() &&
                  scene.entity(instance).get<NavigationAgent>() == inherited_view->agent,
              "Destination property Revert did not restore inherited values");

        EditorCamera camera;
        if (plane == NavigationPlane::PlanarXY)
            camera.align(2, 1);
        else
            camera.pitch = -.7f;
        camera.distance = 20;
        const Double3 center{0, 0, 0};
        const Double3 delta =
            plane == NavigationPlane::PlanarXY ? Double3{2, -1, 0} : Double3{2, 0, -1};
        const auto first = project_point(camera, {0, 0, 0}, 1200, 900);
        const auto last =
            project_point(camera, {float(delta[0]), float(delta[1]), float(delta[2])}, 1200, 900);
        check(first && last, "Plane drag fixture is off camera");
        const auto projected = projected_plane_drag(
            camera, center,
            plane == NavigationPlane::PlanarXY ? Double3{0, 0, 1} : Double3{0, 1, 0},
            {(*first)[0], (*first)[1]}, {(*last)[0], (*last)[1]}, 1200, 900);
        check(projected && std::hypot((*projected)[0] - delta[0], (*projected)[1] - delta[1],
                                      (*projected)[2] - delta[2]) < 1e-4,
              "Projected plane drag failed a known world displacement");
    }
}
