// Jolt requires its umbrella header before all other Jolt headers.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "builtins.hpp"
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <forge/build.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/physics.hpp>
#include <mutex>
#include <set>
#include <thread>
namespace forge {
namespace {
constexpr unsigned max_bodies = 8192;
constexpr std::size_t max_solver_bytes = 2 * 1024 * 1024;
// One process registration lease shared by all explicitly composed physics worlds.
struct Registration {
    Registration() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory;
        JPH::RegisterTypes();
    }
    ~Registration() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
std::shared_ptr<Registration> registration() {
    static std::mutex mutex;
    static std::weak_ptr<Registration> current;
    std::lock_guard lock(mutex);
    auto result = current.lock();
    if (!result) {
        result = std::make_shared<Registration>();
        current = result;
    }
    return result;
}
struct Layers : JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(layer);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "Static" : "Moving";
    }
#endif
};
struct BroadFilter : JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a != 0 || b.GetValue() != 0;
    }
};
struct PairFilter : JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != 0 || b != 0;
    }
};
JPH::RVec3 position(LocalTranslation p) { return {p.x, p.y, p.z}; }
JPH::Quat rotation(LocalRotation q) {
    q = normalized(q);
    return {q.x, q.y, q.z, q.w};
}
void valid_position(LocalTranslation p) {
    for (double v : {p.x, p.y, p.z})
        if (!std::isfinite(v) || std::abs(v) > 1e9)
            throw std::runtime_error("Physics position exceeds supported +/-1 billion meters");
}
struct Configuration {
    PhysicsBody body;
    unsigned shape{};
    Double3 dimensions{};
    LocalScale scale;
    bool operator==(const Configuration&) const = default;
    Json json() const {
        return {{"motion", body.motion},
                {"density", body.density},
                {"mass", body.mass},
                {"friction", body.friction},
                {"restitution", body.restitution},
                {"gravity_factor", body.gravity_factor},
                {"shape", shape},
                {"dimensions", dimensions},
                {"scale", {scale.x, scale.y, scale.z}}};
    }
};
Configuration configuration(flecs::entity e, LocalScale scale) {
    auto b = e.get<PhysicsBody>();
    detail::validate_reflected_value(e, b);
    if (b.motion == 2 &&
        (!e.has<SpatialBinding>() || e.get<SpatialBinding>().mode != SpatialMode::World))
        throw std::runtime_error("Dynamic Physics Body requires Child space: World");
    if (unsigned(e.has<BoxCollider>()) + unsigned(e.has<SphereCollider>()) +
            unsigned(e.has<CapsuleCollider>()) !=
        1)
        throw std::runtime_error(
            "Physics Body requires exactly one Box, Sphere or Capsule Collider");
    Configuration c{b, 0, {}, scale};
    if (e.has<BoxCollider>()) {
        auto v = e.get<BoxCollider>();
        detail::validate_reflected_value(e, v);
        c.dimensions = {v.x, v.y, v.z};
    } else if (e.has<SphereCollider>()) {
        detail::validate_reflected_value(e, e.get<SphereCollider>());
        c.shape = 1;
        c.dimensions.fill(e.get<SphereCollider>().radius);
    } else {
        c.shape = 2;
        auto v = e.get<CapsuleCollider>();
        detail::validate_reflected_value(e, v);
        c.dimensions = {v.radius, v.height, v.radius};
    }
    for (unsigned i = 0; i < 3; ++i) {
        const double scaled = c.dimensions[i] * std::array<float, 3>{scale.x, scale.y, scale.z}[i];
        if (!std::isfinite(scaled) || scaled < .000999999 || scaled > 10000)
            throw std::runtime_error("Scaled collider dimensions must remain .001..10000 meters");
    }
    if (c.shape && (std::abs(scale.x - scale.y) > 1e-5f || std::abs(scale.x - scale.z) > 1e-5f))
        throw std::runtime_error(
            "Sphere and Capsule Colliders require uniform positive world scale");
    return c;
}
JPH::RefConst<JPH::Shape> shape(const Configuration& c) {
    JPH::ShapeSettings::ShapeResult result;
    if (c.shape == 0) {
        JPH::Vec3 half(float(c.dimensions[0] * c.scale.x * .5),
                       float(c.dimensions[1] * c.scale.y * .5),
                       float(c.dimensions[2] * c.scale.z * .5));
        JPH::BoxShapeSettings s(half, std::min(.05f, half.ReduceMin() * .1f));
        s.mDensity = c.body.density;
        result = s.Create();
    } else if (c.shape == 1) {
        JPH::SphereShapeSettings s(float(c.dimensions[0] * c.scale.x));
        s.mDensity = c.body.density;
        result = s.Create();
    } else {
        JPH::CapsuleShapeSettings s(float(c.dimensions[1] * c.scale.y * .5),
                                    float(c.dimensions[0] * c.scale.x));
        s.mDensity = c.body.density;
        result = s.Create();
    }
    if (result.HasError())
        throw std::runtime_error("Jolt collider: " + std::string(result.GetError().c_str()));
    return result.Get();
}
std::string hex(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
std::string unhex(const std::string& text) {
    if (text.size() % 2 || text.size() > 2 * max_solver_bytes)
        throw std::runtime_error("Invalid physics checkpoint size");
    std::string out;
    out.reserve(text.size() / 2);
    auto digit = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("Invalid physics checkpoint encoding");
    };
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += char(digit(text[i]) * 16 + digit(text[i + 1]));
    return out;
}
// Corruption detection for a private same-process-family transport, not authentication.
std::string digest(const std::string& text) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return std::to_string(h);
}
} // namespace
struct PhysicsRuntime::Impl {
    WorldContext& context;
    PhysicsConfig config;
    bool active = true;
    std::thread::id thread = std::this_thread::get_id();
    std::shared_ptr<Registration> lease = registration();
    Layers layers;
    BroadFilter broad;
    PairFilter pairs;
    JPH::TempAllocatorImpl allocator{32 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                  int(std::clamp(std::thread::hardware_concurrency(), 2u, 5u) - 1)};
    struct Listener : JPH::ContactListener {
        struct Event {
            std::uint32_t first, second;
            bool begin;
        };
        std::mutex mutex;
        std::vector<Event> events;
        bool overflow = false;
        void append(JPH::BodyID a, JPH::BodyID b, bool begin) {
            std::lock_guard lock(mutex);
            if (events.size() < 16384)
                events.push_back(
                    {a.GetIndexAndSequenceNumber(), b.GetIndexAndSequenceNumber(), begin});
            else
                overflow = true;
        }
        void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&,
                            JPH::ContactSettings&) override {
            append(a.GetID(), b.GetID(), true);
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            append(pair.GetBody1ID(), pair.GetBody2ID(), false);
        }
    } listener;
    JPH::PhysicsSystem system;
    struct Body {
        JPH::BodyID id;
        EntityRef ref;
        Configuration config;
        LocalTransform last;
    };
    std::map<std::uint64_t, Body> bodies;
    struct Command {
        EntityRef ref;
        LocalTranslation p;
        LocalRotation q;
        bool teleport, clear;
    };
    std::vector<Command> commands;
    std::vector<std::uint64_t> snaps;
    std::vector<PhysicsContact> events;
    std::uint64_t tick = 0;
    explicit Impl(WorldContext& c, PhysicsConfig settings) : context(c), config(settings) {
        config.validate();
        system.Init(max_bodies, 0, 16384, 8192, layers, broad, pairs);
        system.SetContactListener(&listener);
        system.SetGravity(JPH::Vec3(float(config.gravity[0]), float(config.gravity[1]),
                                    float(config.gravity[2])));
    }
    void check() const {
        if (!active || thread != std::this_thread::get_id())
            throw std::runtime_error(
                "Physics service is stopped or accessed outside its owning thread");
    }
    void erase(std::map<std::uint64_t, Body>::iterator it) {
        auto& api = system.GetBodyInterface();
        api.RemoveBody(it->second.id);
        api.DestroyBody(it->second.id);
        bodies.erase(it);
    }
    std::map<std::uint64_t, std::pair<Configuration, LocalTransform>>
    desired(const std::map<std::uint64_t, TransformNode>* planned = nullptr) {
        const auto nodes = planned ? *planned : context.transform_nodes();
        const auto evaluated = evaluate_transforms(nodes);
        std::map<std::uint64_t, std::pair<Configuration, LocalTransform>> result;
        std::vector<flecs::entity_t> candidates;
        auto q = context.world().query<const PhysicsBody>();
        q.each([&](flecs::entity e, const PhysicsBody&) { candidates.push_back(e.id()); });
        // Validation may throw. Do it after the Flecs iterator releases table locks.
        for (auto id : candidates) {
            auto e = context.world().entity(id);
            const auto ref = context.reference(e.id());
            if (ref && context.resolve(*ref).state != WorldContext::ResolveState::Available)
                throw std::runtime_error(
                    "Physics requires unambiguous scene/entity references in this world");
            if (!context.reference(e.id()))
                throw std::runtime_error(
                    "Physics Body requires authored EntityId/scene membership");
            if (!evaluated.contains(id) || !evaluated.at(id).resolved)
                throw std::runtime_error("Physics Body needs a resolved Transform");
            if (e.get<PhysicsBody>().motion < 2) {
                // Phase 3 already resolves FollowStructure, Explicit, and World boundaries.
                // Traverse that same graph, including entities without physics components.
                for (auto parent = nodes.at(id).parent; parent; parent = nodes.at(parent).parent) {
                    auto ancestor = context.world().entity(parent);
                    if (ancestor.has<PhysicsBody>() && ancestor.get<PhysicsBody>().motion == 2) {
                        const auto ancestor_ref = context.reference(parent);
                        const auto label = [](flecs::entity entity) {
                            if (entity.has<AuthoredName>())
                                return entity.get<AuthoredName>().value;
                            return std::string(entity.name().c_str() ? entity.name().c_str()
                                                                     : "unnamed");
                        };
                        Diagnostic d{Severity::Error,
                                     "physics.unsupported_dynamic_ancestry",
                                     "Physics Body '" + label(e) +
                                         "' cannot spatially follow Dynamic Physics Body '" +
                                         label(ancestor) +
                                         "'. Separate Static/Kinematic bodies require independent "
                                         "World binding or a non-Dynamic spatial ancestry; "
                                         "physical attachments "
                                         "need future compound/constraint support.",
                                     {}};
                        d.context.entity = ref->entity;
                        d.context.asset = ref->scene;
                        d.context.related_entity = ancestor_ref;
                        d.context.module = "forge.physics";
                        d.context.property = "spatial";
                        d.context.tick = tick;
                        context.services().emit(d);
                        throw PhysicsConfigurationError(std::move(d));
                    }
                }
            }
            auto pose = decompose(evaluated.at(id).affine);
            valid_position(pose.translation);
            result.emplace(e.id(), std::pair{configuration(e, pose.scale), pose});
        }
        if (result.size() > max_bodies)
            throw std::runtime_error("Physics body limit exceeded (8192)");
        return result;
    }
    Body create(std::uint64_t entity, const Configuration& c, const LocalTransform& pose,
                std::optional<JPH::BodyID> requested = {}) {
        JPH::BodyCreationSettings settings(
            shape(c), position(pose.translation), rotation(pose.rotation),
            static_cast<JPH::EMotionType>(c.body.motion), c.body.motion ? 1 : 0);
        settings.mFriction = c.body.friction;
        settings.mRestitution = c.body.restitution;
        settings.mGravityFactor = c.body.gravity_factor;
        if (c.body.mass > 0 && c.body.motion != 0) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = c.body.mass;
        }
        auto& api = system.GetBodyInterface();
        auto body =
            requested ? api.CreateBodyWithID(*requested, settings) : api.CreateBody(settings);
        if (!body)
            throw std::runtime_error("Jolt body allocation/mapping failed");
        api.AddBody(body->GetID(),
                    c.body.motion ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        return {body->GetID(), *context.reference(entity), c, pose};
    }
};
PhysicsRuntime::PhysicsRuntime(WorldContext& c, PhysicsConfig config)
    : impl_(std::make_unique<Impl>(c, config)) {}
