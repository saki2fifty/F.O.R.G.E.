#include "navigation_sources.hpp"
#include <forge/application_state_activation.hpp>
#include <forge/character_components.hpp>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>

namespace forge::navigation_detail {
namespace {
bool declared_prefab(flecs::entity e) {
    if (!e.has(flecs::Prefab))
        return false;
    if (e.has<AuthoredPrefab>())
        return true;
    for (auto parent = e.target(flecs::ChildOf); parent; parent = parent.target(flecs::ChildOf))
        if (parent.has(flecs::Prefab))
            return false;
    return true;
}
Json row(flecs::entity e, EntityId id, bool active) {
    Json out = {{"id", id}, {"components", Json::object()}};
    auto& c = out["components"];
    if (declared_prefab(e))
        out["prefab"] = true;
    if (e.has<NavigationSurface>())
        c["forge.navigation_surface"] = {{"enabled", active && e.get<NavigationSurface>().enabled}};
    if (e.has<NavigationRegion2D>()) {
        const auto& region = e.get<NavigationRegion2D>();
        c["forge.navigation_region_2d"] = {{"enabled", active && region.enabled},
                                           {"obstacle", region.obstacle},
                                           {"width", region.width},
                                           {"height", region.height}};
    }
    if (e.has<Primitive>())
        c["forge.primitive"] = {{"kind", e.get<Primitive>().kind}};
    if (e.has<MeshRenderer>()) {
        const auto& mesh = e.get<MeshRenderer>();
        c["forge.mesh_renderer"] = {{"mesh", mesh.mesh}, {"enabled", mesh.enabled}};
    }
    if (e.has<PhysicsBody>())
        c["forge.physics_body"] = {{"motion", active ? e.get<PhysicsBody>().motion : 0}};
    if (e.has<PhysicsBody2D>())
        c["forge.physics_body_2d"] = {{"motion", active ? e.get<PhysicsBody2D>().motion : 0}};
    if (e.has<CharacterController>())
        c["forge.character_controller"] = {
            {"enabled", active && e.get<CharacterController>().enabled}};
    if (e.has<CharacterController2D>())
        c["forge.character_controller_2d"] = {
            {"enabled", active && e.get<CharacterController2D>().enabled}};
    if (e.has<NavigationAgent>())
        c["forge.navigation_agent"] = {{"enabled", active && e.get<NavigationAgent>().enabled}};
    const auto binding = e.has<SpatialBinding>() ? e.get<SpatialBinding>() : SpatialBinding{};
    if (binding.mode == SpatialMode::World)
        out["spatial"] = {{"mode", "world"}};
    else if (binding.mode == SpatialMode::Explicit)
        out["spatial"] = {{"mode", "explicit"}, {"target", binding.target}};
    auto parent = e.target(flecs::ChildOf);
    if (parent && parent.owns<StableId>())
        out["parent"] = parent.get<StableId>().value;
    // Match the effective Scene projection: a transform-less group never gains
    // geometry merely because the evaluator has derived values elsewhere.
    if (e.has<LocalTranslation>() && e.has<WorldTransform>()) {
        const auto& transform = e.get<WorldTransform>();
        out["world_affine"] = transform.affine.m;
        out["spatial_resolved"] = transform.resolved;
    }
    return out;
}
} // namespace
Sources capture_native(WorldContext& context, const Scene& scene,
                       const flecs::query<const PersistentEntityId>& candidates,
                       NavigationPlane plane) {
    if (context.world().c_ptr() != scene.world().c_ptr())
        throw std::logic_error("Navigation capture requires the Scene's owning world");
    context.evaluate_world_transforms();
    ApplicationStateActivation conditions(context);
    std::map<EntityId, Json> rows;
    auto add = [&](flecs::entity e, EntityId id) {
        // Logical Scene AssetId alone cannot distinguish two loaded instances.
        // Scene's existing identity lookup owns that membership decision.
        if (!e || scene.entity(id.str()) != e)
            return false;
        return rows.try_emplace(id, row(e, id, conditions.active(e.id()))).second;
    };
    std::vector<std::pair<flecs::entity, EntityId>> selected;
    candidates.each([&](flecs::entity e, const PersistentEntityId& id) {
        const bool enabled =
            plane == NavigationPlane::PlanarXY
                ? e.has<NavigationRegion2D>() && e.get<NavigationRegion2D>().enabled
                : e.has<NavigationSurface>() && e.get<NavigationSurface>().enabled;
        if (enabled && !declared_prefab(e))
            selected.emplace_back(e, id.value);
    });
    for (const auto& [entity, id] : selected)
        if (conditions.active(entity.id()))
            add(entity, id);
    if (rows.size() > 512)
        throw std::runtime_error("Navigation geometry exceeds 512 entities");
    // These are ephemeral copies, not a second hierarchy or transform authority.
    // Expand only ancestors followed by the current static-source validator.
    std::vector<EntityId> pending;
    for (const auto& [id, value] : rows)
        pending.push_back(id);
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& value = rows.at(pending[i]);
        const auto spatial = value.value("spatial", Json::object());
        const auto mode = spatial.value("mode", std::string("follow_structure"));
        if (mode == "world")
            continue;
        std::string parent;
        if (mode == "explicit") {
            const auto target = spatial.at("target").get<EntityRef>();
            if (target.scene != scene.asset_id())
                continue; // Shared validator supplies the external-parent diagnostic.
            parent = target.entity.str();
        } else
            parent = value.value("parent", std::string{});
        if (parent.empty())
            continue;
        const auto e = scene.entity(parent);
        if (e && e.owns<PersistentEntityId>()) {
            const auto id = e.get<PersistentEntityId>().value;
            if (add(e, id))
                pending.push_back(id);
        }
    }
    Json doc = {{"asset_id", scene.asset_id()}, {"entities", Json::array()}};
    for (auto& [id, value] : rows)
        doc["entities"].push_back(std::move(value));
    for (auto& value : scene.unresolved_navigation_sources(plane))
        doc["entities"].push_back(std::move(value));
    return capture(doc, plane);
}
} // namespace forge::navigation_detail
