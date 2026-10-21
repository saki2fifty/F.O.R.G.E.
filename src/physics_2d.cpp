#include "physics_2d.hpp"
#include "builtins.hpp"
#include "physics_2d_transform.hpp"
#include <algorithm>
#include <box2d/box2d.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace forge::physics_detail {
namespace {
constexpr std::size_t body_limit = 1024, pair_limit = 4096;
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
} // namespace

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
        shape_definition.filter.maskBits = value.mask;
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
void Native2D::check() const { impl_->check(); }
std::size_t Native2D::size() const { return impl_->bodies.size(); }
bool Native2D::realized() const { return B2_IS_NON_NULL(impl_->world); }
void Native2D::validate(const Desired2D& desired, float dt,
                        const std::set<std::uint64_t>& commanded,
                        const std::map<std::uint64_t, LocalTransform>& placements) const {
    const auto& self = *impl_;
    self.check();
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
                b2DestroyBody(old->second.id);
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
                    b2DestroyBody(old->second.id);
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
void Native2D::step(float dt) {
    auto& self = *impl_;
    self.check();
    if (!realized())
        return;
    self.pair_work = std::size_t(b2World_GetCounters(self.world).contactCount);
    for (const auto& [id, body] : self.bodies)
        if (body.admitted.config.body.sensor)
            self.pair_work += std::size_t(b2Shape_GetSensorCapacity(body.shape));
    self.overflow = self.pair_work > pair_limit;
    if (!self.overflow)
        b2World_Step(self.world, dt, 4);
    if (self.overflow || std::size_t(b2World_GetCounters(self.world).contactCount) > pair_limit) {
        self.fault = true;
        throw std::runtime_error(
            "2D pair work capacity 4096 exceeded; partial native step rejected");
    }
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
