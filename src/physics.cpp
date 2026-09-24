// Jolt requires its umbrella header before all other Jolt headers.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "builtins.hpp"
#include "collision_selection.hpp"
#include "collision_shape.hpp"
#include "physics_character.hpp"
#include "physics_debug.hpp"
#include "physics_registration.hpp"
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <algorithm>
#include <cmath>
#include <forge/build.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/physics.hpp>
#include <future>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
namespace forge {
namespace {
constexpr unsigned max_bodies = 8192;
constexpr std::size_t max_solver_bytes = 2 * 1024 * 1024;
using physics_detail::Registration;
using physics_detail::registration;
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
// Exact-pin CollisionGroup supplies full 32-bit application group/subgroup
// values without changing Jolt's 16-bit ObjectLayer ABI. Broadphase remains the
// engine-owned Static/Moving partition. No Flecs access from native workers.
struct ProjectGroupFilter final : JPH::GroupFilter {
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
        return a.GetSubGroupID() < 32 && b.GetSubGroupID() < 32 &&
               (a.GetGroupID() & (std::uint32_t{1} << b.GetSubGroupID())) &&
               (b.GetGroupID() & (std::uint32_t{1} << a.GetSubGroupID()));
    }
};
struct QueryBodyFilter final : JPH::BodyFilter {
    PhysicsQueryFilter filter;
    explicit QueryBodyFilter(PhysicsQueryFilter value) : filter(value) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        const auto layer = body.GetCollisionGroup().GetSubGroupID();
        return layer < 32 && (filter.mask & (std::uint32_t{1} << layer)) &&
               (filter.include_sensors || !body.IsSensor());
    }
};
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
    AssetId asset;
    std::string revision;
    std::shared_ptr<const physics_detail::PreparedCollision> prepared;
    bool operator==(const Configuration& b) const {
        return body == b.body && shape == b.shape && dimensions == b.dimensions &&
               scale == b.scale && asset == b.asset && revision == b.revision;
    }
    Json json() const {
        Json value{{"motion", body.motion},
                   {"density", body.density},
                   {"mass", body.mass},
                   {"friction", body.friction},
                   {"restitution", body.restitution},
                   {"gravity_factor", body.gravity_factor},
                   {"enabled", body.enabled},
                   {"sensor", body.sensor},
                   {"layer", body.layer},
                   {"mask", body.mask},
                   {"shape", shape},
                   {"dimensions", dimensions},
                   {"scale", {scale.x, scale.y, scale.z}}};
        if (asset)
            value["collision"] = {{"asset", asset}, {"revision", revision}};
        return value;
    }
};
void validate_asset_shape(const PhysicsBody& body, LocalScale scale,
                          const physics_detail::PreparedCollision& value) {
    if (value.static_only && body.motion != 0)
        throw std::runtime_error(
            "Triangle-mesh collision (including compound children) requires a Static Physics Body");
    const JPH::Vec3 native_scale(scale.x, scale.y, scale.z);
    if (!value.shape->IsValidScale(native_scale))
        throw std::runtime_error(
            "Collision asset cannot represent this world scale; visual scale was not changed");
    const auto bounds = value.shape->GetLocalBounds();
    const auto com = value.shape->GetCenterOfMass();
    for (unsigned i = 0; i < 3; ++i)
        for (const auto endpoint : {bounds.mMin[i], bounds.mMax[i]}) {
            const double coordinate = (double(endpoint) + com[i]) * native_scale[i];
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
                throw std::runtime_error("Scaled collision asset exceeds supported local bounds");
        }
}
Configuration validate_inline_shape(Configuration c) {
    const auto scale = c.scale;
    const JPH::Vec3 signed_scale(scale.x, scale.y, scale.z);
    bool valid_scale = false;
    if (c.shape == 0)
        valid_scale = JPH::BoxShape(JPH::Vec3::sReplicate(.5f)).IsValidScale(signed_scale);
    else if (c.shape == 1)
        valid_scale = JPH::SphereShape(.5f).IsValidScale(signed_scale);
    else if (c.shape == 2)
        valid_scale = JPH::CapsuleShape(.5f, .5f).IsValidScale(signed_scale);
    else
        valid_scale = JPH::CylinderShape(.5f, .5f).IsValidScale(signed_scale);
    if (!valid_scale)
        throw std::runtime_error(
            "Visual scale is valid, but this collider rejects its world scale: "
            "Jolt requires nonzero axes; Sphere/Capsule require uniform magnitudes; Cylinder "
            "requires matching X/Z magnitudes");
    // These centered symmetric shapes ignore axis signs in pinned Jolt.
    // Baking magnitudes into their dimensions represents exactly the same solid;
    // this is not a general policy for mesh/compound/asymmetric colliders.
    const LocalScale magnitude{std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)};
    for (unsigned i = 0; i < 3; ++i) {
        const double scaled =
            c.dimensions[i] * std::array<float, 3>{magnitude.x, magnitude.y, magnitude.z}[i];
        if (!std::isfinite(scaled) || scaled < .000999999 || scaled > 10000)
            throw std::runtime_error("Scaled collider dimensions must remain .001..10000 meters");
    }
    if ((c.shape == 1 || c.shape == 2) && (std::abs(magnitude.x - magnitude.y) > 1e-5f ||
                                           std::abs(magnitude.x - magnitude.z) > 1e-5f))
        throw std::runtime_error(
            "Sphere and Capsule Colliders require uniform world scale magnitudes");
    return c;
}
Configuration configuration(flecs::entity e, LocalScale scale,
                            ResourceLease<CollisionAsset> collision = {}) {
    auto b = e.get<PhysicsBody>();
    detail::validate_reflected_value(e, b);
    if (b.motion == 2 &&
        (!e.has<SpatialBinding>() || e.get<SpatialBinding>().mode != SpatialMode::World))
        throw std::runtime_error("Dynamic Physics Body requires Child space: World");
    if (unsigned(e.has<BoxCollider>()) + unsigned(e.has<SphereCollider>()) +
            unsigned(e.has<CapsuleCollider>()) + unsigned(e.has<CylinderCollider>()) +
            unsigned(e.has<AssetCollider>()) !=
        1)
        throw std::runtime_error(
            "Physics Body requires exactly one Box, Sphere, Capsule, Cylinder or Asset Collider");
    Configuration c{b, 0, {}, scale};
    if (e.has<AssetCollider>()) {
        detail::validate_reflected_value(e, e.get<AssetCollider>());
        const auto ref = e.get<AssetCollider>().asset;
        if (!collision || !ref.id || collision.identity().asset != ref.id)
            throw std::runtime_error("Collision asset is not ready; prepare required collision "
                                     "resources before simulation");
        validate_asset_shape(b, scale, *collision->native);
        c.shape = 4;
        c.asset = ref.id;
        c.revision = collision.identity().revision;
        c.prepared = collision->native;
        return c;
    }
    if (e.has<BoxCollider>()) {
        auto v = e.get<BoxCollider>();
        detail::validate_reflected_value(e, v);
        c.dimensions = {v.x, v.y, v.z};
    } else if (e.has<SphereCollider>()) {
        detail::validate_reflected_value(e, e.get<SphereCollider>());
        c.shape = 1;
        c.dimensions.fill(e.get<SphereCollider>().radius);
    } else if (e.has<CapsuleCollider>()) {
        c.shape = 2;
        auto v = e.get<CapsuleCollider>();
        detail::validate_reflected_value(e, v);
        c.dimensions = {v.radius, v.height, v.radius};
    } else {
        c.shape = 3;
        const auto v = e.get<CylinderCollider>();
        detail::validate_reflected_value(e, v);
        c.dimensions = {v.radius, v.height, v.radius};
    }
    return validate_inline_shape(c);
}

