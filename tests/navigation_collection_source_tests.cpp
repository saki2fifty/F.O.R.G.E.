#include "navigation_mesh.hpp"
#include "navigation_sources.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/physics_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <tuple>

using namespace forge;
using namespace forge::navigation_detail;
namespace {
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid collected navigation source was accepted");
}
void physical_floor(bool asset_shape) {
    const auto id = EntityId::generate();
    const auto asset = AssetId::generate();
    Json components = {{"forge.physics_body", {{"enabled", true}, {"motion", 0}}},
                       {"forge.primitive", {{"kind", 1}}}};
    PhysicsDebugCollision retained;
    if (asset_shape) {
        components["forge.asset_collider"] = {{"asset", asset}};
        CollisionData data;
        CollisionNode node;
        node.id = CollisionMemberId::generate();
        node.dimensions = {12, 1, 12};
        data.nodes.push_back(node);
        auto prepared = prepare_collision_resource(std::move(data), {});
        retained = {asset, prepared.value->native};
        prepared.value.reset(); // The shared immutable native shape remains pinned.
    } else
        components["forge.box_collider"] = {{"x", 12}, {"y", 1}, {"z", 12}};
    const auto world = affine_transform({{0, -.5, 0}, {}, {1, 1, 1}});
    Json doc = {{"asset_id", AssetId::generate()},
                {"entities", Json::array({{{"id", id},
                                           {"spatial_resolved", true},
                                           {"world_affine", world.m},
                                           {"components", components}}})}};
    NavigationSettings settings;
    settings.collect_static_colliders = true;
    const auto source = capture(doc, settings);
    const auto provenance = collision_inputs(source, [&](AssetId selected) {
        require(selected == asset, "Collision selection lost the physical asset identity");
        return std::string(64, 'a');
    });
    const auto geometry = navigation_detail::geometry(
        source, Json::array(), {}, {},
        [&](AssetId selected) {
            require(selected == asset, "Triangle adapter loaded an unrelated Collision");
            return retained;
        },
        provenance);
    require(!geometry.vertices.empty(), "Physical floor has no triangles");
    Double3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
    for (std::size_t vertex = 0; vertex < geometry.vertices.size(); vertex += 3)
        for (unsigned axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], double(geometry.vertices[vertex + axis]));
            high[axis] = std::max(high[axis], double(geometry.vertices[vertex + axis]));
        }
    require(low == Double3{-6, -1, -6} && high == Double3{6, 0, 6},
            "Collision triangles do not match independently known physical Box bounds");
    const auto mesh = std::make_shared<Mesh>(build_tile(geometry, settings));
    Query query(mesh);
    const auto route = query.path({-4, 0, 0}, {4, 0, 0});
    require(route.status == NavStatus::Success && route.points.size() >= 2,
            "Physical floor did not supply an actual walkable native route");
    for (const auto& point : route.points)
        require(std::abs(point[1]) < .21,
                "Physical floor route used artwork height instead of collision height");
    const auto& shape = source.surfaces.front().collision;
    rejects([&] { (void)collision_geometry(shape, world, retained, 1); });
    if (asset_shape) {
        auto replaced = provenance;
        replaced[0]["revision"] = std::string(64, 'b');
        require(source_digest(source, Json::array(), provenance) !=
                    source_digest(source, Json::array(), replaced),
                "Collision revision replacement did not change navigation source identity");
        rejects([&] { (void)source_digest(source, Json::array()); });
        rejects([&] { (void)collision_geometry(shape, world, {}, 16384); });
    }
}
void curved_physical_shapes() {
    // Independent physical extents at a translated, uniformly scaled pose.
    const auto world = affine_transform({{5, -2, 7}, {}, {1.5f, 1.5f, 1.5f}});
    for (const auto& [key, values, extent] :
         {std::tuple{"forge.sphere_collider", Json{{"radius", 2}}, Double3{3, 3, 3}},
          std::tuple{"forge.capsule_collider", Json{{"radius", 1}, {"height", 2}},
                     Double3{1.5, 3, 1.5}},
          std::tuple{"forge.cylinder_collider", Json{{"radius", 2}, {"height", 2}},
                     Double3{3, 1.5, 3}}}) {
        const auto selected = collision_geometry(Json{{key, values}}, world, {}, 16384);
        Double3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
        require(!selected.shape.triangles.empty(), "Curved physical shape has no triangles");
        for (const auto& triangle : selected.shape.triangles)
            for (const auto& point : triangle)
                for (unsigned axis = 0; axis < 3; ++axis) {
                    low[axis] = std::min(low[axis], double(point[axis]));
                    high[axis] = std::max(high[axis], double(point[axis]));
                }
        for (unsigned axis = 0; axis < 3; ++axis)
            require(std::abs(low[axis] + extent[axis]) < .04 &&
                        std::abs(high[axis] - extent[axis]) < .04,
                    "Collected curved triangles disagree with known scaled physical extents");
        require(selected.rigid == affine_transform({{5, -2, 7}, {}, {1, 1, 1}}),
                "Physical geometry translation or scale was applied twice");
        rejects([&] { (void)collision_geometry(Json{{key, values}}, world, {}, 1); });
    }
}
void exercise(NavigationPlane plane) {
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "floor"},
                        {"name", "Floor"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}},
                       {{"id", "body"},
                        {"name", "Physical source"},
                        {"components", {{"forge.position", {{"x", 2}, {"y", 0}, {"z", 0}}}}}}})}});
    auto floor = scene.entity("floor"), body = scene.entity("body");
    floor.set<NavigationSurface>({});
    floor.set<Primitive>({0});
    floor.set<NavigationRegion2D>({true, false, 20, 20});
    if (plane == NavigationPlane::PlanarXY) {
        PhysicsBody2D config;
        config.layer = 31;
        body.set<PhysicsBody2D>(config);
        body.set<BoxCollider2D>({2, 4});
    } else {
        PhysicsBody config;
        config.layer = 31;
        body.set<PhysicsBody>(config);
        body.set<BoxCollider>({2, 3, 4});
        body.set<Primitive>({1}); // Artwork must not replace the selected physical Box.
    }
    NavigationSettings settings;
    settings.plane = plane;
    settings.collect_static_colliders = true;
    settings.collision_layers = 0x80000000u;
    auto candidates = engine.world().world().query<const PersistentEntityId>();
    const auto sources = [&] {
        const auto copied = capture(scene.effective_document(), settings);
        const auto native = capture_native(engine.world(), scene, candidates, settings);
        require(copied.identities == native.identities &&
                    source_digest(copied, Json::array()) == source_digest(native, Json::array()),
                "Native and evaluated Scene collision selection diverged");
        return copied;
    };
    const auto collected = sources();
    require(collected.surfaces.size() == 2, "Static body was not collected");
    const auto id = body.get<PersistentEntityId>().value;
    const auto found = std::find_if(collected.surfaces.begin(), collected.surfaces.end(),
                                    [&](const auto& source) { return source.entity == id; });
    require(found != collected.surfaces.end(), "Collected body lost its Scene identity");
    if (plane == NavigationPlane::PlanarXY)
        require(found->region && found->region->obstacle && found->collider &&
                    found->collider->dimensions == std::array<float, 2>{2, 4},
                "Collected XY solid did not become a physical obstacle contour");
    else
        require(found->collision.contains("forge.box_collider") &&
                    !found->collision.contains("forge.primitive") && !found->mesh,
                "Collected spatial shape used artwork instead of physical geometry");
    settings.collision_layers = 1;
    require(sources().surfaces.size() == 1, "Layer filtering did not remove the body");
    settings.collision_layers = UINT32_MAX;
    settings.collect_static_colliders = false;
    require(sources().surfaces.size() == 1, "Default profile automatically collected a body");
    settings.collect_static_colliders = true;
    if (plane == NavigationPlane::PlanarXY)
        body.set<NavigationRegion2D>({false});
    else
        body.set<NavigationSurface>({false});
    require(sources().surfaces.size() == 1, "Disabled marker was not an explicit opt-out");
    if (plane == NavigationPlane::PlanarXY) {
        body.remove<NavigationRegion2D>();
        auto config = body.get<PhysicsBody2D>();
        for (auto motion : {1u, 2u}) {
            config.motion = motion;
            body.set(config);
            require(sources().surfaces.size() == 1, "Moving XY body entered static sources");
        }
        config.motion = 0;
        config.sensor = true;
        body.set(config);
        require(sources().surfaces.size() == 1, "Sensor XY body entered solid sources");
        config.sensor = false;
        config.enabled = false;
        body.set(config);
        require(sources().surfaces.size() == 1, "Disabled XY body entered sources");
        config.enabled = true;
        body.set(config);
        body.set<CircleCollider2D>({1});
        rejects([&] { (void)sources(); });
    } else {
        body.remove<NavigationSurface>();
        auto config = body.get<PhysicsBody>();
        for (auto motion : {1u, 2u}) {
            config.motion = motion;
            body.set(config);
            require(sources().surfaces.size() == 1, "Moving spatial body entered static sources");
        }
        config.motion = 0;
        config.sensor = true;
        body.set(config);
        require(sources().surfaces.size() == 1, "Sensor spatial body entered solid sources");
        config.sensor = false;
        config.enabled = false;
        body.set(config);
        require(sources().surfaces.size() == 1, "Disabled spatial body entered sources");
        config.enabled = true;
        body.set(config);
        body.set<SphereCollider>({1});
        rejects([&] { (void)sources(); });
    }
}
void unresolved_intent(NavigationPlane plane) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto add = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    };
    const auto floor = scene.entity(add("Explicit floor"));
    floor.set<Primitive>({0}).set<NavigationSurface>({}).set<NavigationRegion2D>(
        {true, false, 20, 20});
    const auto parent = add("Prefab root"), original = add("Physical member");
    scene.reparent_entity(original, parent, ReparentMode::KeepLocal);
    const auto prefab = create_prefab_source(scene, parent);
    scene.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    flecs::entity inherited;
    scene.entity(instance).children([&](flecs::entity child) { inherited = child; });
    require(bool(inherited), "Prefab physical member did not realize");
    const auto id = inherited.get<PersistentEntityId>().value.str();
    // Instance-added physical intent survives removal of its template member.
    // An inherited member's property-only override does not add a body by itself.
    const auto* body_key =
        plane == NavigationPlane::PlanarXY ? "forge.physics_body_2d" : "forge.physics_body";
    const auto* shape_key =
        plane == NavigationPlane::PlanarXY ? "forge.box_collider_2d" : "forge.box_collider";
    for (const auto* component : {body_key, shape_key})
        authoring_command(scene, "component.add", {{"entity", id}, {"component", component}});
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", body_key}, {"field", "layer"}, {"value", 31u}});
    auto removed = prefab.source;
    require(removed.at("members").size() == 2, "Physical prefab fixture has extra members");
    removed["revision"] = removed.at("revision").get<unsigned>() + 1;
    auto& members = removed["members"];
    const auto child = std::find_if(members.begin(), members.end(), [&](const auto& member) {
        return member.at("id") != removed.at("root");
    });
    require(child != members.end(), "Physical prefab child identity is missing");
    members.erase(child);
    scene.set_prefab_sources({{prefab.asset(), removed}});
    require(!scene.entity(id), "Removed physical member remained native");
    NavigationSettings settings;
    settings.plane = plane;
    settings.collect_static_colliders = true;
    settings.collision_layers = 0x80000000u;
    require(scene.unresolved_navigation_sources(settings).size() == 1,
            "Unresolved selected physical intent was silently omitted");
    auto candidates = engine.world().world().query<const PersistentEntityId>();
    rejects([&] { (void)capture(scene.effective_document(), settings); });
    rejects([&] { (void)capture_native(engine.world(), scene, candidates, settings); });
    settings.collect_static_colliders = false;
    require(scene.unresolved_navigation_sources(settings).empty() &&
                capture_native(engine.world(), scene, candidates, settings).surfaces.size() == 1,
            "Unselected unresolved physical intent poisoned a legacy profile");
}
} // namespace
int main() {
    try {
        exercise(NavigationPlane::Spatial3D);
        exercise(NavigationPlane::PlanarXY);
        physical_floor(false);
        physical_floor(true);
        curved_physical_shapes();
        unresolved_intent(NavigationPlane::Spatial3D);
        unresolved_intent(NavigationPlane::PlanarXY);
        std::cout << "Scene/native static collision source selection pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
