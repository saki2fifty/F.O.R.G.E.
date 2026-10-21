#include "physics_2d.hpp"
#include "builtins.hpp"
#include "physics_2d_transform.hpp"
#include <algorithm>
#include <box2d/box2d.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace forge::physics_detail {
namespace {
constexpr std::size_t body_limit = 1024, pair_limit = 4096;
constexpr std::size_t query_work_limit = 16384, event_limit = 16384;
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
    (void)PlanarMapping(affine);
    const auto result = decompose(affine, &hint);
    planar_rotation(result.rotation);
    valid_xy(result.translation);
    return result;
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
b2QueryFilter query_filter(PhysicsQueryFilter value) {
    auto result = b2DefaultQueryFilter();
    result.categoryBits = query_category;
    result.maskBits = value.mask;
    return result;
}
} // namespace

void validate_ray_2d(Double2 origin, Double2 displacement) {
    valid_xy({origin[0], origin[1], 0});
    valid_xy({origin[0] + displacement[0], origin[1] + displacement[1], 0});
    if (!std::isfinite(displacement[0]) || !std::isfinite(displacement[1]) ||
        (displacement[0] == 0 && displacement[1] == 0))
        throw std::runtime_error("2D ray displacement must be finite and nonzero");
}
void validate_overlap_2d(const PhysicsOverlap2D& query) { (void)overlap_proxy(query); }

Desired2D desired_bodies_2d(WorldContext& context, const PhysicsConfig& settings,
                            const std::map<std::uint64_t, TransformNode>& nodes) {
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
                unsigned(entity.has<CapsuleCollider2D>()) !=
            1)
            throw std::runtime_error(
                "2D body requires exactly one Box, Circle or Capsule Collider 2D");
        Body2DConfiguration config{body};
        const double x = std::abs(pose.scale.x), y = std::abs(pose.scale.y);
        if (entity.has<BoxCollider2D>()) {
            const auto value = entity.get<BoxCollider2D>();
            detail::validate_reflected_value(entity, value);
            config.dimensions = {dimension(value.x * x, .01f, 1000),
                                 dimension(value.y * y, .01f, 1000)};
        } else {
            if (std::abs(x - y) > 1e-6 * std::max(x, y))
                throw std::runtime_error(
                    "Circle/Capsule Collider 2D requires equal absolute XY scale");
            config.shape = 1;
            if (entity.has<CircleCollider2D>()) {
                const auto value = entity.get<CircleCollider2D>();
                detail::validate_reflected_value(entity, value);
                config.dimensions[0] = dimension(value.radius * x, .005f, 500);
            } else {
                const auto value = entity.get<CapsuleCollider2D>();
                detail::validate_reflected_value(entity, value);
                config.shape = 2;
                config.dimensions = {dimension(value.radius * x, .005f, 500),
                                     dimension(value.height * y, .01f, 1000)};
                if (double(config.dimensions[1]) + 2 * double(config.dimensions[0]) > 1000)
                    throw std::runtime_error("2D capsule total height exceeds 1000 metres");
            }
        }
        (void)geometry(config);
        result.emplace(id, Body2DDesired{*ref, config, pose});
    }
    return result;
}

