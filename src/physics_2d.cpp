#include "physics_2d.hpp"
#include "builtins.hpp"
#include "physics_2d_joint_native.hpp"
#include "physics_2d_transform.hpp"
#include "physics_debug.hpp"
#include "physics_joint_2d_value.hpp"
#include "runtime_tile_map.hpp"
#include "tile_map_collision.hpp"
#include <algorithm>
#include <box2d/box2d.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace forge::physics_detail {
namespace {
constexpr std::size_t body_limit = 1024, pair_limit = 4096, shape_limit = 4096;
constexpr std::size_t query_work_limit = 16384, event_limit = 16384;
constexpr float mover_slop = .005f, mover_tolerance = .00001f;
constexpr std::size_t mover_plane_limit = 64;
constexpr unsigned mover_iteration_limit = 8;
// Private native query traffic bypasses body collision masks while retaining
// the existing 32 named project layers. No extra authored layer is introduced.
constexpr std::uint64_t query_category = std::uint64_t{1} << 63;
void planar_rotation(LocalRotation value) {
    const auto q = normalized(value);
    if (std::abs(q.x) > 1e-6f || std::abs(q.y) > 1e-6f)
        throw std::runtime_error(
            "2D physics requires pure Z rotation throughout its spatial ancestry");
}
void valid_xy(LocalTranslation value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || std::abs(value.x) > 1000000 ||
        std::abs(value.y) > 1000000)
        throw std::runtime_error("2D physics position exceeds supported +/-1000000 metres");
}
LocalTransform planar_pose(std::uint64_t id, const std::map<std::uint64_t, TransformNode>& nodes,
                           const std::map<std::uint64_t, EvaluatedTransform>& evaluated) {
    auto hint = nodes.at(id).local;
    // Pure-Z ancestors fix the depth sign independently. Supply sign preferences
    // to the existing decomposer so a reflected depth axis cannot select X/Y tilt.
    hint.scale = {1, 1, 1};
    for (auto node = id; node; node = nodes.at(node).parent) {
        const auto& local = nodes.at(node).local;
        planar_rotation(local.rotation);
        hint.scale.x *= std::signbit(local.scale.x) ? -1.f : 1.f;
        hint.scale.y *= std::signbit(local.scale.y) ? -1.f : 1.f;
        hint.scale.z *= local.scale.z == 0 ? 0.f : (std::signbit(local.scale.z) ? -1.f : 1.f);
    }
    const auto& affine = evaluated.at(id).affine;
    return checked_planar_pose_2d(affine, hint);
}
float dimension(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("2D effective collider dimensions exceed the supported profile");
    return float(value);
}
b2Rot native_rotation(LocalRotation value) {
    const auto q = normalized(value);
    const double length = std::hypot(double(q.z), double(q.w));
    const double z = q.z / length, w = q.w / length;
    // Convert the existing quaternion directly; do not introduce the native
    // angle-to-trigonometry approximation at the FORGE transform boundary.
    return {float(w * w - z * z), float(2 * w * z)};
}
LocalRotation copied_rotation(b2Rot value) {
    if (!std::isfinite(value.c) || !std::isfinite(value.s) ||
        std::hypot(double(value.c), double(value.s)) < .5)
        throw std::runtime_error("2D solver produced invalid rotation");
    const double half = .5 * std::atan2(double(value.s), double(value.c));
    return {0, 0, float(std::sin(half)), float(std::cos(half))};
}
struct Geometry {
    b2Polygon box{};
    b2Circle circle{};
    b2Capsule capsule{};
    b2MassData mass{};
};
Geometry geometry(const Body2DConfiguration& config) {
    Geometry result;
    const auto& d = config.dimensions;
    if (config.shape == 0) {
        result.box = b2MakeBox(d[0] * .5f, d[1] * .5f);
        result.mass = b2ComputePolygonMass(&result.box, config.body.density);
    } else if (config.shape == 1) {
        result.circle = {{0, 0}, d[0]};
        result.mass = b2ComputeCircleMass(&result.circle, config.body.density);
    } else if (config.shape == 2) {
        result.capsule = {{0, -d[1] * .5f}, {0, d[1] * .5f}, d[0]};
        result.mass = b2ComputeCapsuleMass(&result.capsule, config.body.density);
    } else
        throw std::runtime_error("Unknown admitted 2D collider kind");
    if (config.body.mass > 0) {
        const float ratio = config.body.mass / result.mass.mass;
        result.mass.mass = config.body.mass;
        result.mass.rotationalInertia *= ratio;
    }
    if (!(result.mass.mass > 0) || !std::isfinite(result.mass.mass) ||
        !(result.mass.rotationalInertia > 0) || !std::isfinite(result.mass.rotationalInertia))
        throw std::runtime_error("2D collider mass/inertia is not positive and finite");
    return result;
}
b2Polygon compound_box(const BoxPart2D& value) {
    for (unsigned axis = 0; axis < 2; ++axis) {
        (void)dimension(value.dimensions[axis], .01f, 1000);
        if (!std::isfinite(value.center[axis]) ||
            std::abs(double(value.center[axis])) + .5 * value.dimensions[axis] > 10000)
            throw std::runtime_error("2D compound box exceeds supported local extent");
    }
    return b2MakeOffsetBox(value.dimensions[0] * .5f, value.dimensions[1] * .5f,
                           {value.center[0], value.center[1]}, {1, 0});
}
std::size_t shape_count(const Body2DConfiguration& config) {
    return config.shape == 3 ? (config.parts ? config.parts->size() : 0) : 1;
}
void validate_geometry(const Body2DConfiguration& config) {
    if (config.shape != 3) {
        if (config.parts)
            throw std::runtime_error("Inline 2D collider has compound geometry");
        (void)geometry(config);
        return;
    }
    if (config.body.motion != 0 || config.body.sensor ||
        config.dimensions != std::array<float, 2>{} || !config.parts || config.parts->empty() ||
        config.parts->size() > shape_limit)
        throw std::runtime_error("2D compound boxes require a bounded nonempty Static solid");
    for (const auto& part : *config.parts)
        (void)compound_box(part);
}
b2ShapeProxy overlap_proxy(const PhysicsOverlap2D& query) {
    valid_xy({query.origin[0], query.origin[1], 0});
    if (!std::isfinite(query.angle) || std::abs(query.angle) > 1000000)
        throw std::runtime_error("2D overlap angle exceeds the supported radians profile");
    const LocalRotation q{0, 0, float(std::sin(.5 * query.angle)),
                          float(std::cos(.5 * query.angle))};
    const auto rotation = native_rotation(q);
    const auto& d = query.dimensions;
    if (query.shape == PhysicsOverlap2D::Shape::Box) {
        const float x = .5f * dimension(d[0], .01f, 1000), y = .5f * dimension(d[1], .01f, 1000);
        const b2Vec2 points[] = {{-x, -y}, {x, -y}, {x, y}, {-x, y}};
        return b2MakeOffsetProxy(points, 4, 0, {0, 0}, rotation);
    }
    const float radius = dimension(d[0], .005f, 500);
    if (query.shape == PhysicsOverlap2D::Shape::Circle) {
        const b2Vec2 point{0, 0};
        if (d[1] != 0)
            throw std::runtime_error("2D circle overlap has an unused nonzero height");
        return b2MakeProxy(&point, 1, radius);
    }
    if (query.shape != PhysicsOverlap2D::Shape::Capsule)
        throw std::runtime_error("Unknown 2D overlap shape kind");
    const float height = dimension(d[1], .01f, 1000);
    if (double(height) + 2 * double(radius) > 1000)
        throw std::runtime_error("2D overlap capsule total height exceeds the profile");
    const b2Vec2 points[] = {{0, -.5f * height}, {0, .5f * height}};
    return b2MakeOffsetProxy(points, 2, radius, {0, 0}, rotation);
}
b2ShapeProxy sweep_proxy(const PhysicsSweep2D& query) {
    if (!std::isfinite(query.displacement[0]) || !std::isfinite(query.displacement[1]))
        throw std::runtime_error("2D sweep displacement must be finite");
    valid_xy({query.origin[0] + query.displacement[0], query.origin[1] + query.displacement[1], 0});
    if (query.ignore && (!query.ignore->scene || !query.ignore->entity))
        throw std::runtime_error("2D sweep exclusion requires a complete canonical reference");
    return overlap_proxy({query.shape, query.dimensions, query.origin, query.angle});
}
b2QueryFilter query_filter(PhysicsQueryFilter value, std::uint64_t category = query_category) {
    auto result = b2DefaultQueryFilter();
    result.categoryBits = category;
    result.maskBits = value.mask;
    return result;
}
} // namespace