JPH::RefConst<JPH::Shape> shape(const Configuration& c) {
    JPH::ShapeSettings::ShapeResult result;
    if (c.shape == 4) {
        if (!c.prepared)
            throw std::runtime_error("Collision asset shape has no immutable resource owner");
        if (c.scale == LocalScale{1, 1, 1})
            return c.prepared->shape;
        result =
            JPH::ScaledShapeSettings(c.prepared->shape, {c.scale.x, c.scale.y, c.scale.z}).Create();
        if (result.HasError())
            throw std::runtime_error("Jolt collision asset scale: " +
                                     std::string(result.GetError()));
        return result.Get();
    }
    const LocalScale scale{std::abs(c.scale.x), std::abs(c.scale.y), std::abs(c.scale.z)};
    if (c.shape == 0) {
        JPH::Vec3 half(float(c.dimensions[0] * scale.x * .5), float(c.dimensions[1] * scale.y * .5),
                       float(c.dimensions[2] * scale.z * .5));
        JPH::BoxShapeSettings s(half, std::min(.05f, half.ReduceMin() * .1f));
        s.mDensity = c.body.density;
        result = s.Create();
    } else if (c.shape == 1) {
        JPH::SphereShapeSettings s(float(c.dimensions[0] * scale.x));
        s.mDensity = c.body.density;
        result = s.Create();
    } else if (c.shape == 2) {
        JPH::CapsuleShapeSettings s(float(c.dimensions[1] * scale.y * .5),
                                    float(c.dimensions[0] * scale.x));
        s.mDensity = c.body.density;
        result = s.Create();
    } else {
        JPH::CylinderShapeSettings s(float(c.dimensions[1] * scale.y * .5),
                                     float(c.dimensions[0] * scale.x));
        s.mDensity = c.body.density;
        result = s.Create();
    }
    if (result.HasError())
        throw std::runtime_error("Jolt collider: " + std::string(result.GetError().c_str()));
    return result.Get();
}
JPH::MassProperties mass_properties(const Configuration& c, const JPH::Shape& geometry) {
    auto value = geometry.GetMassProperties();
    if (c.body.motion == 0)
        return value;
    if (!std::isfinite(value.mMass) || value.mMass <= 0)
        throw std::runtime_error("Moving collision requires finite positive native mass");
    // Cooked immutable shapes use Jolt's default density (1000 kg/m^3).
    // Apply body density to a copied mass value, never mutate the shared shape.
    const double desired =
        c.body.mass > 0
            ? c.body.mass
            : double(value.mMass) * (c.shape == 4 ? double(c.body.density) / 1000.0 : 1.0);
    if (!std::isfinite(desired) || desired <= 0 || desired > std::numeric_limits<float>::max())
        throw std::runtime_error("Moving collision mass exceeds representable bounds");
    value.ScaleToMass(float(desired));
    for (unsigned column = 0; column < 3; ++column) {
        const auto v = value.mInertia.GetColumn4(column);
        for (unsigned row = 0; row < 3; ++row)
            if (!std::isfinite(v[row]) || (column == row && v[row] <= 0))
                throw std::runtime_error("Moving collision inertia is not finite and positive");
    }
    return value;
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
PhysicsDebugGeometry prepare_physics_debug(const Json& components, LocalScale scale,
                                           ResourceLease<CollisionAsset> collision, bool crouched,
                                           std::size_t limit) {
    if (!limit || limit > 65536)
        throw std::runtime_error("Physics preview triangle budget must be1..65536");
    const auto registration_lease = physics_detail::registration();
    auto decoded = [&]<class T>(const char* key) -> T {
        const auto& value = components.at(key);
        detail::validate_components(Json{{key, value}});
        for (const auto& type : detail::builtins())
            if (std::string_view(type.name) == key)
                return std::get<T>(type.decode(value));
        throw std::runtime_error("Unknown collision preview component");
    };
    JPH::RefConst<JPH::Shape> geometry;
    bool invert_character = false;
    if (components.contains("forge.character_controller")) {
        const auto config =
            decoded.template operator()<CharacterController>("forge.character_controller");
        if (components.contains("forge.physics_body") &&
            decoded.template operator()<PhysicsBody>("forge.physics_body").enabled)
            throw std::runtime_error("Character cannot share an enabled Physics Body");
        const auto shapes = physics_detail::character_shapes(config, scale);
        geometry = crouched ? shapes.crouched : shapes.standing;
        invert_character = scale.y < 0;
    } else {
        const auto body = decoded.template operator()<PhysicsBody>("forge.physics_body");
        unsigned count = 0;
        for (const auto key :
             {"forge.box_collider", "forge.sphere_collider", "forge.capsule_collider",
              "forge.cylinder_collider", "forge.asset_collider"})
            count += components.contains(key);
        if (count != 1)
            throw std::runtime_error("Physics Body requires exactly one collider");
        Configuration c{body, 0, {}, scale};
        if (components.contains("forge.asset_collider")) {
            const auto ref =
                decoded.template operator()<AssetCollider>("forge.asset_collider").asset;
            if (!collision || collision.identity().asset != ref.id)
                throw std::runtime_error("Collision asset preview is not ready");
            validate_asset_shape(body, scale, *collision->native);
            c.shape = 4;
            c.prepared = collision->native;
        } else {
            if (components.contains("forge.box_collider")) {
                const auto v = decoded.template operator()<BoxCollider>("forge.box_collider");
                c.dimensions = {v.x, v.y, v.z};
            } else if (components.contains("forge.sphere_collider")) {
                c.shape = 1;
                c.dimensions.fill(
                    decoded.template operator()<SphereCollider>("forge.sphere_collider").radius);
            } else if (components.contains("forge.capsule_collider")) {
                c.shape = 2;
                const auto v =
                    decoded.template operator()<CapsuleCollider>("forge.capsule_collider");
                c.dimensions = {v.radius, v.height, v.radius};
            } else {
                c.shape = 3;
                const auto v =
                    decoded.template operator()<CylinderCollider>("forge.cylinder_collider");
                c.dimensions = {v.radius, v.height, v.radius};
            }
            c = validate_inline_shape(c);
        }
        geometry = shape(c);
        (void)mass_properties(c, *geometry);
    }
    JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
    geometry->CollectTransformedShapes(JPH::AABox::sBiggest(), geometry->GetCenterOfMass(),
                                       JPH::Quat::sIdentity(), JPH::Vec3::sOne(), {}, leaves, {});
    PhysicsDebugGeometry result;
    std::array<JPH::Float3, 96> vertices;
    for (const auto& leaf : leaves.mHits) {
        JPH::Shape::GetTrianglesContext context;
        leaf.GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::RVec3::sZero());
        while (const auto count = leaf.GetTrianglesNext(context, 32, vertices.data())) {
            for (int i = 0; i < count; ++i) {
                if (result.triangles.size() == limit) {
                    result.truncated = true;
                    return result;
                }
                std::array<std::array<float, 3>, 3> triangle;
                for (unsigned j = 0; j < 3; ++j) {
                    const auto& p = vertices[std::size_t(i) * 3 + j];
                    triangle[j] = {p.x, invert_character ? -p.y : p.y,
                                   invert_character ? -p.z : p.z};
                }
                result.triangles.push_back(triangle);
            }
        }
    }
    return result;
}
struct PhysicsRuntime::Impl {
    WorldContext& context;
    PhysicsConfig config;
    bool active = true;
    std::thread::id thread = std::this_thread::get_id();
    std::shared_ptr<Registration> lease = registration();
    Layers layers;
    BroadFilter broad;
    PairFilter pairs;
    JPH::RefConst<ProjectGroupFilter> group_filter = new ProjectGroupFilter;
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
    struct CharacterRecord {
        EntityRef ref;
        CharacterController config;
        LocalTransform last;
        std::unique_ptr<physics_detail::Character> owner;
        bool shape_blocked = false;
    };
    std::map<std::uint64_t, CharacterRecord> characters;
    struct CharacterCommand {
        EntityRef ref;
        unsigned kind = 0; // Movement, Jump, Crouch, Placement.
        Double3 velocity{};
        float speed = 0;
        bool flag = false;
        LocalTranslation position;
        LocalRotation rotation;
    };
    std::vector<CharacterCommand> character_commands;
    using CharacterDesired =
        std::map<std::uint64_t, std::pair<CharacterController, LocalTransform>>;
    CharacterDesired
    desired_characters(const std::map<std::uint64_t, TransformNode>* planned = nullptr) {
        const auto nodes = planned ? *planned : context.transform_nodes();
        const auto evaluated = evaluate_transforms(nodes);
        std::vector<flecs::entity_t> candidates;
        auto q = context.world().query<const CharacterController>();
        q.each([&](flecs::entity e, const CharacterController& c) {
            if (c.enabled)
                candidates.push_back(e.id());
        });
        if (candidates.size() > 1024)
            throw std::runtime_error("Character capacity 1024 exceeded");
        CharacterDesired result;
        for (auto id : candidates) {
            auto e = context.world().entity(id);
            const auto c = e.get<CharacterController>();
            detail::validate_reflected_value(e, c);
            physics_detail::validate_character(c);
            if (c.layer >= config.layers.size() || config.layers[c.layer].empty())
                throw std::runtime_error("Character selects an unnamed project collision layer");
            const auto ref = context.reference(id);
            if (!ref || context.resolve(*ref).state != WorldContext::ResolveState::Available)
                throw std::runtime_error(
                    "Character requires unambiguous authored scene membership");
            if ((e.has<PhysicsBody>() && e.get<PhysicsBody>().enabled) ||
                !e.has<SpatialBinding>() || e.get<SpatialBinding>().mode != SpatialMode::World)
                throw std::runtime_error("Character requires Child space: World and cannot share "
                                         "an enabled Physics Body");
            if (!evaluated.contains(id) || !evaluated.at(id).resolved)
                throw std::runtime_error("Character needs a resolved transform");
            const auto pose = decompose(evaluated.at(id).affine, &nodes.at(id).local);
            valid_position(pose.translation);
            result.emplace(id, std::pair{c, pose});
        }
        return result;
    }
    Json character_configuration(std::uint64_t id, LocalScale scale) {
        for (const auto& type : detail::builtins())
            if (std::string_view(type.name) == "forge.character_controller")
                return {{"controller", type.read(context.world().entity(id), true)},
                        {"scale", {scale.x, scale.y, scale.z}}};
        throw std::runtime_error("Missing character reflection metadata");
    }
    void synchronize_characters() {
        const auto desired = desired_characters();
        // Admission before retiring previous characters. Scene transform writes
        // cannot compete with a simulated controller's position/orientation.
        std::map<std::uint64_t, CharacterRecord> prepared;
        for (const auto& [id, value] : desired) {
            const auto& [c, pose] = value;
            const auto prior = characters.find(id);
            if (prior != characters.end() &&
                (!equivalent(pose.translation, prior->second.last.translation) ||
                 !equivalent(pose.rotation, prior->second.last.rotation)))
                throw std::runtime_error(
                    "Character pose is simulation-owned; use character placement");
            if (prior != characters.end() && prior->second.config == c &&
                prior->second.last.scale == pose.scale)
                continue;
            auto next = std::make_unique<physics_detail::Character>(system, allocator, c, pose,
                                                                    group_filter.GetPtr());
            JPH::BodyID ignored;
            if (prior != characters.end()) {
                auto& old = *prior->second.owner;
                ignored = old.native().GetInnerBodyID();
                next->restore_motion(old.intent(), old.takeoff_velocity(), old.pending_jump(),
                                     old.crouched(), old.last_jump_accepted());
                next->native().SetLinearVelocity(old.native().GetLinearVelocity());
            }
            if (!next->place(pose.translation, pose.rotation, false, ignored))
                throw std::runtime_error(
                    "Character candidate overlaps solid geometry; previous character retained");
            prepared.emplace(
                id, CharacterRecord{*context.reference(id), c, pose, std::move(next), false});
        }
        for (const auto& command : character_commands) {
            const auto found = context.resolve(command.ref);
            if (found.state != WorldContext::ResolveState::Available ||
                !desired.contains(found.entity))
                throw std::runtime_error("Character command target was removed or disabled");
        }
        std::erase_if(characters, [&](const auto& item) { return !desired.contains(item.first); });
        for (auto& [id, next] : prepared) {
            characters.insert_or_assign(id, std::move(next));
            snaps.push_back(id);
        }
        for (const auto& command : character_commands) {
            const auto id = context.resolve(command.ref).entity;
            auto& c = characters.at(id);
            auto& owner = *c.owner;
            switch (command.kind) {
            case 0:
                owner.movement(command.velocity);
                break;
            case 1:
                owner.jump(command.speed);
                break;
            case 2:
                c.shape_blocked = !owner.crouch(command.flag);
                break;
            case 3:
                c.shape_blocked = !owner.place(command.position, command.rotation, command.flag);
                if (!c.shape_blocked) {
                    auto e = context.world().entity(id);
                    if (!equivalent(c.last.translation, command.position))
                        e.set<LocalTranslation>(command.position);
                    if (!equivalent(c.last.rotation, command.rotation))
                        e.set<LocalRotation>(command.rotation);
                    c.last.translation = command.position;
                    c.last.rotation = command.rotation;
                    snaps.push_back(id);
                }
                break;
            default:
                throw std::runtime_error("Unknown character command");
            }
        }
        character_commands.clear();
    }
    void queue_character(CharacterCommand command) {
        check();
        if (character_commands.size() >= 4096)
            throw std::runtime_error("Character command queue full");
        const auto found = context.resolve(command.ref);
        if (found.state != WorldContext::ResolveState::Available ||
            !context.world().entity(found.entity).has<CharacterController>())
            throw std::runtime_error("Missing character command target");
        const auto entity = context.world().entity(found.entity);
        if (!entity.get<CharacterController>().enabled)
            throw std::runtime_error("Character command target is disabled");
        auto q = entity.has<LocalRotation>() ? entity.get<LocalRotation>() : LocalRotation{};
        const bool inverted = entity.has<LocalScale>() && entity.get<LocalScale>().y < 0;
        Double3 intent{};
        if (const auto prior = characters.find(found.entity); prior != characters.end()) {
            q = prior->second.last.rotation;
            intent = prior->second.owner->intent();
        }
        auto preflight = [&](const CharacterCommand& c) {
            if (c.kind == 0)
                intent = c.velocity;
            if (c.kind == 3)
                q = c.rotation;
            const auto up = rotation(q) * JPH::Vec3(0, inverted ? -1.f : 1.f, 0);
            const JPH::Vec3 v{float(intent[0]), float(intent[1]), float(intent[2])};
            if (std::abs(v.Dot(up)) > 1e-5f * std::max(1.f, v.Length()))
                throw std::runtime_error("Character movement must be planar to its up axis; clear "
                                         "movement before changing up");
        };
        for (const auto& prior : character_commands)
            if (prior.ref == command.ref)
                preflight(prior);
        preflight(command);
        character_commands.push_back(std::move(command));
    }

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
    std::filesystem::path project;
    std::shared_ptr<const AssetCatalog> catalog;
    std::unique_ptr<ResourcePool<CollisionAsset>> collision_pool;
    std::map<AssetId, ResourceTicket> collision_tickets;
    std::map<std::uint64_t, AssetId> collision_observers;
    std::map<AssetId, std::string> collision_errors;
    // Last: join file-only capture before destroying dependent owner state.
    std::future<std::shared_ptr<const AssetCatalog>> catalog_capture;
    explicit Impl(WorldContext& c, PhysicsConfig settings, std::filesystem::path root)
        : context(c), config(settings), project(std::move(root)) {
        config.validate();
        system.Init(max_bodies, 0, 16384, 8192, layers, broad, pairs);
        system.SetContactListener(&listener);
        system.SetGravity(JPH::Vec3(float(config.gravity[0]), float(config.gravity[1]),
                                    float(config.gravity[2])));
    }
    void refresh_assets() {
        check();
        if (project.empty())
            return;
        if (!catalog_capture.valid())
            catalog_capture = std::async(std::launch::async, [root = project] {
                return std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
            });
    }
    bool prepare_assets(bool boundary = false) {
        check();
        std::set<AssetId> required;
        auto query = context.world().query<const PhysicsBody, const AssetCollider>();
        query.each([&](const PhysicsBody& body, const AssetCollider& collider) {
            if (body.enabled)
                required.insert(collider.asset.id);
        });
        const auto simulation_required = required;
        for (const auto& [token, id] : collision_observers)
            required.insert(id);
        if (required.empty()) {
            collision_errors.clear();
            if (collision_pool) {
                for (const auto& [id, ticket] : collision_tickets)
                    collision_pool->unload({id});
                collision_tickets.clear();
                collision_pool->collect();
            }
            return true;
        }
        if (required.contains(AssetId{}) || project.empty())
            throw std::runtime_error(
                "Asset Collider requires an assigned Collision asset and runtime content root");
        if (!collision_pool)
            collision_pool = std::make_unique<ResourcePool<CollisionAsset>>(
                ResourcePoolLimits{1, 64, 4096, 256ull * 1024 * 1024},
                [this](const CollisionResourceData& candidate, const CollisionResourceData*) {
                    const auto nodes = context.transform_nodes();
                    const auto evaluated = evaluate_transforms(nodes);
                    std::vector<flecs::entity_t> entities;
                    auto q = context.world().query<const PhysicsBody, const AssetCollider>();
                    q.each([&](flecs::entity e, const PhysicsBody&, const AssetCollider& collider) {
                        if (e.get<PhysicsBody>().enabled && collider.asset.id == candidate.asset)
                            entities.push_back(e.id());
                    });
                    for (auto id : entities) {
                        if (!evaluated.contains(id) || !evaluated.at(id).resolved)
                            throw std::runtime_error(
                                "Collision replacement needs a resolved world transform");
                        const auto pose = decompose(evaluated.at(id).affine, &nodes.at(id).local);
                        const auto body = context.world().entity(id).get<PhysicsBody>();
                        validate_asset_shape(body, pose.scale, *candidate.native);
                        if (body.motion != 0) {
                            Configuration c{
                                body, 4, {}, pose.scale, candidate.asset, {}, candidate.native};
                            (void)mass_properties(c, *shape(c));
                        }
                    }
                });
        // A presentation/paused poll may queue CPU work, but an existing world
        // only adopts a replacement at the next physics synchronization boundary.
        if (boundary || bodies.empty())
            collision_pool->pump();
        if (!catalog && !catalog_capture.valid())
            refresh_assets();
        if (catalog_capture.valid() &&
            catalog_capture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            catalog = catalog_capture.get();
        if (!catalog)
            return simulation_required.empty();
        bool ready = true;
        for (auto id : required) {
            ResourceTicket request;
            try {
                const auto found = collision_tickets.find(id);
                const auto& selected = catalog->records().at(id).metadata.at("forge.import");
                if (found != collision_tickets.end() &&
                    found->second.inspect().identity.revision ==
                        selected.at("key").get<std::string>() &&
                    found->second.inspect().source_generation ==
                        selected.at("generation").get<std::uint64_t>())
                    request = found->second;
                else
                    request = request_collision(*collision_pool, project, catalog, {id});
                collision_tickets[id] = request;
                collision_errors.erase(id);
            } catch (const std::exception& e) {
                collision_errors[id] = e.what();
                if (simulation_required.contains(id) && !collision_pool->current({id}))
                    throw;
                continue;
            }
            const auto info = request.inspect();
            if (!info.diagnostic.empty())
                collision_errors[id] = info.diagnostic;
            if (collision_pool->current({id}))
                continue; // A rejected/pending replacement retains the usable revision.
            if (simulation_required.contains(id)) {
                if (resource_detail::terminal(info.state) && info.state != ResourceState::Ready)
                    throw std::runtime_error("Collision asset " + id.str() + ": " +
                                             info.diagnostic);
                ready = false;
            }
        }
        for (auto it = collision_tickets.begin(); it != collision_tickets.end();) {
            if (!required.contains(it->first)) {
                collision_pool->unload({it->first});
                it = collision_tickets.erase(it);
            } else
                ++it;
        }
        std::erase_if(collision_errors,
                      [&](const auto& entry) { return !required.contains(entry.first); });
        collision_pool->collect();
        return ready;
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
        q.each([&](flecs::entity e, const PhysicsBody& body) {
            if (body.enabled)
                candidates.push_back(e.id());
        });
        // Validation may throw. Do it after the Flecs iterator releases table locks.
        for (auto id : candidates) {
            auto e = context.world().entity(id);
            const auto body = e.get<PhysicsBody>();
            if (body.layer >= config.layers.size() || config.layers[body.layer].empty())
                throw std::runtime_error("Physics Body selects an unnamed project collision layer");
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
                    const bool character_ancestor = ancestor.has<CharacterController>() &&
                                                    ancestor.get<CharacterController>().enabled;
                    if (character_ancestor ||
                        (ancestor.has<PhysicsBody>() && ancestor.get<PhysicsBody>().enabled &&
                         ancestor.get<PhysicsBody>().motion == 2)) {
                        const auto ancestor_ref = context.reference(parent);
                        const auto label = [](flecs::entity entity) {
                            if (entity.has<AuthoredName>())
                                return entity.get<AuthoredName>().value;
                            return std::string(entity.name().c_str() ? entity.name().c_str()
                                                                     : "unnamed");
                        };
                        Diagnostic d{
                            Severity::Error,
                            character_ancestor ? "physics.unsupported_character_ancestry"
                                               : "physics.unsupported_dynamic_ancestry",
                            "Physics Body '" + label(e) +
                                std::string(
                                    character_ancestor
                                        ? "' cannot spatially follow Character Controller '"
                                        : "' cannot spatially follow Dynamic Physics Body '") +
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
            auto pose = decompose(evaluated.at(id).affine, &nodes.at(id).local);
            valid_position(pose.translation);
            const auto collision = e.has<AssetCollider>() && collision_pool
                                       ? collision_pool->current(e.get<AssetCollider>().asset)
                                       : ResourceLease<CollisionAsset>{};
            result.emplace(e.id(), std::pair{configuration(e, pose.scale, collision), pose});
        }
        if (result.size() > max_bodies)
            throw std::runtime_error("Physics body limit exceeded (8192)");
        return result;
    }
    using Desired = std::map<std::uint64_t, std::pair<Configuration, LocalTransform>>;
    void validate_dynamic_writes(const Desired& desired,
                                 const std::set<std::uint64_t>& commanded = {}) {
        for (const auto& [entity, value] : desired) {
            const auto it = bodies.find(entity);
            if (value.first.body.motion == 2 && it != bodies.end() &&
                it->second.config.body.motion == 2 && !commanded.contains(entity) &&
                (!equivalent(value.second.translation, it->second.last.translation) ||
                 !equivalent(value.second.rotation, it->second.last.rotation)))
                throw std::runtime_error(
                    "Dynamic pose is solver-owned; use Physics teleport instead "
                    "of direct transform writes");
        }
    }
    Body create(std::uint64_t entity, const Configuration& c, const LocalTransform& pose,
                std::optional<JPH::BodyID> requested = {},
                JPH::RefConst<JPH::Shape> prepared = {}) {
        if (!prepared)
            prepared = shape(c);
        JPH::BodyCreationSettings settings(
            prepared.GetPtr(), position(pose.translation), rotation(pose.rotation),
            static_cast<JPH::EMotionType>(c.body.motion), c.body.motion ? 1 : 0);
        settings.mCollisionGroup =
            JPH::CollisionGroup(group_filter.GetPtr(), c.body.mask, c.body.layer);
        settings.mIsSensor = c.body.sensor;
        settings.mCollideKinematicVsNonDynamic = c.body.sensor && c.body.motion == 1;
        settings.mFriction = c.body.friction;
        settings.mRestitution = c.body.restitution;
        settings.mGravityFactor = c.body.gravity_factor;
        if (c.body.motion != 0) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = mass_properties(c, *prepared);
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
PhysicsRuntime::PhysicsRuntime(WorldContext& c, PhysicsConfig config, std::filesystem::path root)
    : impl_(std::make_unique<Impl>(c, config, std::move(root))) {}
PhysicsRuntime::~PhysicsRuntime() { stop(); }
void PhysicsRuntime::stop() noexcept {
    if (!impl_ || !impl_->active)
        return;
    impl_->characters.clear();
    impl_->character_commands.clear();
    impl_->collision_observers.clear();
    while (!impl_->bodies.empty())
        impl_->erase(impl_->bodies.begin());
    impl_->commands.clear();
    if (impl_->collision_pool)
        impl_->collision_pool->close();
    impl_->active = false;
}
bool PhysicsRuntime::prepare_assets() { return impl_->prepare_assets(); }
void PhysicsRuntime::refresh_assets() { impl_->refresh_assets(); }
void PhysicsRuntime::configure(PhysicsConfig config) {
    auto& s = *impl_;
    s.check();
    config.validate();
    if (!s.bodies.empty() || !s.characters.empty() || s.tick)
        throw std::runtime_error("Configure physics before content realization");
    s.config = config;
    s.system.SetGravity(
        JPH::Vec3(float(config.gravity[0]), float(config.gravity[1]), float(config.gravity[2])));
}
void PhysicsRuntime::validate_transform_candidate(
    const std::map<std::uint64_t, TransformNode>& nodes) {
    auto& s = *impl_;
    s.check();
    const auto candidate = s.desired(&nodes);
    s.validate_dynamic_writes(candidate);
    const auto chars = s.desired_characters(&nodes), original_chars = s.desired_characters();
    for (const auto& [id, value] : chars) {
        const auto& original = original_chars.at(id).second;
        if (!equivalent(value.second.translation, original.translation) ||
            !equivalent(value.second.rotation, original.rotation))
            throw std::runtime_error(
                "Character transform is simulation-owned; animate a separate visual child");
    }
    // Before first realization a Dynamic body is still an authored starting pose,
    // not a license for animation to acquire its translation/rotation authority.
    std::optional<Impl::Desired> initial;
    for (const auto& [id, value] : candidate) {
        if (value.first.body.motion != 2 || s.bodies.contains(id))
            continue;
        if (!initial)
            initial = s.desired();
        const auto& current = initial->at(id).second;
        if (!equivalent(value.second.translation, current.translation) ||
            !equivalent(value.second.rotation, current.rotation))
            throw std::runtime_error("Dynamic starting pose cannot be driven by model animation; "
                                     "use a Kinematic body or a separate visual child");
    }
}
void PhysicsRuntime::synchronize(float dt) {
    auto& s = *impl_;
    s.check();
    if (!s.prepare_assets(true))
        throw std::runtime_error("Required collision assets are still preparing");
    const auto initial = s.desired();
    if (initial.size() + s.desired_characters().size() > max_bodies)
        throw std::runtime_error("Combined rigid/character body capacity exceeded");
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
        auto target = decompose(evaluated.at(id).affine, &nodes.at(id).local);
        target.translation = cmd.p;
        target.rotation = cmd.q;
        auto affine = affine_transform(target);
        if (nodes.at(id).parent)
            affine = inverse(evaluated.at(nodes.at(id).parent).affine) * affine;
        const auto local = decompose(affine, &nodes.at(id).local);
        if (!equivalent(local.scale, nodes.at(id).local.scale))
            throw std::runtime_error("Physics target would change LocalScale under its spatial "
                                     "parent; choose a representable target or World binding");
        nodes.at(id).local.translation = local.translation;
        nodes.at(id).local.rotation = local.rotation;
        commanded.insert(id);
    }
    const auto desired = s.desired(&nodes);
    // Same solver ownership check used by reject-before-write animation admission.
    s.validate_dynamic_writes(desired, commanded);
    // Complete native shape admission before authored command writes or retiring
    // any old body. Body allocation failure remains an explicit runtime fault.
    std::map<std::uint64_t, JPH::RefConst<JPH::Shape>> prepared;
    for (const auto& [entity, value] : desired) {
        const auto old = s.bodies.find(entity);
        if (old == s.bodies.end() || old->second.config != value.first) {
            auto geometry = shape(value.first);
            if (value.first.body.motion != 0)
                (void)mass_properties(value.first, *geometry);
            prepared.emplace(entity, std::move(geometry));
        }
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
            s.erase(it);
            auto next = s.create(entity, config, pose, {}, prepared.at(entity));
            it = s.bodies.emplace(entity, std::move(next)).first;
            if (moving) {
                api.SetLinearAndAngularVelocity(it->second.id, linear, angular);
                if (!awake)
                    api.DeactivateBody(it->second.id);
            }
            s.snaps.push_back(entity);
        }
        if (it == s.bodies.end()) {
            it = s.bodies.emplace(entity, s.create(entity, config, pose, {}, prepared.at(entity)))
                     .first;
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
    s.synchronize_characters();
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
    for (auto& [id, character] : s.characters)
        character.owner->step(dt, s.config.gravity);
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
    for (const auto& [id, c] : s.characters)
        refs.emplace(c.owner->native().GetInnerBodyID().GetIndexAndSequenceNumber(), c.ref);
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
    for (auto& [id, c] : s.characters) {
        const auto p = c.owner->native().GetPosition();
        const LocalTranslation position{p.GetX(), p.GetY(), p.GetZ()};
        valid_position(position);
        if (!equivalent(position, c.last.translation))
            s.context.world().entity(id).set<LocalTranslation>(position);
        c.last.translation = position;
    }
}
std::vector<std::uint64_t> PhysicsRuntime::take_discontinuities() {
    impl_->check();
    return std::exchange(impl_->snaps, {});
}
std::optional<PhysicsHit> PhysicsRuntime::raycast(Double3 origin, Double3 displacement) const {
    return raycast_filtered(origin, displacement, {});
}
std::optional<PhysicsHit> PhysicsRuntime::raycast_filtered(Double3 origin, Double3 displacement,
                                                           PhysicsQueryFilter filter) const {
    auto& s = *impl_;
    s.check();
    valid_position({origin[0], origin[1], origin[2]});
    valid_position({displacement[0], displacement[1], displacement[2]});
    JPH::RRayCast ray(
        JPH::RVec3(origin[0], origin[1], origin[2]),
        JPH::Vec3(float(displacement[0]), float(displacement[1]), float(displacement[2])));
    JPH::RayCastResult hit;
    if (!s.system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, QueryBodyFilter(filter)))
        return {};
    std::optional<EntityRef> ref;
    for (const auto& [id, body] : s.bodies)
        if (body.id == hit.mBodyID)
            ref = body.ref;
    for (const auto& [id, c] : s.characters)
        if (c.owner->native().GetInnerBodyID() == hit.mBodyID)
            ref = c.ref;
    if (!ref)
        throw std::runtime_error("Physics ray hit an unowned body");
    const auto p = ray.GetPointOnRay(hit.mFraction);
    JPH::BodyLockRead lock(s.system.GetBodyLockInterface(), hit.mBodyID);
    if (!lock.Succeeded())
        throw std::runtime_error("Raycast body disappeared");
    const auto normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, p);
    return PhysicsHit{*ref,
                      {p.GetX(), p.GetY(), p.GetZ()},
                      {normal.GetX(), normal.GetY(), normal.GetZ()},
                      hit.mFraction};
}
std::optional<PhysicsHit> PhysicsRuntime::shape_cast(const PhysicsSweep& request,
                                                     PhysicsQueryFilter filter) const {
    auto& s = *impl_;
    s.check();
    valid_position(request.origin);
    valid_position({request.displacement[0], request.displacement[1], request.displacement[2]});
    if (unsigned(request.shape) > 3)
        throw std::runtime_error("Unsupported query shape");
    const unsigned count = request.shape == PhysicsSweep::Shape::Box      ? 3
                           : request.shape == PhysicsSweep::Shape::Sphere ? 1
                                                                          : 2;
    for (unsigned i = 0; i < 3; ++i) {
        const auto v = request.dimensions[i];
        if (!std::isfinite(v) || (i < count ? (v < .001 || v > 10000) : v != 0))
            throw std::runtime_error("Invalid query shape dimensions");
    }
    Configuration config;
    config.shape = unsigned(request.shape);
    config.scale = {1, 1, 1};
    config.dimensions = request.dimensions;
    if (config.shape == 1)
        config.dimensions.fill(request.dimensions[0]);
    else if (config.shape >= 2)
        config.dimensions[2] = request.dimensions[0];
    const auto geometry = shape(config);
    const auto q = rotation(request.rotation);
    const JPH::RVec3 origin(request.origin.x, request.origin.y, request.origin.z);
    const auto cast = JPH::RShapeCast::sFromWorldTransform(
        geometry, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(q, origin),
        JPH::Vec3(float(request.displacement[0]), float(request.displacement[1]),
                  float(request.displacement[2])));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    s.system.GetNarrowPhaseQuery().CastShape(cast, settings, origin, collector, {}, {},
                                             QueryBodyFilter(filter));
    if (!collector.HadHit())
        return {};
    const auto& hit = collector.mHit;
    std::optional<EntityRef> ref;
    for (const auto& [id, body] : s.bodies)
        if (body.id == hit.mBodyID2)
            ref = body.ref;
    for (const auto& [id, c] : s.characters)
        if (c.owner->native().GetInnerBodyID() == hit.mBodyID2)
            ref = c.ref;
    if (!ref)
        throw std::runtime_error("Shape query hit an unowned body");
    const auto point = origin + hit.mContactPointOn2;
    const auto normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
    return PhysicsHit{*ref,
                      {point.GetX(), point.GetY(), point.GetZ()},
                      {normal.GetX(), normal.GetY(), normal.GetZ()},
                      hit.mFraction};
}
std::uint64_t PhysicsRuntime::request_collision_asset(AssetId asset) {
    auto& s = *impl_;
    s.check();
    if (!asset || s.project.empty() || s.collision_observers.size() >= 256)
        throw std::runtime_error(
            "Collision preload requires an asset/content root and at most256 subscriptions");
    static std::atomic<std::uint64_t> sequence{1};
    auto token = sequence.load();
    do {
        if (token == UINT64_MAX)
            throw std::runtime_error("Collision subscription space exhausted");
    } while (!sequence.compare_exchange_weak(token, token + 1));
    s.collision_observers.emplace(token, asset);
    return token;
}
RuntimeResourceStatus PhysicsRuntime::inspect_collision_asset(std::uint64_t token) const {
    auto& s = *impl_;
    s.check();
    const auto observer = s.collision_observers.find(token);
    if (observer == s.collision_observers.end())
        throw std::runtime_error("Unknown collision subscription");
    const auto asset = observer->second;
    RuntimeResourceStatus out{"queued", {}, {}, {}, 0};
    if (const auto request = s.collision_tickets.find(asset);
        request != s.collision_tickets.end()) {
        const auto info = request->second.inspect();
        out = {resource_state_name(info.state),
               info.identity.revision,
               {},
               info.diagnostic,
               info.source_generation};
    }
    if (s.collision_pool)
        if (const auto value = s.collision_pool->current({asset}); value)
            out.retained_revision = value.identity().revision;
    if (const auto error = s.collision_errors.find(asset); error != s.collision_errors.end()) {
        out.state = "failed";
        out.diagnostic = error->second;
    }
    return out;
}
bool PhysicsRuntime::release_collision_asset(std::uint64_t token) {
    impl_->check();
    return impl_->collision_observers.erase(token) != 0;
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
CharacterState PhysicsRuntime::character(EntityRef ref) const {
    auto& s = *impl_;
    s.check();
    const auto found = s.context.resolve(ref);
    if (found.state != WorldContext::ResolveState::Available ||
        !s.characters.contains(found.entity))
        throw std::runtime_error("Character is unavailable or not yet realized");
    const auto& value = s.characters.at(found.entity);
    const auto& c = value.owner->native();
    const auto copy = [](const auto& v) -> Double3 { return {v.GetX(), v.GetY(), v.GetZ()}; };
    CharacterState out;
    out.position = value.last.translation;
    out.rotation = value.last.rotation;
    out.velocity = copy(c.GetLinearVelocity());
    out.ground_normal = copy(c.GetGroundNormal());
    out.ground_velocity = copy(c.GetGroundVelocity());
    out.ground_position = copy(c.GetGroundPosition());
    switch (c.GetGroundState()) {
    case JPH::CharacterBase::EGroundState::OnGround:
        out.ground = CharacterGround::OnGround;
        break;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
        out.ground = CharacterGround::OnSteepGround;
        break;
    case JPH::CharacterBase::EGroundState::NotSupported:
        out.ground = CharacterGround::NotSupported;
        break;
    case JPH::CharacterBase::EGroundState::InAir:
        out.ground = CharacterGround::InAir;
        break;
    }
    for (const auto& [id, b] : s.bodies)
        if (b.id == c.GetGroundBodyID())
            out.supporting_entity = b.ref;
    for (const auto& [id, b] : s.characters)
        if (b.owner->native().GetInnerBodyID() == c.GetGroundBodyID())
            out.supporting_entity = b.ref;
    out.crouched = value.owner->crouched();
    out.shape_change_blocked = value.shape_blocked;
    out.jump_accepted = value.owner->last_jump_accepted();
    return out;
}
void PhysicsRuntime::move_character(EntityRef ref, Double3 v) {
    for (auto x : v)
        if (!std::isfinite(x) || std::abs(x) > 1000000)
            throw std::runtime_error("Invalid character velocity");
    Impl::CharacterCommand c;
    c.ref = ref;
    c.velocity = v;
    impl_->queue_character(c);
}
void PhysicsRuntime::jump_character(EntityRef ref, float speed) {
    if (!std::isfinite(speed) || speed < 0 || speed > 1000000)
        throw std::runtime_error("Invalid character jump speed");
    Impl::CharacterCommand c;
    c.ref = ref;
    c.kind = 1;
    c.speed = speed;
    impl_->queue_character(c);
}
void PhysicsRuntime::crouch_character(EntityRef ref, bool value) {
    Impl::CharacterCommand c;
    c.ref = ref;
    c.kind = 2;
    c.flag = value;
    impl_->queue_character(c);
}
void PhysicsRuntime::place_character(EntityRef ref, LocalTranslation p, LocalRotation q,
                                     bool clear) {
    valid_position(p);
    q = normalized(q);
    Impl::CharacterCommand c;
    c.ref = ref;
    c.kind = 3;
    c.position = p;
    c.rotation = q;
    c.flag = clear;
    impl_->queue_character(c);
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
    Json resources = Json::array();
    for (const auto& [id, ticket] : s.collision_tickets) {
        const auto info = ticket.inspect();
        const auto current = s.collision_pool->current({id});
        resources.push_back(
            {{"asset", id},
             {"ready", bool(current)},
             {"revision", current ? current.identity().revision : ""},
             {"diagnostic",
              s.collision_errors.contains(id) ? s.collision_errors.at(id) : info.diagnostic}});
    }
    return {{"characters", s.characters.size()}, {"collision_resources", std::move(resources)},
            {"bodies", s.bodies.size()},         {"active_dynamic", active},
            {"sleeping_dynamic", sleeping},      {"tick", s.tick},
            {"gravity", s.config.gravity},       {"layers", s.config.layers}};
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
    const auto chars = s.desired_characters();
    if (chars.size() != s.characters.size())
        throw std::runtime_error("Character checkpoint requires synchronized content");
    for (const auto& [id, c] : s.characters)
        if (!chars.contains(id) || chars.at(id).first != c.config ||
            chars.at(id).second.scale != c.last.scale ||
            !equivalent(chars.at(id).second.translation, c.last.translation) ||
            !equivalent(chars.at(id).second.rotation, c.last.rotation))
            throw std::runtime_error("Character configuration or pose changed after adoption");
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
                {"layers", s.config.layers},
                {"tick", s.tick},
                {"mapping", mapping},
                {"solver", hex(bytes)},
                {"bytes", bytes.size()}};
    out["characters"] = Json::array();
    auto native_bytes = bytes.size();
    for (const auto& [id, c] : s.characters) {
        JPH::StateRecorderImpl state;
        c.owner->native().SaveState(state);
        const auto raw = state.GetData();
        native_bytes += raw.size();
        if (native_bytes > max_solver_bytes)
            throw std::runtime_error(
                "Combined character/physics checkpoint exceeds solver byte budget");
        out["characters"].push_back(
            {{"entity", c.ref},
             {"body", c.owner->native().GetInnerBodyID().GetIndexAndSequenceNumber()},
             {"configuration", s.character_configuration(id, c.last.scale)},
             {"solver", hex(raw)},
             {"bytes", raw.size()},
             {"intent", c.owner->intent()},
             {"takeoff", c.owner->takeoff_velocity()},
             {"jump", c.owner->pending_jump()},
             {"jump_accepted", c.owner->last_jump_accepted()},
             {"crouched", c.owner->crouched()},
             {"shape_blocked", c.shape_blocked}});
    }
    out["character_commands"] = Json::array();
    for (const auto& c : s.character_commands)
        out["character_commands"].push_back(
            {{"entity", c.ref},
             {"kind", c.kind},
             {"velocity", c.velocity},
             {"speed", c.speed},
             {"flag", c.flag},
             {"position", {c.position.x, c.position.y, c.position.z}},
             {"rotation", {c.rotation.x, c.rotation.y, c.rotation.z, c.rotation.w}}});
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
    if (!s.bodies.empty() || !s.characters.empty() || s.tick || !s.commands.empty() ||
        !s.character_commands.empty())
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
        checked.at("gravity") != Json(s.config.gravity) ||
        checked.at("layers") != Json(s.config.layers))
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
    const auto desired_chars = s.desired_characters();
    const auto& character_data = checked.at("characters");
    if (!character_data.is_array() || character_data.size() != desired_chars.size())
        throw std::runtime_error("Character checkpoint count mismatch");
    std::size_t native_bytes = bytes.size();
    for (const auto& item : character_data) {
        const auto ref = item.at("entity").get<EntityRef>();
        const auto found = s.context.resolve(ref);
        const JPH::BodyID body(item.at("body").get<std::uint32_t>());
        const auto raw = unhex(item.at("solver").get<std::string>());
        native_bytes += raw.size();
        if (found.state != WorldContext::ResolveState::Available ||
            !desired_chars.contains(found.entity) || !entities.insert(found.entity).second ||
            body.IsInvalid() || body.GetIndex() >= max_bodies ||
            !ids.insert(body.GetIndexAndSequenceNumber()).second ||
            !indices.insert(body.GetIndex()).second ||
            item.at("configuration") !=
                s.character_configuration(found.entity,
                                          desired_chars.at(found.entity).second.scale) ||
            raw.empty() || raw.size() != item.at("bytes").get<std::size_t>() ||
            native_bytes > max_solver_bytes)
            throw std::runtime_error("Character checkpoint identity/configuration/byte mismatch");
    }
    std::vector<Impl::CharacterCommand> character_commands;
    const auto& pending_characters = checked.at("character_commands");
    if (!pending_characters.is_array() || pending_characters.size() > 4096)
        throw std::runtime_error("Invalid character recovery commands");
    for (const auto& item : pending_characters) {
        Impl::CharacterCommand c;
        c.ref = item.at("entity").get<EntityRef>();
        c.kind = item.at("kind").get<unsigned>();
        c.velocity = item.at("velocity").get<Double3>();
        c.speed = item.at("speed").get<float>();
        c.flag = item.at("flag").get<bool>();
        const auto p = item.at("position").get<Double3>();
        const auto q = item.at("rotation").get<std::array<float, 4>>();
        c.position = {p[0], p[1], p[2]};
        c.rotation = normalized({q[0], q[1], q[2], q[3]});
        valid_position(c.position);
        const auto resolved = s.context.resolve(c.ref);
        if (c.kind > 3 || resolved.state != WorldContext::ResolveState::Available ||
            !desired_chars.contains(resolved.entity) || !std::isfinite(c.speed) || c.speed < 0 ||
            c.speed > 1000000)
            throw std::runtime_error("Invalid character recovery command");
        for (auto v : c.velocity)
            if (!std::isfinite(v) || std::abs(v) > 1000000)
                throw std::runtime_error("Invalid character recovery velocity");
        character_commands.push_back(c);
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
    for (const auto& item : character_data) {
        const auto ref = item.at("entity").get<EntityRef>();
        const auto id = s.context.resolve(ref).entity;
        const auto& [config, pose] = desired_chars.at(id);
        auto owner = std::make_unique<physics_detail::Character>(
            s.system, s.allocator, config, pose, s.group_filter.GetPtr(),
            JPH::BodyID(item.at("body").get<std::uint32_t>()));
        owner->restore_motion(item.at("intent").get<Double3>(), item.at("takeoff").get<Double3>(),
                              item.at("jump").get<float>(), item.at("crouched").get<bool>(),
                              item.at("jump_accepted").get<bool>());
        s.characters.emplace(id, Impl::CharacterRecord{ref, config, pose, std::move(owner),
                                                       item.at("shape_blocked").get<bool>()});
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
    for (const auto& item : character_data) {
        const auto id = s.context.resolve(item.at("entity").get<EntityRef>()).entity;
        const auto raw = unhex(item.at("solver").get<std::string>());
        JPH::StateRecorderImpl state;
        state.WriteBytes(raw.data(), raw.size());
        state.Rewind();
        auto& c = s.characters.at(id).owner->native();
        const auto expected_rotation = c.GetRotation();
        c.RestoreState(state);
        if (state.IsFailed())
            throw std::runtime_error("Truncated native character checkpoint");
        char trailing{};
        state.ReadBytes(&trailing, 1);
        if (!state.IsEOF())
            throw std::runtime_error("Trailing native character checkpoint bytes");
        const auto pos = c.GetPosition();
        valid_position({pos.GetX(), pos.GetY(), pos.GetZ()});
        const auto q = c.GetRotation();
        if (!q.IsNormalized() ||
            !equivalent(normalized({q.GetX(), q.GetY(), q.GetZ(), q.GetW()}),
                        LocalRotation{expected_rotation.GetX(), expected_rotation.GetY(),
                                      expected_rotation.GetZ(), expected_rotation.GetW()}))
            throw std::runtime_error(
                "Character checkpoint orientation differs from restored scene");
        const auto velocity = c.GetLinearVelocity();
        for (auto v : {velocity.GetX(), velocity.GetY(), velocity.GetZ()})
            if (!std::isfinite(v))
                throw std::runtime_error("Character checkpoint contains nonfinite velocity");
        const auto& expected = s.characters.at(id).last.translation;
        if (!equivalent(LocalTranslation{pos.GetX(), pos.GetY(), pos.GetZ()}, expected))
            throw std::runtime_error("Character checkpoint pose differs from restored scene");
    }
    s.character_commands = std::move(character_commands);
    s.tick = checked.at("tick").get<std::uint64_t>();
    adopt();
    s.commands = std::move(commands);
    for (const auto& [entity, b] : s.bodies)
        s.snaps.push_back(entity);
}
EngineModule physics_module(PhysicsConfig config, std::filesystem::path root) {
    config.validate();
    auto module = physics_schema_module();
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.allowed_services = 11;
    module.provided_services = capability(Capability::Physics);
    module.start = [config, root = std::move(root)](ModuleContext& c) {
        if (!c.owner)
            throw std::runtime_error("Physics module requires a WorldContext owner");
        auto service = std::make_shared<PhysicsRuntime>(*c.owner, config, root);
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
