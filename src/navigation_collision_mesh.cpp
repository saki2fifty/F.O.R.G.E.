#include "navigation_collision_mesh.hpp"
#include "builtins.hpp"
#include <forge/physics_components.hpp>

namespace forge::navigation_detail {
NavigationCollisionGeometry collision_geometry(const nlohmann::json& shape_components,
                                               const AffineTransform& world,
                                               PhysicsDebugCollision collision,
                                               std::size_t triangle_limit) {
    auto components = shape_components;
    // Geometry does not depend on mass, damping or contact material. Reuse the
    // ordinary native adapter with a canonical static body and the selected shape.
    components["forge.physics_body"] = detail::encode_builtin_value(PhysicsBody{});
    const auto pose = physics_debug_pose(components, world, decompose(world));
    auto geometry =
        prepare_physics_debug(components, pose.scale, std::move(collision), false, triangle_limit);
    if (geometry.truncated)
        throw std::runtime_error("Collected collision exceeds the navigation triangle budget");
    if (geometry.triangles.empty())
        throw std::runtime_error("Collected collision has no navigation triangles");
    auto rigid = pose;
    rigid.scale = {1, 1, 1};
    return {affine_transform(rigid), std::move(geometry)};
}
} // namespace forge::navigation_detail