void validate_planar_rotation_2d(LocalRotation rotation) { planar_rotation(rotation); }
LocalTransform checked_planar_pose_2d(const AffineTransform& affine, const LocalTransform& hint) {
    (void)PlanarMapping(affine);
    const auto result = decompose(affine, &hint);
    planar_rotation(result.rotation);
    valid_xy(result.translation);
    return result;
}
void validate_ray_2d(Double2 origin, Double2 displacement) {
    valid_xy({origin[0], origin[1], 0});
    valid_xy({origin[0] + displacement[0], origin[1] + displacement[1], 0});
    if (!std::isfinite(displacement[0]) || !std::isfinite(displacement[1]) ||
        (displacement[0] == 0 && displacement[1] == 0))
        throw std::runtime_error("2D ray displacement must be finite and nonzero");
}
void validate_overlap_2d(const PhysicsOverlap2D& query) { (void)overlap_proxy(query); }
void validate_sweep_2d(const PhysicsSweep2D& query) { (void)sweep_proxy(query); }
Body2DConfiguration inline_configuration_2d(PhysicsBody2D body, unsigned shape,
                                            std::array<float, 2> raw, LocalScale scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z))
        throw std::runtime_error("2D collider scale must be finite");
    Body2DConfiguration config{body, shape};
    const double x = std::abs(scale.x), y = std::abs(scale.y);
    if (shape == 0)
        config.dimensions = {dimension(raw[0] * x, .01f, 1000), dimension(raw[1] * y, .01f, 1000)};
    else {
        if (shape > 2)
            throw std::runtime_error("Unknown 2D collider kind");
        if (std::abs(x - y) > 1e-6 * std::max(x, y))
            throw std::runtime_error("Circle/Capsule Collider 2D requires equal absolute XY scale");
        config.dimensions[0] = dimension(raw[0] * x, .005f, 500);
        if (shape == 2) {
            config.dimensions[1] = dimension(raw[1] * y, .01f, 1000);
            if (double(config.dimensions[1]) + 2 * double(config.dimensions[0]) > 1000)
                throw std::runtime_error("2D capsule total height exceeds 1000 metres");
        } else if (raw[1] != 0)
            throw std::runtime_error("Circle Collider 2D has an unused height");
    }
    (void)geometry(config);
    return config;
}
Body2DConfiguration tile_map_configuration_2d(PhysicsBody2D body, const TileMapData& cells,
                                              std::uint64_t layer, LocalScale scale) {
    if (body.motion != 0 || body.sensor)
        throw std::runtime_error("Tile Map Collider 2D requires a Static nonsensor body");
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z))
        throw std::runtime_error("2D collider scale must be finite");
    if (scale.x == 0 || scale.y == 0)
        throw std::runtime_error("Tile Map collision requires nonzero XY scale");
    const auto rectangles = tile_map_collision_rectangles(cells, layer);
    const auto size = cells.cell_size();
    std::vector<BoxPart2D> values;
    values.reserve(rectangles.size());
    for (const auto& rectangle : rectangles) {
        const std::array<std::int32_t, 2> origin{rectangle.origin.x, rectangle.origin.y};
        const std::array<std::uint32_t, 2> counts{rectangle.width, rectangle.height};
        const std::array<float, 2> axes{scale.x, scale.y};
        BoxPart2D box;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const double full = double(counts[axis]) * size[axis] * std::abs(axes[axis]);
            const double center =
                (double(origin[axis]) + .5 * counts[axis]) * size[axis] * axes[axis];
            box.dimensions[axis] = dimension(full, .01f, 1000);
            if (!std::isfinite(center) || std::abs(center) + .5 * full > 10000)
                throw std::runtime_error(
                    "Tile Map collision exceeds supported scaled local extent");
            box.center[axis] = float(center);
        }
        (void)compound_box(box);
        values.push_back(box);
    }
    auto parts = std::make_shared<const std::vector<BoxPart2D>>(std::move(values));
    Body2DConfiguration config{body, 3};
    config.parts = std::move(parts);
    return config;
}
LocalTransform debug_pose_2d(const AffineTransform& world) {
    (void)PlanarMapping(world);
    const double x = std::hypot(world.m[0], world.m[4]);
    const double y = std::hypot(world.m[1], world.m[5]);
    const double z = std::hypot(world.m[2], world.m[6], world.m[10]);
    const double orientation =
        (world.m[0] / x) * (world.m[5] / y) - (world.m[1] / y) * (world.m[4] / x);
    const double half = .5 * std::atan2(world.m[4], world.m[0]);
    LocalTransform hint;
    // The X-axis direction determines pure-Z rotation; the determinant fixes
    // the Y sign. Applying these axes to offset boxes also preserves the exact
    // affine map beneath reflected or flat ancestors.
    hint.scale = {float(x), float(std::copysign(y, orientation)),
                  float(std::copysign(z, world.m[10]))};
    hint.rotation = {0, 0, float(std::sin(half)), float(std::cos(half))};
    const auto pose = decompose(world, &hint);
    planar_rotation(pose.rotation);
    return pose;
}
PhysicsDebugGeometry prepare_debug_2d(const Json& components, LocalScale scale, std::size_t limit,
                                      const TileMapData* tile_map) {
    if (!limit || limit > 65536)
        throw std::runtime_error("2D preview line budget must be1..65536");
    const auto decoded = [&]<class T>(const char* key) -> T {
        const auto& value = components.at(key);
        detail::validate_components(Json{{key, value}});
        for (const auto& type : detail::builtins())
            if (std::string_view(type.name) == key)
                return std::get<T>(type.decode(detail::canonical_builtin_value(type, value)));
        throw std::runtime_error("Unknown 2D preview component");
    };
    const auto body = decoded.template operator()<PhysicsBody2D>("forge.physics_body_2d");
    if (body.enabled)
        for (const char* other : {"forge.physics_body", "forge.character_controller"})
            if (components.contains(other) && components.at(other).value("enabled", true))
                throw std::runtime_error("2D preview cannot share enabled simulation writers");
    unsigned count = 0;
    for (const char* name : {"forge.box_collider_2d", "forge.circle_collider_2d",
                             "forge.capsule_collider_2d", "forge.tile_map_collider_2d"})
        count += components.contains(name);
    if (count != 1)
        throw std::runtime_error("2D body preview requires exactly one 2D collider");
    if (components.contains("forge.tile_map_collider_2d")) {
        const auto collider =
            decoded.template operator()<TileMapCollider2D>("forge.tile_map_collider_2d");
        const auto renderer =
            decoded.template operator()<TileMapRenderer>("forge.tile_map_renderer");
        if (!tile_map || tile_map->asset() != renderer.map.id)
            throw std::runtime_error("Published Tile Map is not prepared for collision preview");
        const auto config = tile_map_configuration_2d(body, *tile_map, collider.layer, scale);
        PhysicsDebugGeometry result;
        result.truncated = config.parts->size() > limit / 4;
        for (const auto& part : *config.parts) {
            const auto polygon = compound_box(part);
            for (int i = 0; i < polygon.count; ++i) {
                if (result.lines.size() == limit)
                    return result;
                const auto a = polygon.vertices[i], b = polygon.vertices[(i + 1) % polygon.count];
                result.lines.push_back({{{a.x, a.y, 0}, {b.x, b.y, 0}}});
            }
        }
        return result;
    }
    unsigned kind = 0;
    std::array<float, 2> raw{};
    if (components.contains("forge.box_collider_2d")) {
        const auto value = decoded.template operator()<BoxCollider2D>("forge.box_collider_2d");
        raw = {value.x, value.y};
    } else if (components.contains("forge.circle_collider_2d")) {
        kind = 1;
        raw[0] = decoded.template operator()<CircleCollider2D>("forge.circle_collider_2d").radius;
    } else {
        kind = 2;
        const auto value =
            decoded.template operator()<CapsuleCollider2D>("forge.capsule_collider_2d");
        raw = {value.radius, value.height};
    }
    const auto config = inline_configuration_2d(body, kind, raw, scale);
    const auto& d = config.dimensions;
    using Point = std::array<float, 3>;
    std::vector<Point> outline;
    if (kind == 0)
        outline = {{-.5f * d[0], -.5f * d[1], 0},
                   {.5f * d[0], -.5f * d[1], 0},
                   {.5f * d[0], .5f * d[1], 0},
                   {-.5f * d[0], .5f * d[1], 0}};
    else {
        const unsigned arcs = kind == 1 ? 1 : 2;
        for (unsigned arc = 0; arc < arcs; ++arc)
            for (unsigned i = 0; i < (kind == 1 ? 64u : 33u); ++i) {
                const double angle =
                    kind == 1 ? 2 * std::numbers::pi * i / 64 : std::numbers::pi * (arc + i / 32.);
                const double center = kind == 1 ? 0 : (arc == 0 ? .5 : -.5) * d[1];
                outline.push_back(
                    {float(d[0] * std::cos(angle)), float(center + d[0] * std::sin(angle)), 0});
            }
    }
    PhysicsDebugGeometry result;
    result.truncated = outline.size() > limit;
    for (std::size_t i = 0; i < std::min(outline.size(), limit); ++i)
        result.lines.push_back({outline[i], outline[(i + 1) % outline.size()]});
    return result;
}

Desired2D desired_bodies_2d(WorldContext& context, const PhysicsConfig& settings,
                            const std::map<std::uint64_t, TransformNode>& nodes,
                            const Native2D* previous, const TileMap2DOverride* candidate,
                            bool allow_pending_maps) {
    std::vector<flecs::entity_t> candidates;
    auto query = context.world().query<const PhysicsBody2D>();
    query.each([&](flecs::entity entity, const PhysicsBody2D& body) {
        if (body.enabled)
            candidates.push_back(entity.id());
    });
    if (candidates.size() > body_limit)
        throw std::runtime_error("2D physics body capacity 1024 exceeded");
    if (candidates.empty())
        return {};
    const auto evaluated = evaluate_transforms(nodes);
    Desired2D result;
    for (auto id : candidates) {
        const auto entity = context.world().entity(id);
        const auto body = entity.get<PhysicsBody2D>();
        detail::validate_reflected_value(entity, body);
        const auto ref = context.reference(id);
        if (!ref || context.resolve(*ref).state != WorldContext::ResolveState::Available)
            throw std::runtime_error("2D physics requires unambiguous authored scene membership");
        if ((entity.has<PhysicsBody>() && entity.get<PhysicsBody>().enabled) ||
            (entity.has<CharacterController>() && entity.get<CharacterController>().enabled))
            throw std::runtime_error("An entity cannot share enabled 2D and 3D simulation writers");
        if (body.layer >= settings.layers.size() || settings.layers[body.layer].empty())
            throw std::runtime_error("2D body selects an unnamed project collision layer");
        if (!evaluated.contains(id) || !evaluated.at(id).resolved)
            throw std::runtime_error("2D body needs a resolved transform");
        if (body.motion == 2 && (!entity.has<SpatialBinding>() ||
                                 entity.get<SpatialBinding>().mode != SpatialMode::World))
            throw std::runtime_error("Dynamic 2D body requires Child space: World");
        if (body.motion != 2)
            for (auto parent = nodes.at(id).parent; parent; parent = nodes.at(parent).parent) {
                const auto ancestor = context.world().entity(parent);
                if ((ancestor.has<PhysicsBody>() && ancestor.get<PhysicsBody>().enabled &&
                     ancestor.get<PhysicsBody>().motion == 2) ||
                    (ancestor.has<PhysicsBody2D>() && ancestor.get<PhysicsBody2D>().enabled &&
                     ancestor.get<PhysicsBody2D>().motion == 2) ||
                    (ancestor.has<CharacterController>() &&
                     ancestor.get<CharacterController>().enabled))
                    throw std::runtime_error("Static/Kinematic 2D body cannot spatially follow a "
                                             "Dynamic body or Character");
            }
        const auto pose = planar_pose(id, nodes, evaluated);
        if (nodes.at(id).parent)
            (void)PlanarMapping(evaluated.at(nodes.at(id).parent).affine);
        if (unsigned(entity.has<BoxCollider2D>()) + unsigned(entity.has<CircleCollider2D>()) +
                unsigned(entity.has<CapsuleCollider2D>()) +
                unsigned(entity.has<TileMapCollider2D>()) !=
            1)
            throw std::runtime_error(
                "2D body requires exactly one Box, Circle, Capsule or Tile Map Collider 2D");
        if (entity.has<TileMapCollider2D>()) {
            const auto collider = entity.get<TileMapCollider2D>();
            detail::validate_reflected_value(entity, collider);
            if (body.motion != 0 || body.sensor || !entity.has<TileMapRenderer>())
                throw std::runtime_error(
                    "Tile Map Collider 2D requires a Static nonsensor body and Tile Map Renderer");
            const bool overridden = candidate && candidate->entity == id;
            std::shared_ptr<const detail::RuntimeTileMapSnapshot> snapshot;
            const TileMapData* cells = overridden ? candidate->cells : nullptr;
            if (!overridden) {
                const auto status = detail::inspect_runtime_tile_map(context, *ref);
                if (!status.ready || status.adopted != entity.get<TileMapRenderer>().map.id) {
                    if (allow_pending_maps)
                        continue;
                    throw std::runtime_error(
                        "Tile Map Collider 2D requires its requested Map instance "
                        "to be ready before simulation");
                }
                snapshot = detail::runtime_tile_map_snapshot(context, *ref);
                cells = &snapshot->cells;
            }
            if (!cells || cells->asset() != entity.get<TileMapRenderer>().map.id)
                throw std::runtime_error("Tile Map collision candidate has a different Map root");
            const auto* old = previous ? previous->admitted(id) : nullptr;
            std::shared_ptr<const std::vector<BoxPart2D>> parts;
            if (!overridden && old && old->ref == *ref && old->tile_snapshot == snapshot &&
                old->tile_layer == collider.layer && old->pose.scale.x == pose.scale.x &&
                old->pose.scale.y == pose.scale.y && old->config.shape == 3)
                parts = old->config.parts;
            else {
                parts = tile_map_configuration_2d(body, *cells, collider.layer, pose.scale).parts;
            }
            // An empty layer has no collision or native handle. Refilling it
            // creates a new realization epoch; no hidden placeholder shape exists.
            if (!parts || parts->empty())
                continue;
            Body2DConfiguration config{body, 3};
            config.parts = std::move(parts);
            result.emplace(id,
                           Body2DDesired{*ref, std::move(config), pose, snapshot, collider.layer});
            continue;
        }
        unsigned kind = 0;
        std::array<float, 2> raw{};
        if (entity.has<BoxCollider2D>()) {
            const auto value = entity.get<BoxCollider2D>();
            detail::validate_reflected_value(entity, value);
            raw = {value.x, value.y};
        } else {
            kind = 1;
            if (entity.has<CircleCollider2D>()) {
                const auto value = entity.get<CircleCollider2D>();
                detail::validate_reflected_value(entity, value);
                raw[0] = value.radius;
            } else {
                const auto value = entity.get<CapsuleCollider2D>();
                detail::validate_reflected_value(entity, value);
                kind = 2;
                raw = {value.radius, value.height};
            }
        }
        const auto config = inline_configuration_2d(body, kind, raw, pose.scale);
        result.emplace(id, Body2DDesired{*ref, config, pose});
    }
    return result;
}
void validate_shape_capacity_2d(const Desired2D& desired) {
    if (desired.size() > body_limit)
        throw std::runtime_error("2D physics body capacity1024 exceeded");
    std::size_t shapes = 0;
    for (const auto& [id, value] : desired) {
        const auto count = shape_count(value.config);
        if (count > shape_limit - shapes)
            throw std::runtime_error("2D physics shape capacity4096 exceeded");
        shapes += count;
    }
}