struct Native2D::Impl {
    PhysicsConfig config;
    b2WorldId world{};
    bool fault = false, overflow = false;
    std::size_t pair_work = 0;
    std::uint64_t next_epoch = 1;
    struct Body {
        b2BodyId id{};
        b2ShapeId shape{};
        Body2DDesired admitted;
        std::uint64_t epoch = 0;
    };
    std::map<std::uint64_t, Body> bodies;
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
        if (body.admitted.config.body.sensor)
            pending_end_bound += std::size_t(b2Shape_GetSensorCapacity(body.shape));
        if (!retired.emplace(b2StoreShapeId(body.shape), Binding{body.admitted.ref, body.epoch})
                 .second)
            throw std::runtime_error("2D retired-shape generation alias");
        b2DestroyBody(body.id);
    }
    const Body* owned(b2ShapeId shape) const noexcept {
        if (!b2Shape_IsValid(shape))
            return nullptr;
        const auto* body = static_cast<const Body*>(b2Shape_GetUserData(shape));
        return body && B2_ID_EQUALS(body->shape, shape) ? body : nullptr;
    }
    Binding binding(b2ShapeId shape) const {
        if (const auto* body = owned(shape))
            return {body->admitted.ref, body->epoch};
        const auto old = retired.find(b2StoreShapeId(shape));
        if (old != retired.end())
            return old->second;
        throw std::runtime_error("2D event has no current or retained native shape binding");
    }
    struct RayQuery {
        const Impl* self;
        PhysicsQueryFilter filter;
        std::size_t work = 0;
        bool failed = false;
        std::optional<PhysicsHit2D> hit;
    };
    static float ray_hit(b2ShapeId shape, b2Pos point, b2Vec2 normal, float fraction,
                         void* pointer) noexcept {
        auto& query = *static_cast<RayQuery*>(pointer);
        if (++query.work > query_work_limit) {
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
        if (!query.filter.include_sensors && body->admitted.config.body.sensor)
            return -1;
        if (!query.hit || fraction < query.hit->fraction)
            query.hit = PhysicsHit2D{
                body->admitted.ref, {point.x, point.y}, {normal.x, normal.y}, fraction};
        return float(query.hit->fraction);
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
        const auto shape = geometry(config);
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
        b2ShapeId native{};
        if (config.shape == 0)
            native = b2CreatePolygonShape(id, &shape_definition, &shape.box);
        else if (config.shape == 1)
            native = b2CreateCircleShape(id, &shape_definition, &shape.circle);
        else
            native = b2CreateCapsuleShape(id, &shape_definition, &shape.capsule);
        if (!b2Shape_IsValid(native)) {
            b2DestroyBody(id);
            throw std::runtime_error("Native 2D inline shape creation failed");
        }
        if (value.motion == 2 && value.mass > 0)
            b2Body_SetMassData(id, shape.mass);
        return {id, native, admitted, epoch};
    }
};
Native2D::Native2D(const PhysicsConfig& config) : impl_(std::make_unique<Impl>(config)) {}
Native2D::~Native2D() = default;
Json Body2DConfiguration::json() const {
    return {
        {"body", detail::encode_builtin_value(body)}, {"shape", shape}, {"dimensions", dimensions}};
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
// Validate supported public native properties, never native layouts or ID fields.
Json recovery_native_state(b2BodyId id, b2ShapeId shape, const Body2DConfiguration& config) {
    const auto& body = config.body;
    const auto locks = b2Body_GetMotionLocks(id);
    const auto filter = b2Shape_GetFilter(shape);
    const auto defaults = b2DefaultBodyDef();
    if (b2Body_GetType(id) != static_cast<b2BodyType>(body.motion) || !b2Body_IsEnabled(id) ||
        b2Body_GetShapeCount(id) != 1 || b2Body_GetLinearDamping(id) != body.linear_damping ||
        b2Body_GetAngularDamping(id) != body.angular_damping ||
        b2Body_GetGravityScale(id) != body.gravity_factor ||
        b2Body_IsSleepingEnabled(id) != body.allow_sleeping ||
        locks.linearX != (body.motion == 2 && body.lock_x) ||
        locks.linearY != (body.motion == 2 && body.lock_y) ||
        locks.angularZ != (body.motion == 2 && body.lock_rotation) ||
        b2Body_IsBullet(id) != (body.motion == 2 && !body.sensor && body.bullet) ||
        b2Body_GetSleepThreshold(id) != defaults.sleepThreshold ||
        b2Body_GetSafetyFactor(id) != defaults.safetyFactor ||
        !b2Body_IsContactRecyclingEnabled(id) || b2Shape_IsSensor(shape) != body.sensor ||
        b2Shape_GetDensity(shape) != body.density || b2Shape_GetFriction(shape) != body.friction ||
        b2Shape_GetRestitution(shape) != body.restitution ||
        filter.categoryBits != (std::uint64_t{1} << body.layer) ||
        filter.maskBits != (std::uint64_t(body.mask) | query_category) || filter.groupIndex != 0 ||
        !b2Shape_AreSensorEventsEnabled(shape) || !b2Shape_AreContactEventsEnabled(shape) ||
        b2Shape_ArePreSolveEventsEnabled(shape) || b2Shape_AreHitEventsEnabled(shape))
        throw std::runtime_error("2D recovery native body configuration mismatch");
    const auto expected = geometry(config);
    bool matches = false;
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
    std::array<b2ShapeId, body_limit> shapes{};
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
Native2DImage Native2D::checkpoint(const Desired2D& desired, std::size_t remaining) const {
    const auto& self = *impl_;
    self.check();
    validate_step();
    if (!self.retired.empty() || self.pending_end_bound || desired.size() != self.bodies.size())
        throw std::runtime_error("2D checkpoint requires adopted synchronized bodies");
    Native2DImage image;
    if (!realized()) {
        if (!desired.empty())
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
             {"native", recovery_native_state(body.id, body.shape, body.admitted.config)}});
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
                      {"mapping", std::move(mapping)}};
    return image;
}
void Native2D::restore(const Json& metadata, const std::string& bytes, const Desired2D& desired,
                       std::uint64_t tick) {
    auto& self = *impl_;
    self.check();
    if (realized() || !self.bodies.empty())
        throw std::runtime_error("2D restoration requires a fresh unpublished owner");
    if (metadata.is_null()) {
        if (!bytes.empty() || !desired.empty())
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
    if (!candidate->next_epoch || candidate->next_epoch > (std::uint64_t{1} << 60) ||
        !mapping.is_array() || mapping.size() != desired.size() || desired.size() > body_limit)
        throw std::runtime_error("2D recovery mapping capacity mismatch");
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
    if (counters.bodyCount != int(desired.size()) || counters.shapeCount != counters.bodyCount ||
        counters.jointCount != 0 || counters.contactCount < 0 ||
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
    if (shapes.overflow || shapes.count != desired.size())
        throw std::runtime_error("2D recovery could not reacquire every shape");
    for (std::size_t index = 0; index < shapes.count; ++index) {
        const auto shape = shapes.shapes[index];
        const auto native = b2Shape_GetBody(shape);
        const auto epoch = recovery_epoch(b2Body_GetName(native));
        const auto found = epochs.find(epoch);
        if (found == epochs.end())
            throw std::runtime_error("Unknown/duplicate native 2D recovery binding");
        const auto [id, entry] = found->second;
        const auto& body = desired.at(id);
        b2ShapeId only{};
        if (b2Body_GetShapes(native, &only, 1) != 1 || !B2_ID_EQUALS(only, shape) ||
            b2Body_GetUserData(native) || b2Shape_GetUserData(shape) ||
            entry->at("native") != recovery_native_state(native, shape, body.config))
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
            candidate->bodies.emplace(id, Impl::Body{native, shape, body, epoch}).first->second;
        b2Body_SetUserData(native, &record);
        b2Shape_SetUserData(shape, &record);
        epochs.erase(found);
    }
    if (!epochs.empty())
        throw std::runtime_error("Incomplete 2D recovery bindings");
    candidate->last_step_tick = candidate->last_event_tick = tick;
    b2World_SetCustomFilterCallback(candidate->world, Impl::filter, candidate.get());
    impl_.swap(candidate);
}
void Native2D::check() const { impl_->check(); }
std::size_t Native2D::size() const { return impl_->bodies.size(); }
bool Native2D::realized() const { return B2_IS_NON_NULL(impl_->world); }
void Native2D::validate(const Desired2D& desired, float dt,
                        const std::set<std::uint64_t>& commanded,
                        const std::map<std::uint64_t, LocalTransform>& placements) const {
    const auto& self = *impl_;
    self.check();
    std::size_t retired_count = 0, ends = self.pending_end_bound;
    for (const auto& [id, body] : self.bodies) {
        const auto next = desired.find(id);
        if (next == desired.end() || next->second.ref != body.admitted.ref ||
            next->second.config != body.admitted.config) {
            ++retired_count;
            ends += std::size_t(b2Body_GetContactCapacity(body.id));
            if (body.admitted.config.body.sensor)
                ends += std::size_t(b2Shape_GetSensorCapacity(body.shape));
        }
    }
    // A step can end its old sensor overlaps and begin its new admitted pairs.
    // Reserve both windows in addition to pending native destruction Ends.
    if (retired_count + self.retired.size() > event_limit || ends > event_limit - 2 * pair_limit)
        throw std::runtime_error(
            "2D pending retirement/event capacity exceeded before native writes");
    for (const auto& [id, value] : desired) {
        (void)geometry(value.config);
        const auto old = self.bodies.find(id);
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
                           std::vector<std::uint64_t>& snaps) {
    auto& self = *impl_;
    self.check();
    if (desired.empty() && self.bodies.empty())
        return;
    try {
        self.realize();
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
                b2Shape_SetUserData(old->second.shape, &old->second);
                snaps.push_back(id);
            }
            old->second.admitted = value;
        }
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