PhysicsRuntime::~PhysicsRuntime() { stop(); }
void PhysicsRuntime::stop() noexcept {
    if (!impl_ || !impl_->active)
        return;
    while (!impl_->bodies.empty())
        impl_->erase(impl_->bodies.begin());
    impl_->commands.clear();
    impl_->active = false;
}
void PhysicsRuntime::configure(PhysicsConfig config) {
    auto& s = *impl_;
    s.check();
    config.validate();
    if (!s.bodies.empty() || s.tick)
        throw std::runtime_error("Configure physics before content realization");
    s.config = config;
    s.system.SetGravity(
        JPH::Vec3(float(config.gravity[0]), float(config.gravity[1]), float(config.gravity[2])));
}
void PhysicsRuntime::synchronize(float dt) {
    auto& s = *impl_;
    s.check();
    const auto initial = s.desired();
    auto nodes = s.context.transform_nodes();
    std::set<std::uint64_t> commanded;
    for (const auto& cmd : s.commands) {
        const auto resolved = s.context.resolve(cmd.ref);
        if (resolved.state != WorldContext::ResolveState::Available ||
            !initial.contains(resolved.entity))
            throw std::runtime_error("Physics command target was removed or is unresolved");
        const auto id = resolved.entity;
        if (!cmd.teleport && (initial.at(id).first.body.motion != 1 || dt <= 0))
            throw std::runtime_error("Kinematic target requires a Kinematic Body and fixed tick");
        const auto evaluated = evaluate_transforms(nodes);
        auto target = decompose(evaluated.at(id).affine);
        target.translation = cmd.p;
        target.rotation = cmd.q;
        auto affine = affine_transform(target);
        if (nodes.at(id).parent)
            affine = inverse(evaluated.at(nodes.at(id).parent).affine) * affine;
        const auto local = decompose(affine);
        if (!equivalent(local.scale, nodes.at(id).local.scale))
            throw std::runtime_error("Physics target would change LocalScale under its spatial "
                                     "parent; choose a representable target or World binding");
        nodes.at(id).local.translation = local.translation;
        nodes.at(id).local.rotation = local.rotation;
        commanded.insert(id);
    }
    const auto desired = s.desired(&nodes);
    // Preflight direct Dynamic writes and all target geometry before any ECS/Jolt mutation.
    for (const auto& [entity, value] : desired) {
        const auto it = s.bodies.find(entity);
        if (value.first.body.motion == 2 && it != s.bodies.end() &&
            it->second.config.body.motion == 2 && !commanded.contains(entity) &&
            (!equivalent(value.second.translation, it->second.last.translation) ||
             !equivalent(value.second.rotation, it->second.last.rotation)))
            throw std::runtime_error("Dynamic pose is solver-owned; use Physics teleport instead "
                                     "of direct transform writes");
    }
    for (auto id : commanded) {
        auto e = s.context.world().entity(id);
        const auto before = s.context.get_local_transform(e);
        const auto& local = nodes.at(id).local;
        if (!equivalent(before.translation, local.translation))
            e.set<LocalTranslation>(local.translation);
        if (!equivalent(before.rotation, local.rotation))
            e.set<LocalRotation>(local.rotation);
    }
    s.context.evaluate_world_transforms();
    auto& api = s.system.GetBodyInterface();
    // Validate the complete candidate before touching live realization.
    for (auto it = s.bodies.begin(); it != s.bodies.end();) {
        if (!desired.contains(it->first)) {
            auto old = it++;
            s.erase(old);
        } else
            ++it;
    }
    for (const auto& [entity, value] : desired) {
        const auto& [config, pose] = value;
        auto it = s.bodies.find(entity);
        if (it != s.bodies.end() && it->second.config != config) {
            auto linear = api.GetLinearVelocity(it->second.id),
                 angular = api.GetAngularVelocity(it->second.id);
            bool moving =
                it->second.config.body.motion == config.body.motion && config.body.motion != 0;
            bool awake = api.IsActive(it->second.id);
            // Prepare shape before destroying the old body; initialization failure stops this
            // runtime.
            (void)shape(config);
            s.erase(it);
            auto next = s.create(entity, config, pose);
            it = s.bodies.emplace(entity, std::move(next)).first;
            if (moving) {
                api.SetLinearAndAngularVelocity(it->second.id, linear, angular);
                if (!awake)
                    api.DeactivateBody(it->second.id);
            }
            s.snaps.push_back(entity);
        }
        if (it == s.bodies.end()) {
            it = s.bodies.emplace(entity, s.create(entity, config, pose)).first;
            s.snaps.push_back(entity);
        }
        it->second.last = pose;
    }
    for (const auto& cmd : s.commands) {
        auto found = s.context.resolve(cmd.ref);
        auto it = s.bodies.find(found.entity);
        if (found.state != WorldContext::ResolveState::Available || it == s.bodies.end() ||
            it->second.ref != cmd.ref)
            throw std::runtime_error("Physics command target was removed or is unresolved");
        auto& body = it->second;
        if (cmd.teleport) {
            api.SetPositionAndRotation(body.id, position(cmd.p), rotation(cmd.q),
                                       JPH::EActivation::Activate);
            if (cmd.clear && body.config.body.motion != 0)
                api.SetLinearAndAngularVelocity(body.id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            s.snaps.push_back(found.entity);
        } else {
            if (body.config.body.motion != 1 || dt <= 0)
                throw std::runtime_error(
                    "Kinematic target requires a Kinematic Body and fixed tick");
            api.MoveKinematic(body.id, position(cmd.p), rotation(cmd.q), dt);
        }
    }
    // Resolve every body's final target after FIFO intent, independent of entity-ID order.
    // Teleports establish discontinuities before kinematic velocities are derived.
    for (auto& [entity, body] : s.bodies) {
        const auto& pose = desired.at(entity).second;
        if (body.config.body.motion == 0)
            api.SetPositionAndRotationWhenChanged(body.id, position(pose.translation),
                                                  rotation(pose.rotation),
                                                  JPH::EActivation::Activate);
        else if (body.config.body.motion == 1 && dt > 0)
            api.MoveKinematic(body.id, position(pose.translation), rotation(pose.rotation), dt);
    }
    s.commands.clear();
}
void PhysicsRuntime::step(float dt) {
    auto& s = *impl_;
    s.check();
    if (!std::isfinite(dt) || dt <= 0)
        throw std::runtime_error("Physics needs a positive fixed dt");
    auto profile = s.context.services().profile("physics", "JoltUpdate", s.tick + 1);
    auto error = s.system.Update(dt, 1, &s.allocator, &s.jobs);
    if (error != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt capacity exceeded during fixed tick");
    ++s.tick;
}
void PhysicsRuntime::adopt() {
    auto& s = *impl_;
    s.check();
    auto& api = s.system.GetBodyInterface();
    // Update has joined all workers. Only this owner-thread phase may touch Flecs.
    s.events.clear();
    if (s.listener.overflow)
        throw std::runtime_error("Physics contact buffer exceeded 16384 events");
    std::map<std::uint32_t, EntityRef> refs;
    for (const auto& [entity, b] : s.bodies)
        refs.emplace(b.id.GetIndexAndSequenceNumber(), b.ref);
    for (const auto& event : s.listener.events)
        if (refs.contains(event.first) && refs.contains(event.second))
            s.events.push_back({refs.at(event.first), refs.at(event.second), event.begin, s.tick});
    s.listener.events.clear();
    std::sort(s.events.begin(), s.events.end(), [](const auto& a, const auto& b) {
        return std::tie(a.first, a.second, a.begin) < std::tie(b.first, b.second, b.begin);
    });
    for (auto& [id, body] : s.bodies)
        if (body.config.body.motion == 2) {
            JPH::RVec3 p;
            JPH::Quat q;
            api.GetPositionAndRotation(body.id, p, q);
            auto e = s.context.world().entity(id);
            body.last.translation = {p.GetX(), p.GetY(), p.GetZ()};
            body.last.rotation = {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
            e.set<LocalTranslation>(body.last.translation);
            e.set<LocalRotation>(body.last.rotation);
        }
}
std::vector<std::uint64_t> PhysicsRuntime::take_discontinuities() {
    impl_->check();
    return std::exchange(impl_->snaps, {});
}
std::optional<PhysicsHit> PhysicsRuntime::raycast(Double3 origin, Double3 displacement) const {
    auto& s = *impl_;
    s.check();
    valid_position({origin[0], origin[1], origin[2]});
    valid_position({displacement[0], displacement[1], displacement[2]});
    JPH::RRayCast ray(
        JPH::RVec3(origin[0], origin[1], origin[2]),
        JPH::Vec3(float(displacement[0]), float(displacement[1]), float(displacement[2])));
    JPH::RayCastResult hit;
    if (!s.system.GetNarrowPhaseQuery().CastRay(ray, hit))
        return {};
    for (const auto& [entity, body] : s.bodies)
        if (body.id == hit.mBodyID) {
            auto p = ray.GetPointOnRay(hit.mFraction);
            JPH::BodyLockRead lock(s.system.GetBodyLockInterface(), hit.mBodyID);
            if (!lock.Succeeded())
                throw std::runtime_error("Raycast body disappeared");
            auto normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, p);
            return PhysicsHit{body.ref,
                              {p.GetX(), p.GetY(), p.GetZ()},
                              {normal.GetX(), normal.GetY(), normal.GetZ()},
                              hit.mFraction};
        }
    throw std::runtime_error("Physics ray hit an unowned body");
}
void PhysicsRuntime::teleport(EntityRef ref, LocalTranslation p, LocalRotation q, bool clear) {
    impl_->check();
    valid_position(p);
    q = normalized(q);
    if (impl_->commands.size() >= 4096)
        throw std::runtime_error("Physics command queue full");
    impl_->commands.push_back({ref, p, q, true, clear});
}
void PhysicsRuntime::move_kinematic(EntityRef ref, LocalTranslation p, LocalRotation q) {
    impl_->check();
    valid_position(p);
    q = normalized(q);
    if (impl_->commands.size() >= 4096)
        throw std::runtime_error("Physics command queue full");
    impl_->commands.push_back({ref, p, q, false, false});
}
const std::vector<PhysicsContact>& PhysicsRuntime::contacts() const {
    impl_->check();
    return impl_->events;
}
Json PhysicsRuntime::status() const {
    auto& s = *impl_;
    s.check();
    unsigned active = 0, sleeping = 0;
    for (const auto& [entity, b] : s.bodies)
        if (b.config.body.motion == 2) {
            if (s.system.GetBodyInterface().IsActive(b.id))
                ++active;
            else
                ++sleeping;
        }
    return {{"bodies", s.bodies.size()},
            {"active_dynamic", active},
            {"sleeping_dynamic", sleeping},
            {"tick", s.tick},
            {"gravity", s.config.gravity}};
}
Json PhysicsRuntime::checkpoint() const {
    auto& s = *impl_;
    s.check();
    const auto desired = s.desired();
    if (desired.size() != s.bodies.size())
        throw std::runtime_error("Physics checkpoint requires synchronized content; write body "
                                 "components before Physics");
    for (const auto& [id, b] : s.bodies)
        if (!desired.contains(id) || desired.at(id).first != b.config ||
            !equivalent(desired.at(id).second.translation, b.last.translation) ||
            !equivalent(desired.at(id).second.rotation, b.last.rotation))
            throw std::runtime_error("Physics checkpoint configuration or pose changed after "
                                     "adoption; defer component/transform edits to Gameplay");
    JPH::StateRecorderImpl recorder;
    s.system.SaveState(recorder);
    auto bytes = recorder.GetData();
    if (bytes.size() > max_solver_bytes)
        throw std::runtime_error("Physics checkpoint exceeds 2 MiB solver limit");
    Json mapping = Json::array();
    for (const auto& [id, b] : s.bodies)
        mapping.push_back({{"entity", b.ref},
                           {"body", b.id.GetIndexAndSequenceNumber()},
                           {"configuration", b.config.json()}});
    Json out = {{"version", 1},
                {"build", build_id},
                {"source", source_commit},
                {"runtime", FORGE_NATIVE_SDK_FINGERPRINT},
                {"jolt", "e77f175595e64cb44218cc9d9d56fc365ad0e36a-double-sse2-rtti"},
                {"gravity", s.config.gravity},
                {"tick", s.tick},
                {"mapping", mapping},
                {"solver", hex(bytes)},
                {"bytes", bytes.size()}};
    out["commands"] = Json::array();
    for (const auto& c : s.commands)
        out["commands"].push_back({{"entity", c.ref},
                                   {"position", {c.p.x, c.p.y, c.p.z}},
                                   {"rotation", {c.q.x, c.q.y, c.q.z, c.q.w}},
                                   {"teleport", c.teleport},
                                   {"clear", c.clear}});
    out["integrity"] = digest(out.dump());
    return out;
}
void PhysicsRuntime::restore(const Json& payload) {
    auto& s = *impl_;
    s.check();
    if (!s.bodies.empty() || s.tick || !s.commands.empty())
        throw std::runtime_error("Physics restoration requires a fresh unpublished world");
    if (payload.dump().size() > 6 * 1024 * 1024)
        throw std::runtime_error("Physics checkpoint exceeds envelope limit");
    auto checked = payload;
    auto checksum = checked.at("integrity").get<std::string>();
    checked.erase("integrity");
    if (digest(checked.dump()) != checksum || checked.at("version") != 1 ||
        checked.at("build") != build_id || checked.at("source") != source_commit ||
        checked.at("runtime") != FORGE_NATIVE_SDK_FINGERPRINT ||
        checked.at("jolt") != "e77f175595e64cb44218cc9d9d56fc365ad0e36a-double-sse2-rtti" ||
        checked.at("gravity") != Json(s.config.gravity))
        throw std::runtime_error(
            "Physics checkpoint integrity/version/build/configuration mismatch");
    auto bytes = unhex(checked.at("solver").get<std::string>());
    if (bytes.empty() || bytes.size() != checked.at("bytes").get<std::size_t>())
        throw std::runtime_error("Truncated physics checkpoint");
    auto desired = s.desired();
    const auto& mapping = checked.at("mapping");
    if (!mapping.is_array() || mapping.size() != desired.size())
        throw std::runtime_error("Physics checkpoint body count mismatch");
    std::set<std::uint32_t> ids, indices;
    std::set<std::uint64_t> entities;
    struct Pending {
        std::uint64_t entity;
        JPH::BodyID body;
    };
    std::vector<Pending> pending;
    for (const auto& entry : mapping) {
        auto ref = entry.at("entity").get<EntityRef>();
        auto resolution = s.context.resolve(ref);
        auto token = entry.at("body").get<std::uint32_t>();
        JPH::BodyID body(token);
        if (resolution.state != WorldContext::ResolveState::Available ||
            !desired.contains(resolution.entity) || !entities.insert(resolution.entity).second ||
            body.IsInvalid() || body.GetIndex() >= max_bodies || !ids.insert(token).second ||
            !indices.insert(body.GetIndex()).second ||
            entry.at("configuration") != desired.at(resolution.entity).first.json())
            throw std::runtime_error("Physics checkpoint realization mapping mismatch");
        pending.push_back({resolution.entity, body});
    }
    const auto& queued = checked.at("commands");
    if (!queued.is_array() || queued.size() > 4096)
        throw std::runtime_error("Invalid recovery command queue");
    std::vector<Impl::Command> commands;
    for (const auto& c : queued) {
        const auto p = c.at("position").get<std::array<double, 3>>();
        const auto q = c.at("rotation").get<std::array<float, 4>>();
        LocalTranslation translation{p[0], p[1], p[2]};
        valid_position(translation);
        auto ref = c.at("entity").get<EntityRef>();
        auto resolved = s.context.resolve(ref);
        if (resolved.state != WorldContext::ResolveState::Available ||
            !desired.contains(resolved.entity))
            throw std::runtime_error("Recovery command target mismatch");
        commands.push_back({ref, translation, normalized({q[0], q[1], q[2], q[3]}),
                            c.at("teleport").get<bool>(), c.at("clear").get<bool>()});
    }
    for (const auto& item : pending) {
        const auto& [c, p] = desired.at(item.entity);
        s.bodies.emplace(item.entity, s.create(item.entity, c, p, item.body));
    }
    JPH::StateRecorderImpl recorder;
    recorder.WriteBytes(bytes.data(), bytes.size());
    recorder.Rewind();
    if (!s.system.RestoreState(recorder) || recorder.IsFailed())
        throw std::runtime_error("Jolt rejected physics checkpoint");
    char extra{};
    recorder.ReadBytes(&extra, 1);
    if (!recorder.IsEOF())
        throw std::runtime_error("Trailing Jolt checkpoint bytes");
    s.tick = checked.at("tick").get<std::uint64_t>();
    adopt();
    s.commands = std::move(commands);
    for (const auto& [entity, b] : s.bodies)
        s.snaps.push_back(entity);
}
EngineModule physics_module(PhysicsConfig config) {
    config.validate();
    auto module = physics_schema_module();
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.allowed_services = 11;
    module.provided_services = capability(Capability::Physics);
    module.start = [config](ModuleContext& c) {
        if (!c.owner)
            throw std::runtime_error("Physics module requires a WorldContext owner");
        auto service = std::make_shared<PhysicsRuntime>(*c.owner, config);
        c.state = service;
        c.services.publish_physics(service);
    };
    module.stop = [](ModuleContext& c) {
        if (c.state)
            std::static_pointer_cast<PhysicsRuntime>(c.state)->stop();
        c.services.publish_physics({});
    };
    return module;
}
} // namespace forge