struct Native2D::Impl {
    PhysicsConfig config;
    b2WorldId world{};
    bool fault = false, overflow = false;
    std::size_t pair_work = 0;
    std::uint64_t next_epoch = 1;
    struct Body {
        b2BodyId id{};
        std::vector<b2ShapeId> shapes;
        Body2DDesired admitted;
        std::uint64_t epoch = 0;
    };
    std::map<std::uint64_t, Body> bodies;
    struct Joint {
        b2JointId id{};
        Joint2DDesired admitted;
    };
    std::map<std::uint64_t, Joint> joints;
    bool body_changes(std::uint64_t id, const Desired2D& desired) const {
        const auto next = desired.find(id);
        const auto old = bodies.find(id);
        return next == desired.end() || old == bodies.end() ||
               next->second.ref != old->second.admitted.ref ||
               next->second.config != old->second.admitted.config;
    }
    std::size_t ending_joint_contacts(const DesiredJoints2D& joints,
                                      const Desired2D& desired) const {
        std::size_t ends = 0;
        if (!joints.empty()) {
            using Pair = std::pair<std::uint64_t, std::uint64_t>;
            std::set<Pair> old_exclusions;
            for (const auto& [id, joint] : this->joints)
                if (!joint.admitted.config.collide_connected)
                    old_exclusions.emplace(std::min(joint.admitted.a, joint.admitted.b),
                                           std::max(joint.admitted.a, joint.admitted.b));
            std::map<std::uint64_t, std::set<std::uint64_t>> new_exclusions;
            for (const auto& [id, joint] : joints) {
                const Pair pair{std::min(joint.a, joint.b), std::max(joint.a, joint.b)};
                if (!joint.config.collide_connected && !old_exclusions.contains(pair) &&
                    !body_changes(pair.first, desired) && !body_changes(pair.second, desired) &&
                    bodies.contains(pair.first) && bodies.contains(pair.second))
                    for (const auto shape : bodies.at(pair.second).shapes)
                        new_exclusions[pair.first].insert(b2StoreShapeId(shape));
            }
            for (const auto& [id, others] : new_exclusions) {
                const auto body = bodies.at(id).id;
                const int capacity = b2Body_GetContactCapacity(body);
                if (capacity < 0 || capacity > int(pair_limit))
                    throw std::runtime_error("Native 2D connected contact capacity mismatch");
                std::vector<b2ContactData> contacts(static_cast<std::size_t>(capacity));
                const int count = b2Body_GetContactData(body, contacts.data(), capacity);
                if (count < 0 || count > capacity)
                    throw std::runtime_error("Native 2D connected contact enumeration mismatch");
                for (int i = 0; i < count; ++i)
                    if (others.contains(b2StoreShapeId(contacts[i].shapeIdA)) ||
                        others.contains(b2StoreShapeId(contacts[i].shapeIdB)))
                        ++ends;
            }
        }
        return ends;
    }
    void retire_joints(const DesiredJoints2D& desired, const Desired2D& desired_bodies) {
        for (auto old = joints.begin(); old != joints.end();) {
            const auto next = desired.find(old->first);
            if (next == desired.end() ||
                !joint_same_geometry_2d(old->second.admitted, next->second) ||
                body_changes(old->second.admitted.a, desired_bodies) ||
                body_changes(old->second.admitted.b, desired_bodies)) {
                retire_joint_2d(old->second.id);
                old = joints.erase(old);
            } else
                ++old;
        }
    }
    void realize_joints(const DesiredJoints2D& desired) {
        for (const auto& [id, value] : desired) {
            auto found = joints.find(id);
            if (found == joints.end()) {
                const auto native =
                    create_joint_2d(world, value, bodies.at(value.a).id, bodies.at(value.b).id);
                joints.emplace(id, Joint{native, value});
            } else {
                update_joint_2d(found->second.id, found->second.admitted.config, value.config);
                found->second.admitted = value;
            }
        }
    }
    std::vector<b2JointId> joint_handles() const {
        const auto count = b2World_GetCounters(world).jointCount;
        if (count < 0 || count > 4096)
            throw std::runtime_error("Native 2D joint capacity mismatch");
        std::map<EntityRef, const Body*> ordered;
        for (const auto& [id, body] : bodies)
            if (!ordered.emplace(body.admitted.ref, &body).second)
                throw std::runtime_error("Duplicate canonical 2D body in joint enumeration");
        std::vector<b2JointId> result;
        std::set<std::uint64_t> seen;
        for (const auto& [ref, body] : ordered) {
            const auto capacity = b2Body_GetJointCount(body->id);
            if (capacity < 0 || capacity > count)
                throw std::runtime_error("Invalid native 2D body joint count");
            std::vector<b2JointId> attached(static_cast<std::size_t>(capacity));
            if (b2Body_GetJoints(body->id, attached.data(), capacity) != capacity)
                throw std::runtime_error("Incomplete native 2D joint enumeration");
            for (const auto joint : attached)
                if (B2_ID_EQUALS(b2Joint_GetBodyA(joint), body->id)) {
                    if (!seen.insert(b2StoreJointId(joint)).second)
                        throw std::runtime_error("Duplicate native 2D joint enumeration");
                    result.push_back(joint);
                }
        }
        if (result.size() != std::size_t(count))
            throw std::runtime_error("Native 2D joint inventory is not closed over owned bodies");
        return result;
    }
    struct Binding {
        EntityRef ref;
        std::uint64_t epoch;
    };
    std::map<std::uint64_t, Binding> retired;
    std::vector<PhysicsContact2D> events;
    std::size_t pending_end_bound = 0;
    std::uint64_t last_step_tick = 0, last_event_tick = 0;
    void retire(const Body& body) {
        pending_end_bound += std::size_t(b2Body_GetContactCapacity(body.id));
        for (const auto shape : body.shapes) {
            if (body.admitted.config.body.sensor)
                pending_end_bound += std::size_t(b2Shape_GetSensorCapacity(shape));
            if (!retired.emplace(b2StoreShapeId(shape), Binding{body.admitted.ref, body.epoch})
                     .second)
                throw std::runtime_error("2D retired-shape generation alias");
        }
        b2DestroyBody(body.id);
    }
    const Body* owned(b2ShapeId shape) const noexcept {
        if (!b2Shape_IsValid(shape))
            return nullptr;
        const auto* body = static_cast<const Body*>(b2Shape_GetUserData(shape));
        return body && B2_ID_EQUALS(body->id, b2Shape_GetBody(shape)) ? body : nullptr;
    }
    Binding binding(b2ShapeId shape) const {
        if (const auto* body = owned(shape))
            return {body->admitted.ref, body->epoch};
        const auto old = retired.find(b2StoreShapeId(shape));
        if (old != retired.end())
            return old->second;
        throw std::runtime_error("2D event has no current or retained native shape binding");
    }
    static bool charge_query_work(std::size_t& local, std::size_t* shared = nullptr) noexcept {
        if (local >= query_work_limit || (shared && *shared >= query_work_limit))
            return false;
        ++local;
        if (shared)
            ++*shared;
        return true;
    }
    struct RayQuery {
        const Impl* self;
        PhysicsQueryFilter filter;
        std::optional<EntityRef> ignore{};
        bool ignore_initial_overlap = false;
        std::size_t work = 0;
        bool failed = false;
        std::optional<PhysicsHit2D> hit;
        std::size_t* shared_work = nullptr;
    };
    static float ray_hit(b2ShapeId shape, b2Pos point, b2Vec2 normal, float fraction,
                         void* pointer) noexcept {
        auto& query = *static_cast<RayQuery*>(pointer);
        if (!charge_query_work(query.work, query.shared_work)) {
            query.failed = true;
            return 0;
        }
        const auto* body = query.self->owned(shape);
        if (!body || !std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(fraction) ||
            fraction < 0 || fraction > 1) {
            query.failed = true;
            return 0;
        }
        if ((!query.filter.include_sensors && body->admitted.config.body.sensor) ||
            (query.ignore && body->admitted.ref == *query.ignore) ||
            (query.ignore_initial_overlap && fraction == 0))
            return -1;
        if (!query.hit || fraction < query.hit->fraction)
            query.hit = PhysicsHit2D{
                body->admitted.ref, {point.x, point.y}, {normal.x, normal.y}, fraction};
        return float(query.hit->fraction);
    }
    struct MoverQuery {
        RayQuery query;
        b2ShapeProxy mover;
        b2Pos origin;
        b2Vec2 displacement;
    };
    static bool mover_candidate(b2ShapeId shape, void* pointer) noexcept {
        auto& result = *static_cast<MoverQuery*>(pointer);
        auto& query = result.query;
        if (query.failed || (query.hit && query.hit->fraction == 0))
            return false;
        if (!charge_query_work(query.work, query.shared_work)) {
            query.failed = true;
            return false;
        }
        const auto* body = query.self->owned(shape);
        if (!body) {
            query.failed = true;
            return false;
        }
        if ((!query.filter.include_sensors && body->admitted.config.body.sensor) ||
            (query.ignore && body->admitted.ref == *query.ignore))
            return true;
        b2ShapeProxy obstacle{};
        switch (b2Shape_GetType(shape)) {
        case b2_polygonShape: {
            const auto value = b2Shape_GetPolygon(shape);
            obstacle = b2MakeProxy(value.vertices, value.count, value.radius);
            break;
        }
        case b2_circleShape: {
            const auto value = b2Shape_GetCircle(shape);
            obstacle = b2MakeProxy(&value.center, 1, value.radius);
            break;
        }
        case b2_capsuleShape: {
            const auto value = b2Shape_GetCapsule(shape);
            const b2Vec2 points[]{value.center1, value.center2};
            obstacle = b2MakeProxy(points, 2, value.radius);
            break;
        }
        default:
            query.failed = true;
            return false;
        }
        const auto position = b2Body_GetPosition(body->id);
        const auto rotation = b2Body_GetRotation(body->id);
        // Pure narrow-phase work inside the read-only native query callback.
        // No ECS access, allocation, exception or candidate inventory is needed.
        // Subtract double positions before narrowing to the obstacle's frame.
        const b2Vec2 relative{float(result.origin.x - position.x),
                              float(result.origin.y - position.y)};
        const b2ShapeCastPairInput cast{
            obstacle,
            result.mover,
            {b2InvRotateVector(rotation, relative), b2InvMulRot(rotation, b2Rot_identity)},
            b2InvRotateVector(rotation, result.displacement),
            query.hit ? float(query.hit->fraction) : 1,
            true};
        const auto output = b2CastShape(&cast);
        if (output.hit) {
            const auto point = b2RotateVector(rotation, output.point);
            const auto normal = b2RotateVector(rotation, output.normal);
            (void)ray_hit(shape, {position.x + point.x, position.y + point.y}, normal,
                          output.fraction, &query);
        }
        return !query.failed && (!query.hit || query.hit->fraction > 0);
    }
    struct PenetrationQuery {
        const Impl* self;
        PhysicsQueryFilter filter;
        std::optional<EntityRef> ignore;
        b2Capsule capsule;
        b2Pos origin;
        std::size_t work = 0;
        bool failed = false;
        double depth = 0;
        std::size_t* shared_work = nullptr;
    };
    static bool mover_penetration_hit(b2ShapeId shape, void* pointer) noexcept {
        auto& query = *static_cast<PenetrationQuery*>(pointer);
        if (query.failed)
            return false;
        if (!charge_query_work(query.work, query.shared_work)) {
            query.failed = true;
            return false;
        }
        const auto* body = query.self->owned(shape);
        if (!body) {
            query.failed = true;
            return false;
        }
        if ((!query.filter.include_sensors && body->admitted.config.body.sensor) ||
            (query.ignore && body->admitted.ref == *query.ignore))
            return true;
        const auto position = b2Body_GetPosition(body->id);
        const auto rotation = b2Body_GetRotation(body->id);
        const b2Vec2 relative{float(query.origin.x - position.x),
                              float(query.origin.y - position.y)};
        const b2Transform query_in_body{b2InvRotateVector(rotation, relative),
                                        b2InvMulRot(rotation, b2Rot_identity)};
        b2LocalManifold manifold{};
        switch (b2Shape_GetType(shape)) {
        case b2_polygonShape: {
            const auto value = b2Shape_GetPolygon(shape);
            manifold = b2CollidePolygonAndCapsule(&value, &query.capsule, query_in_body);
            break;
        }
        case b2_circleShape: {
            const auto value = b2Shape_GetCircle(shape);
            const b2Transform body_in_query{
                {float(position.x - query.origin.x), float(position.y - query.origin.y)}, rotation};
            manifold = b2CollideCapsuleAndCircle(&query.capsule, &value, body_in_query);
            break;
        }
        case b2_capsuleShape: {
            const auto value = b2Shape_GetCapsule(shape);
            manifold = b2CollideCapsules(&value, &query.capsule, query_in_body);
            break;
        }
        default:
            query.failed = true;
            return false;
        }
        // The overlap query already found intersecting proxies. Missing or
        // invalid manifold data must not become a successful clearance result.
        // A zero normal is valid for measuring depth, never a recovery direction.
        if (manifold.pointCount < 1 || manifold.pointCount > 2 ||
            !std::isfinite(manifold.normal.x) || !std::isfinite(manifold.normal.y)) {
            query.failed = true;
            return false;
        }
        for (int i = 0; i < manifold.pointCount; ++i) {
            const auto& point = manifold.points[i];
            if (!charge_query_work(query.work, query.shared_work) ||
                !std::isfinite(point.point.x) || !std::isfinite(point.point.y) ||
                !std::isfinite(point.separation)) {
                query.failed = true;
                return false;
            }
            query.depth = std::max(query.depth, -double(point.separation));
        }
        return true;
    }
    struct MoverPlanes {
        const Impl* self;
        PhysicsQueryFilter filter;
        std::optional<EntityRef> ignore;
        std::size_t* shared_work;
        std::array<b2CollisionPlane, mover_plane_limit> values{};
        Double2 shift{};
        std::size_t count = 0, work = 0;
        bool failed = false;
        float minimum_floor_y = -1, requested_x = 0;
        bool block_uphill = false;
        double horizontal_limit = 0;
    };
    static bool mover_plane_hit(b2ShapeId shape, const b2PlaneResult* value,
                                void* pointer) noexcept {
        auto& query = *static_cast<MoverPlanes*>(pointer);
        if (query.failed)
            return false;
        if (!charge_query_work(query.work, query.shared_work) || !value || !value->hit) {
            query.failed = true;
            return false;
        }
        const auto* body = query.self->owned(shape);
        if (!body) {
            query.failed = true;
            return false;
        }
        if ((!query.filter.include_sensors && body->admitted.config.body.sensor) ||
            (query.ignore && body->admitted.ref == *query.ignore))
            return true;
        const auto normal = value->plane.normal;
        const double length_squared = double(normal.x) * normal.x + double(normal.y) * normal.y;
        const double offset =
            double(value->plane.offset) + normal.x * query.shift[0] + normal.y * query.shift[1];
        if (!std::isfinite(length_squared) || std::abs(length_squared - 1) > .001 ||
            !std::isfinite(offset) || std::abs(offset) > std::numeric_limits<float>::max() ||
            !std::isfinite(value->point.x) || !std::isfinite(value->point.y)) {
            query.failed = true;
            return false;
        }
        const float converted = float(offset);
        for (std::size_t i = 0; i < query.count; ++i) {
            const auto& previous = query.values[i].plane;
            if (std::abs(previous.normal.x - normal.x) < mover_tolerance &&
                std::abs(previous.normal.y - normal.y) < mover_tolerance &&
                std::abs(previous.offset - converted) < mover_tolerance)
                return true;
        }
        if (query.minimum_floor_y >= 0 && normal.y > mover_tolerance &&
            normal.y < query.minimum_floor_y && normal.x * query.requested_x < 0) {
            if (!query.block_uphill)
                query.horizontal_limit = query.shift[0];
            else if (query.requested_x > 0)
                query.horizontal_limit = std::min(query.horizontal_limit, query.shift[0]);
            else
                query.horizontal_limit = std::max(query.horizontal_limit, query.shift[0]);
            query.block_uphill = true;
        }
        if (query.count == query.values.size()) {
            query.failed = true;
            return false;
        }
        query.values[query.count++] = {
            {normal, converted}, std::numeric_limits<float>::max(), 0, true};
        return true;
    }
    void gather_mover_planes(MoverPlanes& output, const PhysicsOverlap2D& shape, Double2 base,
                             std::uint64_t category) const {
        const auto proxy = overlap_proxy(shape);
        const b2Capsule capsule{proxy.points[0], proxy.points[1], proxy.radius};
        output.shift = {shape.origin[0] - base[0], shape.origin[1] - base[1]};
        b2World_CollideMover(world, {shape.origin[0], shape.origin[1]}, &capsule,
                             query_filter(output.filter, category), mover_plane_hit, &output);
        if (output.failed) {
            if (output.shared_work && *output.shared_work >= query_work_limit)
                throw std::runtime_error("2D mover query work capacity 16384 exceeded");
            if (output.count == output.values.size())
                throw std::runtime_error("2D mover plane capacity 64 exceeded");
            throw std::runtime_error(
                "2D mover planes exceeded their work/plane limit or native binding/result");
        }
    }
    static void check_mover_planes(const MoverPlanes& planes, b2Vec2 value) {
        if (!std::isfinite(value.x) || !std::isfinite(value.y))
            throw std::runtime_error("2D mover returned nonfinite motion");
        for (std::size_t i = 0; i < planes.count; ++i)
            if (b2PlaneSeparation(planes.values[i].plane, value) < -mover_slop - mover_tolerance)
                throw std::runtime_error("2D mover native plane constraints are infeasible");
    }
    static b2Vec2 solve_mover_planes(MoverPlanes& planes, b2Vec2 target) {
        const auto solved = b2SolvePlanes(target, planes.values.data(), int(planes.count));
        if (solved.iterationCount < 0 || solved.iterationCount > 20)
            throw std::runtime_error("2D mover native solver iteration count is invalid");
        check_mover_planes(planes, solved.delta);
        return solved.delta;
    }
    static b2Vec2 clip_mover_motion(const MoverPlanes& planes, b2Vec2 target) {
        const auto clipped = b2ClipVector(target, planes.values.data(), int(planes.count));
        if (!std::isfinite(clipped.x) || !std::isfinite(clipped.y))
            throw std::runtime_error("2D mover native clipping returned nonfinite motion");
        // The native clip is one pass. Oblique opposing planes can reintroduce
        // an earlier blocked direction; stopping is a conservative valid result.
        for (std::size_t i = 0; i < planes.count; ++i) {
            const auto& plane = planes.values[i];
            if (plane.clipVelocity && plane.push > 0 &&
                b2Dot(clipped, plane.plane.normal) < -mover_tolerance)
                return {};
        }
        return clipped;
    }
    struct OverlapQuery {
        const Impl* self;
        PhysicsQueryFilter filter;
        std::size_t work = 0, count = 0;
        bool failed = false;
        std::array<EntityRef, physics_overlap_limit> refs{};
    };
    static bool overlap_hit(b2ShapeId shape, void* pointer) noexcept {
        auto& query = *static_cast<OverlapQuery*>(pointer);
        if (++query.work > query_work_limit) {
            query.failed = true;
            return false;
        }
        const auto* body = query.self->owned(shape);
        if (!body) {
            query.failed = true;
            return false;
        }
        if (!query.filter.include_sensors && body->admitted.config.body.sensor)
            return true;
        const auto end = query.refs.begin() + query.count;
        if (std::find(query.refs.begin(), end, body->admitted.ref) != end)
            return true;
        if (query.count == query.refs.size()) {
            query.failed = true;
            return false;
        }
        query.refs[query.count++] = body->admitted.ref;
        return true;
    }
    std::optional<PhysicsHit2D> cast_mover(const PhysicsSweep2D&, PhysicsQueryFilter,
                                           std::size_t& work,
                                           std::uint64_t category = query_category) const;
    double penetration_mover(const PhysicsOverlap2D&, PhysicsQueryFilter, std::optional<EntityRef>,
                             std::size_t& work, std::uint64_t category = query_category) const;
    MoverMotion2D motion_mover(const PhysicsSweep2D&, PhysicsQueryFilter, std::size_t& work,
                               std::uint64_t category = query_category,
                               float minimum_floor_y = -1) const;
    explicit Impl(const PhysicsConfig& value) : config(value) {}
    ~Impl() {
        if (B2_IS_NON_NULL(world))
            b2DestroyWorld(world);
    }
    void check() const {
        if (fault)
            throw std::runtime_error(
                "2D physics faulted; the partial native step cannot be adopted or continued");
    }
    static bool filter(b2ShapeId, b2ShapeId, void* pointer) noexcept {
        auto& self = *static_cast<Impl*>(pointer);
        if (self.pair_work >= pair_limit) {
            self.overflow = true;
            return false;
        }
        ++self.pair_work;
        return true;
    }
    void realize() {
        if (B2_IS_NON_NULL(world))
            return;
        if (!b2IsDoublePrecision())
            throw std::runtime_error("Private 2D backend precision does not match its headers");
        auto definition = b2DefaultWorldDef();
        definition.gravity = {float(config.gravity[0]), float(config.gravity[1])};
        definition.workerCount = 1;
        world = b2CreateWorld(&definition);
        if (!b2World_IsValid(world))
            throw std::runtime_error("Native 2D world allocation failed");
        b2World_SetCustomFilterCallback(world, filter, this);
    }
    Body create(const Body2DDesired& admitted, const LocalTransform& pose) {
        if (next_epoch >= (std::uint64_t{1} << 60))
            throw std::runtime_error("Private 2D simulation epoch capacity exceeded");
        static constexpr char alphabet[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
        char name[11]{};
        const auto epoch = next_epoch++;
        for (unsigned i = 0; i < 10; ++i)
            name[9 - i] = alphabet[(epoch >> (i * 6)) & 63];
        const auto& config = admitted.config;
        const auto& value = config.body;
        std::vector<b2ShapeId> shapes;
        shapes.reserve(shape_count(config));
        auto definition = b2DefaultBodyDef();
        definition.type = static_cast<b2BodyType>(value.motion);
        definition.position = {pose.translation.x, pose.translation.y};
        definition.rotation = native_rotation(pose.rotation);
        definition.name = name;
        definition.linearDamping = value.linear_damping;
        definition.angularDamping = value.angular_damping;
        definition.gravityScale = value.gravity_factor;
        definition.enableSleep = value.allow_sleeping;
        definition.motionLocks = {value.motion == 2 && value.lock_x,
                                  value.motion == 2 && value.lock_y,
                                  value.motion == 2 && value.lock_rotation};
        definition.isBullet = value.motion == 2 && !value.sensor && value.bullet;
        const auto id = b2CreateBody(world, &definition);
        if (!b2Body_IsValid(id))
            throw std::runtime_error("Native 2D body allocation failed");
        const auto shape = config.shape == 3 ? Geometry{} : geometry(config);
        auto shape_definition = b2DefaultShapeDef();
        shape_definition.density = value.density;
        shape_definition.material.friction = value.friction;
        shape_definition.material.restitution = value.restitution;
        shape_definition.filter.categoryBits = std::uint64_t{1} << value.layer;
        shape_definition.filter.maskBits = std::uint64_t(value.mask) | query_category;
        shape_definition.isSensor = value.sensor;
        shape_definition.enableSensorEvents = true;
        shape_definition.enableContactEvents = true;
        shape_definition.enableCustomFiltering = true;
        for (std::size_t index = 0; index < shape_count(config); ++index) {
            b2ShapeId native{};
            if (config.shape == 3) {
                const auto box = compound_box(config.parts->at(index));
                native = b2CreatePolygonShape(id, &shape_definition, &box);
            } else if (config.shape == 0)
                native = b2CreatePolygonShape(id, &shape_definition, &shape.box);
            else if (config.shape == 1)
                native = b2CreateCircleShape(id, &shape_definition, &shape.circle);
            else
                native = b2CreateCapsuleShape(id, &shape_definition, &shape.capsule);
            if (!b2Shape_IsValid(native)) {
                b2DestroyBody(id);
                throw std::runtime_error("Native 2D shape creation failed");
            }
            shapes.push_back(native);
        }
        if (value.motion == 2 && value.mass > 0)
            b2Body_SetMassData(id, shape.mass);
        return {id, std::move(shapes), admitted, epoch};
    }
};
Native2D::Native2D(const PhysicsConfig& config) : impl_(std::make_unique<Impl>(config)) {}
Native2D::~Native2D() = default;
Json Body2DConfiguration::json() const {
    Json result{
        {"body", detail::encode_builtin_value(body)}, {"shape", shape}, {"dimensions", dimensions}};
    if (shape == 3) {
        validate_geometry(*this);
        result["parts"] = Json::array();
        for (const auto& part : *parts)
            result["parts"].push_back(
                {part.center[0], part.center[1], part.dimensions[0], part.dimensions[1]});
    }
    return result;
}
namespace {
Json recovery_pose(const LocalTransform& pose) {
    return {{"position", {pose.translation.x, pose.translation.y, pose.translation.z}},
            {"rotation", {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w}},
            {"scale", {pose.scale.x, pose.scale.y, pose.scale.z}}};
}
bool recovery_pose_matches(const Json& input, const LocalTransform& pose) {
    for (const auto& [field, count] :
         {std::pair{"position", 3u}, std::pair{"rotation", 4u}, std::pair{"scale", 3u}})
        if (!input.at(field).is_array() || input.at(field).size() != count)
            return false;
    const auto p = input.at("position").get<std::array<double, 3>>();
    const auto q = input.at("rotation").get<std::array<float, 4>>();
    const auto s = input.at("scale").get<std::array<float, 3>>();
    return equivalent(LocalTranslation{p[0], p[1], p[2]}, pose.translation) &&
           equivalent(LocalRotation{q[0], q[1], q[2], q[3]}, pose.rotation) &&
           equivalent(LocalScale{s[0], s[1], s[2]}, pose.scale);
}
bool same_vector(b2Vec2 a, b2Vec2 b) { return a.x == b.x && a.y == b.y; }
std::array<float, 20> polygon_key(const b2Polygon& value) {
    if (value.count != 4)
        throw std::runtime_error("2D compound recovery contains a nonbox polygon");
    std::array<float, 20> key{float(value.count), value.radius, value.centroid.x, value.centroid.y};
    for (unsigned i = 0; i < 4; ++i) {
        key[4 + 4 * i] = value.vertices[i].x;
        key[5 + 4 * i] = value.vertices[i].y;
        key[6 + 4 * i] = value.normals[i].x;
        key[7 + 4 * i] = value.normals[i].y;
    }
    if (std::any_of(key.begin(), key.end(), [](float scalar) { return !std::isfinite(scalar); }))
        throw std::runtime_error("2D compound recovery contains nonfinite polygon geometry");
    return key;
}
// Validate supported public native properties, never native layouts or ID fields.
Json recovery_native_state(b2BodyId id, std::span<const b2ShapeId> shapes,
                           const Body2DConfiguration& config) {
    validate_geometry(config);
    const auto& body = config.body;
    const auto locks = b2Body_GetMotionLocks(id);
    const auto defaults = b2DefaultBodyDef();
    if (shapes.size() != shape_count(config) ||
        b2Body_GetType(id) != static_cast<b2BodyType>(body.motion) || !b2Body_IsEnabled(id) ||
        b2Body_GetShapeCount(id) != int(shapes.size()) ||
        b2Body_GetLinearDamping(id) != body.linear_damping ||
        b2Body_GetAngularDamping(id) != body.angular_damping ||
        b2Body_GetGravityScale(id) != body.gravity_factor ||
        b2Body_IsSleepingEnabled(id) != body.allow_sleeping ||
        locks.linearX != (body.motion == 2 && body.lock_x) ||
        locks.linearY != (body.motion == 2 && body.lock_y) ||
        locks.angularZ != (body.motion == 2 && body.lock_rotation) ||
        b2Body_IsBullet(id) != (body.motion == 2 && !body.sensor && body.bullet) ||
        b2Body_GetSleepThreshold(id) != defaults.sleepThreshold ||
        b2Body_GetSafetyFactor(id) != defaults.safetyFactor ||
        !b2Body_IsContactRecyclingEnabled(id))
        throw std::runtime_error("2D recovery native body configuration mismatch");
    for (const auto shape : shapes) {
        if (!b2Shape_IsValid(shape) || !B2_ID_EQUALS(b2Shape_GetBody(shape), id))
            throw std::runtime_error("2D recovery shape belongs to a different body");
        const auto filter = b2Shape_GetFilter(shape);
        if (b2Shape_IsSensor(shape) != body.sensor || b2Shape_GetDensity(shape) != body.density ||
            b2Shape_GetFriction(shape) != body.friction ||
            b2Shape_GetRestitution(shape) != body.restitution ||
            filter.categoryBits != (std::uint64_t{1} << body.layer) ||
            filter.maskBits != (std::uint64_t(body.mask) | query_category) ||
            filter.groupIndex != 0 || !b2Shape_AreSensorEventsEnabled(shape) ||
            !b2Shape_AreContactEventsEnabled(shape) || b2Shape_ArePreSolveEventsEnabled(shape) ||
            b2Shape_AreHitEventsEnabled(shape))
            throw std::runtime_error("2D recovery native shape configuration mismatch");
    }
    const auto expected = config.shape == 3 ? Geometry{} : geometry(config);
    bool matches = false;
    if (config.shape == 3) {
        std::vector<std::array<float, 20>> actual_parts, expected_parts;
        for (const auto shape : shapes) {
            if (b2Shape_GetType(shape) != b2_polygonShape)
                throw std::runtime_error("2D compound recovery contains a nonpolygon shape");
            actual_parts.push_back(polygon_key(b2Shape_GetPolygon(shape)));
        }
        for (const auto& part : *config.parts)
            expected_parts.push_back(polygon_key(compound_box(part)));
        // Shapes have no authored identities. Compare the complete geometry
        // multiset; native enumeration order need not equal construction order.
        std::sort(actual_parts.begin(), actual_parts.end());
        std::sort(expected_parts.begin(), expected_parts.end());
        matches = actual_parts == expected_parts;
    } else {
        const auto shape = shapes.front();
        if (config.shape == 0 && b2Shape_GetType(shape) == b2_polygonShape) {
            const auto actual = b2Shape_GetPolygon(shape);
            matches = actual.count == expected.box.count && actual.radius == expected.box.radius &&
                      same_vector(actual.centroid, expected.box.centroid);
            for (int i = 0; matches && i < actual.count; ++i)
                matches = same_vector(actual.vertices[i], expected.box.vertices[i]) &&
                          same_vector(actual.normals[i], expected.box.normals[i]);
        } else if (config.shape == 1 && b2Shape_GetType(shape) == b2_circleShape) {
            const auto actual = b2Shape_GetCircle(shape);
            matches = same_vector(actual.center, expected.circle.center) &&
                      actual.radius == expected.circle.radius;
        } else if (config.shape == 2 && b2Shape_GetType(shape) == b2_capsuleShape) {
            const auto actual = b2Shape_GetCapsule(shape);
            matches = same_vector(actual.center1, expected.capsule.center1) &&
                      same_vector(actual.center2, expected.capsule.center2) &&
                      actual.radius == expected.capsule.radius;
        }
    }
    const auto mass = b2Body_GetMassData(id);
    const auto near = [](float a, float b) {
        return std::isfinite(a) && std::abs(double(a) - b) <= 1e-5 * std::max(1e-20, double(b));
    };
    if (!matches || !same_vector(mass.center, {0, 0}) ||
        !near(mass.mass, body.motion == 2 ? expected.mass.mass : 0) ||
        !near(mass.rotationalInertia,
              body.motion == 2 && !body.lock_rotation ? expected.mass.rotationalInertia : 0))
        throw std::runtime_error("2D recovery native geometry/mass mismatch");
    const auto position = b2Body_GetPosition(id);
    const auto rotation = b2Body_GetRotation(id);
    const auto velocity = b2Body_GetLinearVelocity(id);
    const auto angular = b2Body_GetAngularVelocity(id);
    valid_xy({position.x, position.y, 0});
    if (!b2IsValidRot(rotation) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
        !std::isfinite(angular))
        throw std::runtime_error("2D recovery contains invalid native motion");
    return {{"position", {position.x, position.y}},
            {"rotation", {rotation.c, rotation.s}},
            {"velocity", {velocity.x, velocity.y}},
            {"angular", angular},
            {"awake", b2Body_IsAwake(id)}};
}
struct RecoveryShapes {
    std::array<b2ShapeId, shape_limit> shapes{};
    std::size_t count = 0;
    bool overflow = false;
    static bool append(b2ShapeId id, void* pointer) noexcept {
        auto& self = *static_cast<RecoveryShapes*>(pointer);
        if (self.count == self.shapes.size()) {
            self.overflow = true;
            return false;
        }
        self.shapes[self.count++] = id;
        return true;
    }
};
std::uint64_t recovery_epoch(const char* name) {
    static constexpr std::string_view alphabet =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
    std::uint64_t result = 0;
    if (!name)
        throw std::runtime_error("Missing 2D recovery binding tag");
    for (unsigned i = 0; i < 10; ++i) {
        const auto index = alphabet.find(name[i]);
        if (index == std::string_view::npos)
            throw std::runtime_error("Invalid 2D recovery binding tag");
        result = (result << 6) | index;
    }
    if (!result)
        throw std::runtime_error("Zero 2D recovery binding epoch");
    return result;
}
} // namespace
Native2DImage Native2D::checkpoint(const Desired2D& desired, std::size_t remaining,
                                   const DesiredJoints2D& joints) const {
    const auto& self = *impl_;
    self.check();
    validate_step();
    if (!self.retired.empty() || self.pending_end_bound || desired.size() != self.bodies.size())
        throw std::runtime_error("2D checkpoint requires adopted synchronized bodies");
    Native2DImage image;
    if (!realized()) {
        if (!desired.empty() || !joints.empty())
            throw std::runtime_error("2D checkpoint has unrealized bodies");
        return image;
    }
    Json mapping = Json::array();
    for (const auto& [id, body] : self.bodies) {
        const auto found = desired.find(id);
        if (found == desired.end() || found->second.ref != body.admitted.ref ||
            found->second.config != body.admitted.config ||
            !recovery_pose_matches(recovery_pose(body.admitted.pose), found->second.pose))
            throw std::runtime_error("2D checkpoint configuration/pose changed after adoption");
        mapping.push_back(
            {{"entity", body.admitted.ref},
             {"epoch", body.epoch},
             {"configuration", body.admitted.config.json()},
             {"target", recovery_pose(body.admitted.pose)},
             {"native", recovery_native_state(body.id, body.shapes, body.admitted.config)}});
    }
    if (joints.size() != self.joints.size())
        throw std::runtime_error("2D joint checkpoint requires synchronized content");
    std::map<std::uint64_t, const Impl::Joint*> bindings;
    for (const auto& [id, joint] : self.joints) {
        const auto found = joints.find(id);
        if (found == joints.end() || found->second.config != joint.admitted.config ||
            !joint_same_geometry_2d(found->second, joint.admitted) ||
            !joint_matches_2d(joint.id, joint.admitted, self.bodies.at(joint.admitted.a).id,
                              self.bodies.at(joint.admitted.b).id) ||
            !bindings.emplace(b2StoreJointId(joint.id), &joint).second)
            throw std::runtime_error("2D joint checkpoint configuration/binding mismatch");
    }
    Json joint_mapping = Json::array();
    for (const auto native : self.joint_handles()) {
        const auto found = bindings.find(b2StoreJointId(native));
        if (found == bindings.end())
            throw std::runtime_error("Unowned native 2D joint during checkpoint");
        const auto& value = found->second->admitted;
        joint_mapping.push_back({{"entity", value.ref},
                                 {"configuration", joint_value_2d(value.config)},
                                 {"anchor_a", value.anchor_a},
                                 {"anchor_b", value.anchor_b}});
    }
    const int required = b2World_GetSnapshot(self.world, nullptr, 0);
    if (required <= 0 || std::size_t(required) > remaining)
        throw std::runtime_error("Combined 2D/3D checkpoint exceeds solver byte budget");
    image.bytes.resize(std::size_t(required));
    if (b2World_GetSnapshot(self.world, reinterpret_cast<std::uint8_t*>(image.bytes.data()),
                            required) != required)
        throw std::runtime_error("Native 2D checkpoint capture failed");
    image.metadata = {{"pin", "178495ba35695aefe9c321e569d2277f06db3b90-double"},
                      {"next_epoch", self.next_epoch},
                      {"mapping", std::move(mapping)},
                      {"joints", std::move(joint_mapping)}};
    return image;
}
void Native2D::restore(const Json& metadata, const std::string& bytes, const Desired2D& desired,
                       std::uint64_t tick, const DesiredJoints2D& joints) {
    auto& self = *impl_;
    self.check();
    if (realized() || !self.bodies.empty() || !self.joints.empty())
        throw std::runtime_error("2D restoration requires a fresh unpublished owner");
    validate(desired, 0, {}, {}, joints);
    if (metadata.is_null()) {
        if (!bytes.empty() || !desired.empty() || !joints.empty())
            throw std::runtime_error("Missing 2D recovery image");
        return;
    }
    if (metadata.at("pin") != "178495ba35695aefe9c321e569d2277f06db3b90-double" ||
        !metadata.at("next_epoch").is_number_unsigned() || bytes.empty() ||
        bytes.size() > 2 * 1024 * 1024 || !b2IsDoublePrecision())
        throw std::runtime_error("2D recovery pin/size mismatch");
    auto candidate = std::make_unique<Impl>(self.config);
    candidate->next_epoch = metadata.at("next_epoch").get<std::uint64_t>();
    const auto& mapping = metadata.at("mapping");
    const auto& joint_mapping = metadata.at("joints");
    if (!candidate->next_epoch || candidate->next_epoch > (std::uint64_t{1} << 60) ||
        !mapping.is_array() || mapping.size() != desired.size() || desired.size() > body_limit)
        throw std::runtime_error("2D recovery mapping capacity mismatch");
    if (!joint_mapping.is_array() || joint_mapping.size() != joints.size() || joints.size() > 4096)
        throw std::runtime_error("2D recovery joint mapping capacity mismatch");
    std::map<EntityRef, std::uint64_t> entities;
    for (const auto& [id, body] : desired)
        if (!entities.emplace(body.ref, id).second)
            throw std::runtime_error("Duplicate canonical 2D identity");
    std::map<std::uint64_t, std::pair<std::uint64_t, const Json*>> epochs;
    std::set<std::uint64_t> seen;
    for (const auto& entry : mapping) {
        const auto ref = entry.at("entity").get<EntityRef>();
        if (!entry.at("epoch").is_number_unsigned() || !entities.contains(ref))
            throw std::runtime_error("Invalid 2D recovery identity/epoch");
        const auto id = entities.at(ref), epoch = entry.at("epoch").get<std::uint64_t>();
        const auto& body = desired.at(id);
        if (!epoch || epoch >= candidate->next_epoch || !seen.insert(id).second ||
            !epochs.emplace(epoch, std::pair{id, &entry}).second ||
            entry.at("configuration") != body.config.json() ||
            !recovery_pose_matches(entry.at("target"), body.pose))
            throw std::runtime_error("2D recovery configuration/target/binding mismatch");
    }
    candidate->world = b2CreateWorldFromSnapshot(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), int(bytes.size()), 1);
    if (!b2World_IsValid(candidate->world))
        throw std::runtime_error("Native 2D recovery rejected candidate");
    const auto counters = b2World_GetCounters(candidate->world);
    const auto gravity = b2World_GetGravity(candidate->world);
    std::size_t expected_shapes = 0;
    for (const auto& [id, body] : desired)
        expected_shapes += shape_count(body.config);
    if (counters.bodyCount != int(desired.size()) || counters.shapeCount != int(expected_shapes) ||
        counters.jointCount != int(joints.size()) || counters.contactCount < 0 ||
        counters.contactCount > int(pair_limit) || gravity.x != float(self.config.gravity[0]) ||
        gravity.y != float(self.config.gravity[1]) ||
        b2World_GetWorkerCount(candidate->world) != 1 ||
        !b2World_IsContinuousEnabled(candidate->world) ||
        !b2World_IsSleepingEnabled(candidate->world) ||
        b2World_GetMaxLinearSpeed(candidate->world) != b2DefaultWorldDef().maximumLinearSpeed)
        throw std::runtime_error("2D recovery native world profile mismatch");
    RecoveryShapes shapes;
    if (!desired.empty())
        b2World_OverlapAABB(candidate->world, {0, 0}, b2World_GetBounds(candidate->world),
                            query_filter({}), RecoveryShapes::append, &shapes);
    if (shapes.overflow || shapes.count != expected_shapes)
        throw std::runtime_error("2D recovery could not reacquire every shape");
    std::set<std::uint64_t> native_bodies;
    for (std::size_t index = 0; index < shapes.count; ++index) {
        const auto shape = shapes.shapes[index];
        const auto native = b2Shape_GetBody(shape);
        if (!native_bodies.insert(b2StoreBodyId(native)).second)
            continue;
        const auto epoch = recovery_epoch(b2Body_GetName(native));
        const auto found = epochs.find(epoch);
        if (found == epochs.end())
            throw std::runtime_error("Unknown/duplicate native 2D recovery binding");
        const auto [id, entry] = found->second;
        const auto& body = desired.at(id);
        std::vector<b2ShapeId> owned_shapes(shape_count(body.config));
        if (b2Body_GetShapeCount(native) != int(owned_shapes.size()) ||
            b2Body_GetShapes(native, owned_shapes.data(), int(owned_shapes.size())) !=
                int(owned_shapes.size()) ||
            b2Body_GetUserData(native) ||
            std::any_of(owned_shapes.begin(), owned_shapes.end(),
                        [](auto part) { return b2Shape_GetUserData(part) != nullptr; }) ||
            entry->at("native") != recovery_native_state(native, owned_shapes, body.config))
            throw std::runtime_error("2D recovery native binding/state mismatch");
        if (body.config.body.motion == 2) {
            const auto position = b2Body_GetPosition(native);
            if (!equivalent(
                    LocalTranslation{position.x, position.y, 0},
                    LocalTranslation{body.pose.translation.x, body.pose.translation.y, 0}) ||
                !equivalent(copied_rotation(b2Body_GetRotation(native)), body.pose.rotation))
                throw std::runtime_error("2D recovered Dynamic pose differs from adopted source");
        }
        auto& record =
            candidate->bodies.emplace(id, Impl::Body{native, std::move(owned_shapes), body, epoch})
                .first->second;
        b2Body_SetUserData(native, &record);
        for (const auto part : record.shapes)
            b2Shape_SetUserData(part, &record);
        epochs.erase(found);
    }
    if (!epochs.empty())
        throw std::runtime_error("Incomplete 2D recovery bindings");
    // The selected snapshot preserves each body's native joint-list order. Visit
    // bodies by canonical EntityRef so new ECS IDs do not affect that enumeration.
    // Never reuse or rewrite an old world's opaque joint handles.
    std::map<EntityRef, std::uint64_t> joint_entities;
    for (const auto& [id, value] : joints)
        if (!joint_entities.emplace(value.ref, id).second)
            throw std::runtime_error("Duplicate canonical 2D joint identity");
    const auto handles = candidate->joint_handles();
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const auto& entry = joint_mapping.at(index);
        const auto ref = entry.at("entity").get<EntityRef>();
        const auto found = joint_entities.find(ref);
        if (found == joint_entities.end())
            throw std::runtime_error("Unknown/duplicate 2D joint recovery identity");
        const auto id = found->second;
        const auto& value = joints.at(id);
        if (entry.at("configuration") != joint_value_2d(value.config) ||
            entry.at("anchor_a") != Json(value.anchor_a) ||
            entry.at("anchor_b") != Json(value.anchor_b) ||
            !joint_matches_2d(handles[index], value, candidate->bodies.at(value.a).id,
                              candidate->bodies.at(value.b).id))
            throw std::runtime_error("2D joint recovery configuration/native order mismatch");
        candidate->joints.emplace(id, Impl::Joint{handles[index], value});
        joint_entities.erase(found);
    }
    if (!joint_entities.empty())
        throw std::runtime_error("Incomplete 2D joint recovery bindings");
    candidate->last_step_tick = candidate->last_event_tick = tick;
    b2World_SetCustomFilterCallback(candidate->world, Impl::filter, candidate.get());
    impl_.swap(candidate);
}
void Native2D::check() const { impl_->check(); }
std::size_t Native2D::size() const { return impl_->bodies.size(); }
std::size_t Native2D::shape_size() const {
    return realized() ? std::size_t(b2World_GetCounters(impl_->world).shapeCount) : 0;
}
std::size_t Native2D::joint_size() const { return impl_->joints.size(); }
bool Native2D::realized() const { return B2_IS_NON_NULL(impl_->world); }
const Body2DDesired* Native2D::admitted(std::uint64_t id) const {
    impl_->check();
    const auto found = impl_->bodies.find(id);
    return found == impl_->bodies.end() ? nullptr : &found->second.admitted;
}
void Native2D::validate(const Desired2D& desired, float dt,
                        const std::set<std::uint64_t>& commanded,
                        const std::map<std::uint64_t, LocalTransform>& placements,
                        const DesiredJoints2D& joints) const {
    const auto& self = *impl_;
    self.check();
    validate_shape_capacity_2d(desired);
    std::size_t retired_count = 0, ends = self.pending_end_bound;
    for (const auto& [id, body] : self.bodies) {
        const auto next = desired.find(id);
        if (next == desired.end() || next->second.ref != body.admitted.ref ||
            next->second.config != body.admitted.config) {
            retired_count += body.shapes.size();
            ends += std::size_t(b2Body_GetContactCapacity(body.id));
            if (body.admitted.config.body.sensor)
                for (const auto shape : body.shapes)
                    ends += std::size_t(b2Shape_GetSensorCapacity(shape));
        }
    }
    if (joints.size() > 4096)
        throw std::runtime_error("Combined physics joint capacity4096 exceeded");
    // New pair exclusions can end contacts without replacing either body. Count
    // each touching pair once, excluding those already covered by body retirement.
    ends += self.ending_joint_contacts(joints, desired);
    // A step can end its old sensor overlaps and begin its new admitted pairs.
    // Reserve both windows in addition to pending native destruction Ends.
    if (retired_count + self.retired.size() > event_limit || ends > event_limit - 2 * pair_limit)
        throw std::runtime_error(
            "2D pending retirement/event capacity exceeded before native writes");
    for (const auto& [id, value] : desired) {
        const auto old = self.bodies.find(id);
        if (old == self.bodies.end() || old->second.admitted.ref != value.ref ||
            old->second.admitted.config != value.config)
            validate_geometry(value.config);
        const bool prior_kinematic = old != self.bodies.end() &&
                                     old->second.admitted.ref == value.ref &&
                                     old->second.admitted.config.body.motion == 1;
        if (dt > 0 && value.config.body.motion == 1 &&
            (prior_kinematic || placements.contains(id))) {
            const auto position = placements.contains(id) ? b2Pos{placements.at(id).translation.x,
                                                                  placements.at(id).translation.y}
                                                          : b2Body_GetPosition(old->second.id);
            const auto rotation = placements.contains(id)
                                      ? native_rotation(placements.at(id).rotation)
                                      : b2Body_GetRotation(old->second.id);
            const double vx = float(value.pose.translation.x - position.x) / dt;
            const double vy = float(value.pose.translation.y - position.y) / dt;
            const float maximum = realized() ? b2World_GetMaxLinearSpeed(self.world)
                                             : b2DefaultWorldDef().maximumLinearSpeed;
            if (std::hypot(vx, vy) > maximum ||
                std::abs(b2RelativeAngle(rotation, native_rotation(value.pose.rotation))) >
                    B2_MAX_ROTATION)
                throw std::runtime_error("2D Kinematic target exceeds native per-tick "
                                         "speed/rotation limits; use a smaller move or teleport");
        }
        if (old != self.bodies.end() && old->second.admitted.ref == value.ref &&
            old->second.admitted.config.body.motion == 2 && value.config.body.motion == 2 &&
            !commanded.contains(id)) {
            const auto& previous = old->second.admitted.pose;
            if (!equivalent({value.pose.translation.x, value.pose.translation.y, 0},
                            LocalTranslation{previous.translation.x, previous.translation.y, 0}) ||
                !equivalent(value.pose.rotation, previous.rotation))
                throw std::runtime_error(
                    "Dynamic 2D XY/rotation is solver-owned; use an explicit 2D physics command");
        }
    }
}
void Native2D::synchronize(const Desired2D& desired, const Desired2D& initial, float,
                           const std::set<std::uint64_t>& commanded,
                           std::vector<std::uint64_t>& snaps, const DesiredJoints2D& joints) {
    auto& self = *impl_;
    self.check();
    if (desired.empty() && self.bodies.empty())
        return;
    try {
        self.realize();
        self.pending_end_bound += self.ending_joint_contacts(joints, desired);
        self.retire_joints(joints, desired);
        for (auto old = self.bodies.begin(); old != self.bodies.end();) {
            if (!desired.contains(old->first)) {
                self.retire(old->second);
                old = self.bodies.erase(old);
            } else
                ++old;
        }
        for (const auto& [id, value] : desired) {
            auto old = self.bodies.find(id);
            if (old == self.bodies.end() || old->second.admitted.ref != value.ref ||
                old->second.admitted.config != value.config) {
                b2Vec2 linear{};
                float angular = 0;
                bool preserve = false, awake = true;
                auto pose = commanded.contains(id) ? initial.at(id).pose : value.pose;
                if (old != self.bodies.end()) {
                    preserve =
                        old->second.admitted.ref == value.ref &&
                        old->second.admitted.config.body.motion == value.config.body.motion &&
                        value.config.body.motion != 0;
                    if (preserve) {
                        linear = b2Body_GetLinearVelocity(old->second.id);
                        angular = b2Body_GetAngularVelocity(old->second.id);
                        awake = b2Body_IsAwake(old->second.id);
                        const auto position = b2Body_GetPosition(old->second.id);
                        const auto rotation = b2Body_GetRotation(old->second.id);
                        pose.translation.x = position.x;
                        pose.translation.y = position.y;
                        pose.rotation = copied_rotation(rotation);
                    }
                    self.retire(old->second);
                    self.bodies.erase(old);
                }
                auto body = self.create(value, pose);
                if (preserve) {
                    b2Body_SetLinearVelocity(body.id, linear);
                    b2Body_SetAngularVelocity(body.id, angular);
                    if (!awake && value.config.body.allow_sleeping)
                        b2Body_SetAwake(body.id, false);
                }
                old = self.bodies.emplace(id, std::move(body)).first;
                b2Body_SetUserData(old->second.id, &old->second);
                for (const auto shape : old->second.shapes)
                    b2Shape_SetUserData(shape, &old->second);
                snaps.push_back(id);
            }
            old->second.admitted = value;
        }
        self.realize_joints(joints);
    } catch (...) {
        self.fault = true;
        throw;
    }
}
void Native2D::apply(std::uint64_t entity, const BodyCommand& command,
                     std::vector<std::uint64_t>& snaps) {
    auto& self = *impl_;
    self.check();
    const auto& body = self.bodies.at(entity);
    if (body.admitted.ref != command.ref || !command.planar)
        throw std::runtime_error("Admitted 2D command generation/domain mismatch");
    if (command.kind >= BodyCommandKind::Force && command.value == Double3{} &&
        command.kind != BodyCommandKind::LinearVelocity &&
        command.kind != BodyCommandKind::AngularVelocity)
        return;
    const b2Vec2 value{float(command.value[0]), float(command.value[1])};
    const b2Pos point{command.p.x, command.p.y};
    switch (command.kind) {
    case BodyCommandKind::Teleport:
        b2Body_SetTransform(body.id, point, native_rotation(command.q));
        if (command.clear && body.admitted.config.body.motion != 0) {
            b2Body_SetLinearVelocity(body.id, {0, 0});
            b2Body_SetAngularVelocity(body.id, 0);
        }
        snaps.push_back(entity);
        break;
    case BodyCommandKind::Kinematic:
        break; // Final admitted target is applied after FIFO placements/loads.
    case BodyCommandKind::Force:
        if (command.at_point)
            b2Body_ApplyForce(body.id, value, point, true);
        else
            b2Body_ApplyForceToCenter(body.id, value, true);
        break;
    case BodyCommandKind::Impulse:
        if (command.at_point)
            b2Body_ApplyLinearImpulse(body.id, value, point, true);
        else
            b2Body_ApplyLinearImpulseToCenter(body.id, value, true);
        break;
    case BodyCommandKind::Torque:
        b2Body_ApplyTorque(body.id, float(command.value[2]), true);
        break;
    case BodyCommandKind::AngularImpulse:
        b2Body_ApplyAngularImpulse(body.id, float(command.value[2]), true);
        break;
    case BodyCommandKind::LinearVelocity:
        b2Body_SetLinearVelocity(body.id, value);
        break;
    case BodyCommandKind::AngularVelocity:
        b2Body_SetAngularVelocity(body.id, float(command.value[2]));
        break;
    }
}
void Native2D::finish_targets(float dt) {
    auto& self = *impl_;
    self.check();
    for (const auto& [id, body] : self.bodies) {
        const auto& pose = body.admitted.pose;
        if (body.admitted.config.body.motion == 0) {
            const auto position = b2Body_GetPosition(body.id);
            const auto rotation = b2Body_GetRotation(body.id);
            const auto target = native_rotation(pose.rotation);
            if (position.x != pose.translation.x || position.y != pose.translation.y ||
                rotation.c != target.c || rotation.s != target.s)
                b2Body_SetTransform(body.id, {pose.translation.x, pose.translation.y}, target);
        } else if (body.admitted.config.body.motion == 1 && dt > 0)
            b2Body_SetTargetTransform(
                body.id, {{pose.translation.x, pose.translation.y}, native_rotation(pose.rotation)},
                dt, true);
    }
}
std::optional<PhysicsBodyState2D> Native2D::state(std::uint64_t entity, EntityRef ref) const {
    const auto& self = *impl_;
    self.check();
    const auto found = self.bodies.find(entity);
    if (found == self.bodies.end() || found->second.admitted.ref != ref)
        return {};
    const auto& body = found->second;
    const auto position = b2Body_GetPosition(body.id);
    const auto centre = b2Body_GetWorldCenter(body.id);
    const auto velocity = b2Body_GetLinearVelocity(body.id);
    const auto rotation = b2Body_GetRotation(body.id);
    return PhysicsBodyState2D{body.admitted.config.body.motion,
                              b2Body_IsAwake(body.id),
                              body.epoch,
                              {position.x, position.y},
                              {centre.x, centre.y},
                              {velocity.x, velocity.y},
                              std::atan2(double(rotation.s), double(rotation.c)),
                              b2Body_GetAngularVelocity(body.id),
                              b2Body_GetMass(body.id),
                              b2Body_GetRotationalInertia(body.id)};
}
std::optional<Double2> Native2D::point_velocity(std::uint64_t entity, EntityRef ref,
                                                Double2 point) const {
    const auto& self = *impl_;
    self.check();
    const auto found = self.bodies.find(entity);
    if (found == self.bodies.end() || found->second.admitted.ref != ref)
        return {};
    const auto value = b2Body_GetWorldPointVelocity(found->second.id, {point[0], point[1]});
    return Double2{value.x, value.y};
}
std::optional<PhysicsHit2D> Native2D::raycast(Double2 origin, Double2 displacement,
                                              PhysicsQueryFilter filter) const {
    const auto& self = *impl_;
    self.check();
    validate_ray_2d(origin, displacement);
    if (!realized() || !filter.mask)
        return {};
    Impl::RayQuery query{&self, filter};
    b2World_CastRay(self.world, {origin[0], origin[1]},
                    {float(displacement[0]), float(displacement[1])}, query_filter(filter),
                    Impl::ray_hit, &query);
    if (query.failed)
        throw std::runtime_error(
            "2D ray query exceeded its work limit or hit an invalid native binding");
    return query.hit;
}
std::vector<EntityRef> Native2D::overlap(const PhysicsOverlap2D& request,
                                         PhysicsQueryFilter filter) const {
    const auto& self = *impl_;
    self.check();
    const auto proxy = overlap_proxy(request);
    if (!realized() || !filter.mask)
        return {};
    Impl::OverlapQuery query{&self, filter};
    b2World_OverlapShape(self.world, {request.origin[0], request.origin[1]}, &proxy,
                         query_filter(filter), Impl::overlap_hit, &query);
    if (query.failed)
        throw std::runtime_error(
            "2D overlap query exceeded its work/body limit or hit an invalid native binding");
    std::vector<EntityRef> result(query.refs.begin(), query.refs.begin() + query.count);
    std::sort(result.begin(), result.end());
    return result;
}
std::optional<PhysicsHit2D> Native2D::shape_cast(const PhysicsSweep2D& request,
                                                 PhysicsQueryFilter filter) const {
    const auto& self = *impl_;
    self.check();
    const auto proxy = sweep_proxy(request);
    if (!realized() || !filter.mask)
        return {};
    Impl::RayQuery query{&self, filter, request.ignore, request.ignore_initial_overlap};
    b2World_CastShape(self.world, {request.origin[0], request.origin[1]}, &proxy,
                      {float(request.displacement[0]), float(request.displacement[1])},
                      query_filter(filter), Impl::ray_hit, &query);
    if (query.failed)
        throw std::runtime_error(
            "2D sweep exceeded its work limit or hit an invalid native binding");
    return query.hit;
}
std::optional<PhysicsHit2D> Native2D::Impl::cast_mover(const PhysicsSweep2D& request,
                                                       PhysicsQueryFilter filter, std::size_t& work,
                                                       std::uint64_t category) const {
    const auto& self = *this;
    self.check();
    if (request.shape != PhysicsOverlap2D::Shape::Capsule)
        throw std::runtime_error("2D mover cast requires a capsule");
    const auto mover = sweep_proxy(request);
    if (B2_IS_NULL(self.world) || !filter.mask)
        return {};
    const b2Vec2 displacement{float(request.displacement[0]), float(request.displacement[1])};
    // Form bounds in double, then round outwards. Both ends of the native
    // float displacement must be covered at a distant double query origin.
    double min_x = mover.points[0].x, max_x = min_x;
    double min_y = mover.points[0].y, max_y = min_y;
    for (int i = 0; i < mover.count; ++i) {
        const double x = mover.points[i].x, y = mover.points[i].y;
        min_x = std::min({min_x, x, x + displacement.x});
        max_x = std::max({max_x, x, x + displacement.x});
        min_y = std::min({min_y, y, y + displacement.y});
        max_y = std::max({max_y, y, y + displacement.y});
    }
    const auto outward = [](double value, bool upper) {
        return std::nextafter(float(value), upper ? std::numeric_limits<float>::infinity()
                                                  : -std::numeric_limits<float>::infinity());
    };
    const b2AABB bounds{
        {outward(min_x - mover.radius, false), outward(min_y - mover.radius, false)},
        {outward(max_x + mover.radius, true), outward(max_y + mover.radius, true)}};
    Impl::MoverQuery candidates{{&self, filter, request.ignore, request.ignore_initial_overlap},
                                mover,
                                {request.origin[0], request.origin[1]},
                                displacement};
    candidates.query.shared_work = &work;
    b2World_OverlapAABB(self.world, {request.origin[0], request.origin[1]}, bounds,
                        query_filter(filter, category), Impl::mover_candidate, &candidates);
    if (candidates.query.failed) {
        if (work >= query_work_limit)
            throw std::runtime_error("2D mover query work capacity 16384 exceeded");
        throw std::runtime_error(
            "2D mover cast exceeded its work limit or native geometry/binding");
    }
    return candidates.query.hit;
}
double Native2D::Impl::penetration_mover(const PhysicsOverlap2D& request, PhysicsQueryFilter filter,
                                         std::optional<EntityRef> ignore, std::size_t& work,
                                         std::uint64_t category) const {
    const auto& self = *this;
    self.check();
    if (request.shape != PhysicsOverlap2D::Shape::Capsule)
        throw std::runtime_error("2D mover penetration requires a capsule");
    if (ignore && (!ignore->scene || !ignore->entity))
        throw std::runtime_error("2D mover exclusion requires a complete canonical reference");
    const auto proxy = overlap_proxy(request);
    if (B2_IS_NULL(self.world) || !filter.mask)
        return 0;
    Impl::PenetrationQuery query{&self,
                                 filter,
                                 ignore,
                                 {proxy.points[0], proxy.points[1], proxy.radius},
                                 {request.origin[0], request.origin[1]}};
    query.shared_work = &work;
    b2World_OverlapShape(self.world, query.origin, &proxy, query_filter(filter, category),
                         Impl::mover_penetration_hit, &query);
    if (query.failed) {
        if (work >= query_work_limit)
            throw std::runtime_error("2D mover query work capacity 16384 exceeded");
        throw std::runtime_error(
            "2D mover penetration exceeded its work limit or native manifold/binding");
    }
    return query.depth;
}
MoverMotion2D Native2D::Impl::motion_mover(const PhysicsSweep2D& request, PhysicsQueryFilter filter,
                                           std::size_t& work, std::uint64_t category,
                                           float minimum_floor_y) const {
    check();
    if (request.shape != PhysicsOverlap2D::Shape::Capsule || request.ignore_initial_overlap)
        throw std::runtime_error(
            "2D mover motion requires a capsule with checked starting contacts");
    if (!std::isfinite(minimum_floor_y) ||
        (minimum_floor_y != -1 && (minimum_floor_y < 0 || minimum_floor_y > 1)))
        throw std::runtime_error("2D mover floor-normal threshold must be -1 or0..1");
    const auto proxy = sweep_proxy(request);
    if (proxy.radius < .02f || std::hypot(request.displacement[0], request.displacement[1]) > 1000)
        throw std::runtime_error("2D mover motion needs radius>=.02m and displacement<=1000m");
    if (B2_IS_NULL(world) || !filter.mask)
        return {request.displacement, request.displacement, 0};
    PhysicsOverlap2D pose{request.shape, request.dimensions, request.origin, request.angle};
    const auto penetration = [&] {
        return penetration_mover(pose, filter, request.ignore, work, category);
    };
    const double recovery_limit = std::min(.05, double(proxy.radius) * .25);
    if (penetration() > recovery_limit + mover_tolerance)
        throw std::runtime_error("2D mover starts too deeply embedded for shallow recovery");
    MoverPlanes recovery_planes{this, filter, request.ignore, &work};
    gather_mover_planes(recovery_planes, pose, pose.origin, category);
    const auto recovery = solve_mover_planes(recovery_planes, {});
    pose.origin = {request.origin[0] + recovery.x, request.origin[1] + recovery.y};
    if (penetration() > mover_slop + mover_tolerance)
        throw std::runtime_error("2D mover shallow recovery did not produce a feasible pose");
    const auto base = pose.origin;
    MoverPlanes planes{this, filter, request.ignore, &work};
    planes.minimum_floor_y = request.displacement[1] <= 0 ? minimum_floor_y : -1;
    planes.requested_x = float(request.displacement[0]);
    gather_mover_planes(planes, pose, base, category);
    const b2Vec2 requested{float(request.displacement[0]), float(request.displacement[1])};
    MoverMotion2D result{{recovery.x, recovery.y}, {}, 0};
    for (unsigned iteration = 0; iteration < mover_iteration_limit; ++iteration) {
        auto target = requested;
        if (planes.block_uphill)
            target.x = requested.x > 0 ? std::min(target.x, float(planes.horizontal_limit))
                                       : std::max(target.x, float(planes.horizontal_limit));
        const auto solved = solve_mover_planes(planes, target);
        auto cast = request;
        cast.origin = base;
        cast.displacement = {solved.x, solved.y};
        // Starting geometry was checked independently; retained planes constrain
        // those shallow contacts while the cast finds new obstacles.
        cast.ignore_initial_overlap = true;
        const auto hit = cast_mover(cast, filter, work, category);
        const float fraction = hit ? float(hit->fraction) : 1;
        const b2Vec2 candidate{solved.x * fraction, solved.y * fraction};
        check_mover_planes(planes, candidate);
        pose.origin = {base[0] + candidate.x, base[1] + candidate.y};
        if (penetration() > mover_slop + mover_tolerance)
            throw std::runtime_error("2D mover cast endpoint remains embedded");
        // Keep approach travel, then remove uphill horizontal intent. Downward
        // motion can still project downhill along the physical surface.
        auto intent = requested;
        if (planes.block_uphill)
            intent.x = 0;
        const auto clipped = clip_mover_motion(planes, intent);
        result = {{double(recovery.x) + candidate.x, double(recovery.y) + candidate.y},
                  {clipped.x, clipped.y},
                  iteration + 1};
        if (!hit || fraction == 1)
            return result;
        const auto prior_count = planes.count;
        gather_mover_planes(planes, pose, base, category);
        check_mover_planes(planes, candidate);
        // Float cast fractions can stop just short of a known contact. Keep
        // its already checked tangent motion when no new constraint appears.
        if (planes.count == prior_count)
            return result;
    }
    // Iteration exhaustion discards remaining motion at the last independently
    // verified safe endpoint. An infeasible native solve never reaches here.
    result.clipped_displacement = {};
    return result;
}
std::optional<PhysicsHit2D> Native2D::mover_cast(const PhysicsSweep2D& request,
                                                 PhysicsQueryFilter filter) const {
    std::size_t work = 0;
    return impl_->cast_mover(request, filter, work);
}
double Native2D::mover_penetration(const PhysicsOverlap2D& request, PhysicsQueryFilter filter,
                                   std::optional<EntityRef> ignore) const {
    std::size_t work = 0;
    return impl_->penetration_mover(request, filter, ignore, work);
}
MoverMotion2D Native2D::mover_motion(const PhysicsSweep2D& request, PhysicsQueryFilter filter,
                                     float minimum_floor_y) const {
    std::size_t work = 0;
    return impl_->motion_mover(request, filter, work, query_category, minimum_floor_y);
}
bool Native2D::current(std::uint64_t entity, EntityRef ref, std::uint64_t epoch) const {
    impl_->check();
    const auto found = impl_->bodies.find(entity);
    return found != impl_->bodies.end() && found->second.admitted.ref == ref &&
           found->second.epoch == epoch;
}
bool Native2D::contains(std::uint64_t entity, EntityRef ref) const {
    impl_->check();
    const auto found = impl_->bodies.find(entity);
    return found != impl_->bodies.end() && found->second.admitted.ref == ref;
}
void Native2D::expire_events() {
    impl_->check();
    impl_->events.clear();
}
std::size_t Native2D::event_count() const {
    impl_->check();
    return impl_->events.size();
}
std::optional<PhysicsContact2D> Native2D::event(std::size_t index) const {
    impl_->check();
    if (index >= impl_->events.size())
        return {};
    return impl_->events[index];
}
void Native2D::validate_step() const {
    impl_->check();
    if (impl_->last_step_tick != impl_->last_event_tick)
        throw std::runtime_error("Adopt the completed 2D fixed step before starting another");
}
void Native2D::adopt_events(std::uint64_t tick) {
    auto& self = *impl_;
    self.check();
    if (!realized() || self.last_step_tick != tick || self.last_event_tick == tick)
        return;
    try {
        const auto contacts = b2World_GetContactEvents(self.world);
        const auto sensors = b2World_GetSensorEvents(self.world);
        if (contacts.beginCount < 0 || contacts.endCount < 0 || sensors.beginCount < 0 ||
            sensors.endCount < 0 ||
            std::uint64_t(contacts.beginCount) + contacts.endCount + sensors.beginCount +
                    sensors.endCount >
                event_limit)
            throw std::runtime_error("2D copied event capacity 16384 exceeded");
        std::vector<PhysicsContact2D> next;
        next.reserve(std::size_t(contacts.beginCount) + contacts.endCount + sensors.beginCount +
                     sensors.endCount);
        auto append = [&](b2ShapeId a, b2ShapeId b, bool begin, bool sensor) {
            auto first = self.binding(a), second = self.binding(b);
            if (!sensor && std::tie(first.ref, first.epoch) > std::tie(second.ref, second.epoch))
                std::swap(first, second);
            next.push_back({first.ref, second.ref, first.epoch, second.epoch, tick, begin, sensor,
                            false, false});
        };
        for (int i = 0; i < contacts.beginCount; ++i)
            append(contacts.beginEvents[i].shapeIdA, contacts.beginEvents[i].shapeIdB, true, false);
        for (int i = 0; i < contacts.endCount; ++i)
            append(contacts.endEvents[i].shapeIdA, contacts.endEvents[i].shapeIdB, false, false);
        for (int i = 0; i < sensors.beginCount; ++i)
            append(sensors.beginEvents[i].sensorShapeId, sensors.beginEvents[i].visitorShapeId,
                   true, true);
        for (int i = 0; i < sensors.endCount; ++i)
            append(sensors.endEvents[i].sensorShapeId, sensors.endEvents[i].visitorShapeId, false,
                   true);
        std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) {
            return std::tie(a.sensor, a.first, a.first_epoch, a.second, a.second_epoch, a.begin) <
                   std::tie(b.sensor, b.first, b.first_epoch, b.second, b.second_epoch, b.begin);
        });
        self.events.swap(next);
        self.retired.clear();
        self.pending_end_bound = 0;
        self.last_event_tick = tick;
    } catch (...) {
        self.fault = true;
        throw;
    }
}
void Native2D::step(float dt, std::uint64_t tick) {
    auto& self = *impl_;
    self.check();
    validate_step();
    if (!realized())
        return;
    self.pair_work = std::size_t(b2World_GetCounters(self.world).contactCount);
    // Sensor overlaps are rebuilt by the native sensor query each step and are
    // already charged by the custom callback. Reserve only retained solid
    // contacts here; charging retained sensors again rejects unchanged worlds.
    self.overflow = self.pair_work > pair_limit;
    if (!self.overflow)
        b2World_Step(self.world, dt, 4);
    if (self.overflow || std::size_t(b2World_GetCounters(self.world).contactCount) > pair_limit) {
        self.fault = true;
        throw std::runtime_error(
            "2D pair work capacity 4096 exceeded; partial native step rejected");
    }
    for (const auto& [id, joint] : self.joints) {
        const auto force = b2Joint_GetConstraintForce(joint.id);
        const auto torque = b2Joint_GetConstraintTorque(joint.id);
        if (!std::isfinite(force.x) || !std::isfinite(force.y) || !std::isfinite(torque)) {
            self.fault = true;
            throw std::runtime_error("Nonfinite native 2D joint reaction; step cannot be adopted");
        }
    }
    self.last_step_tick = tick;
}
std::map<std::uint64_t, LocalTransform> Native2D::adopted_poses() const {
    const auto& self = *impl_;
    self.check();
    std::map<std::uint64_t, LocalTransform> result;
    try {
        for (const auto& [id, body] : self.bodies)
            if (body.admitted.config.body.motion == 2) {
                const auto position = b2Body_GetPosition(body.id);
                const auto rotation = b2Body_GetRotation(body.id);
                auto pose = body.admitted.pose;
                pose.translation.x = position.x;
                pose.translation.y = position.y;
                valid_xy(pose.translation);
                pose.rotation = copied_rotation(rotation);
                result.emplace(id, pose);
            }
    } catch (...) {
        impl_->fault = true;
        throw;
    }
    return result;
}
void Native2D::accept_poses(const std::map<std::uint64_t, LocalTransform>& poses) {
    for (const auto& [id, pose] : poses)
        impl_->bodies.at(id).admitted.pose = pose;
}
} // namespace forge::physics_detail
