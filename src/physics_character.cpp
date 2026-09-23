// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "physics_character.hpp"
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numbers>
namespace forge::physics_detail {
namespace {
void bounded(float v, float low, float high, const char* name) {
    if (!std::isfinite(v) || v < low || v > high)
        throw std::runtime_error(std::string("Character ") + name + " is outside supported bounds");
}
JPH::Vec3 velocity(Double3 value) {
    for (auto v : value)
        if (!std::isfinite(v) || std::abs(v) > 1000000)
            throw std::runtime_error("Character velocity must be finite and within +/-1000000 m/s");
    return {float(value[0]), float(value[1]), float(value[2])};
}
Double3 copied(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
class Filter final : public JPH::BodyFilter {
    std::uint32_t layer_, mask_;
    JPH::BodyID ignore_;

  public:
    explicit Filter(const CharacterController& c, JPH::BodyID ignore = {})
        : layer_(c.layer), mask_(c.mask), ignore_(ignore) {}
    bool ShouldCollide(const JPH::BodyID& id) const override { return id != ignore_; }
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if (body.IsSensor())
            return false;
        const auto& group = body.GetCollisionGroup();
        return group.GetSubGroupID() < 32 &&
               (mask_ & (std::uint32_t{1} << group.GetSubGroupID())) &&
               (group.GetGroupID() & (std::uint32_t{1} << layer_));
    }
};
JPH::RefConst<JPH::Shape> checked(JPH::Shape::ShapeResult result) {
    if (result.HasError())
        throw std::runtime_error("Character shape: " + std::string(result.GetError()));
    return result.Get();
}
} // namespace
void validate_character(const CharacterController& c) {
    if (c.shape > 1 || c.layer > 31)
        throw std::runtime_error("Invalid character shape/layer");
    bounded(c.radius, .001f, 10000, "radius");
    bounded(c.height, .001f, 10000, "height");
    bounded(c.crouch_height, .001f, c.height, "crouch height");
    bounded(c.mass, .001f, 1000000, "mass");
    bounded(c.max_strength, 0, 1000000, "push strength");
    bounded(c.max_slope, 0, 90.f, "slope angle");
    bounded(c.step_height, 0, 10000, "step height");
    bounded(c.step_forward, .001f, 10000, "step forward probe");
    bounded(c.floor_probe, 0, 10000, "floor probe");
    bounded(c.gravity_factor, 0, 1000, "gravity factor");
}
CharacterShapes character_shapes(const CharacterController& c, LocalScale scale) {
    validate_character(c);
    const JPH::Vec3 signed_scale(scale.x, scale.y, scale.z);
    const bool valid = c.shape == 0 ? JPH::CapsuleShape(.5f, .5f).IsValidScale(signed_scale)
                                    : JPH::CylinderShape(.5f, .5f).IsValidScale(signed_scale);
    if (!valid)
        throw std::runtime_error(
            "Character shape cannot represent this world scale; visual scale remains unchanged");
    const float radius = c.radius * std::abs(scale.x), height = c.height * std::abs(scale.y),
                crouch = c.crouch_height * std::abs(scale.y);
    bounded(radius, .001f, 10000, "scaled radius");
    bounded(height, .001f, 10000, "scaled height");
    bounded(crouch, .001f, 10000, "scaled crouch height");
    auto make = [&](float h) {
        const auto solid = c.shape == 0
                               ? checked(JPH::CapsuleShapeSettings(h * .5f, radius).Create())
                               : checked(JPH::CylinderShapeSettings(h * .5f, radius).Create());
        const auto offset = h * .5f + (c.shape == 0 ? radius : 0);
        return checked(
            JPH::RotatedTranslatedShapeSettings({0, offset, 0}, JPH::Quat::sIdentity(), solid)
                .Create());
    };
    return {make(height), make(crouch), radius};
}
Character::Character(JPH::PhysicsSystem& system, JPH::TempAllocator& allocator,
                     CharacterController config, LocalTransform pose,
                     const JPH::GroupFilter* group_filter, JPH::BodyID requested)
    : system_(system), allocator_(allocator), config_(config),
      shapes_(character_shapes(config, pose.scale)) {
    auto q = normalized(pose.rotation);
    JPH::Quat orientation(q.x, q.y, q.z, q.w);
    // Capsule/cylinder radial reflections preserve their solid. A negative local
    // Y reflects the feet offset too, so flip the native axis, not authored TRS.
    inverted_ = pose.scale.y < 0;
    if (inverted_)
        orientation =
            orientation * JPH::Quat::sRotation(JPH::Vec3::sAxisX(), std::numbers::pi_v<float>);
    JPH::CharacterVirtualSettings settings;
    settings.mShape = settings.mInnerBodyShape = shapes_.standing;
    settings.mInnerBodyLayer = 1;
    settings.mInnerBodyIDOverride = requested;
    settings.mUp = orientation * JPH::Vec3::sAxisY();
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -shapes_.radius);
    settings.mMaxSlopeAngle = config.max_slope * std::numbers::pi_v<float> / 180;
    settings.mMass = config.mass;
    settings.mMaxStrength = config.max_strength;
    settings.mEnhancedInternalEdgeRemoval = true;
    character_ = new JPH::CharacterVirtual(
        &settings, {pose.translation.x, pose.translation.y, pose.translation.z}, orientation,
        &system_);
    if (character_->GetInnerBodyID().IsInvalid())
        throw std::runtime_error("Character inner body capacity exhausted");
    system_.GetBodyInterface().SetCollisionGroup(
        character_->GetInnerBodyID(), JPH::CollisionGroup(group_filter, config.mask, config.layer));
}
void Character::movement(Double3 value) {
    auto v = velocity(value);
    if (std::abs(v.Dot(character_->GetUp())) > 1e-5f * std::max(1.f, v.Length()))
        throw std::runtime_error(
            "Character movement must be planar to its up axis; use jump for takeoff");
    intent_ = v;
}
void Character::jump(float speed) {
    bounded(speed, 0, 1000000, "jump speed");
    jump_ = speed;
}
bool Character::crouch(bool value) {
    if (value == crouched_)
        return true;
    const auto& shape = value ? shapes_.crouched : shapes_.standing;
    if (!character_->SetShape(shape, .001f, system_.GetDefaultBroadPhaseLayerFilter(1),
                              system_.GetDefaultLayerFilter(1), Filter(config_), {}, allocator_))
        return false;
    character_->SetInnerBodyShape(shape);
    crouched_ = value;
    return true;
}
void Character::refresh(JPH::BodyID ignore) {
    character_->RefreshContacts(system_.GetDefaultBroadPhaseLayerFilter(1),
                                system_.GetDefaultLayerFilter(1), Filter(config_, ignore), {},
                                allocator_);
    if (character_->GetMaxHitsExceeded())
        throw std::runtime_error("Character contact budget exceeded");
}
void Character::step(float dt, Double3 gravity) {
    if (!std::isfinite(dt) || dt <= 0)
        throw std::runtime_error("Character requires positive fixed dt");
    auto& c = *character_;
    c.UpdateGroundVelocity();
    const auto up = c.GetUp(), ground = c.GetGroundVelocity();
    auto v = up * c.GetLinearVelocity().Dot(up);
    const bool grounded = c.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround &&
                          (v - ground).Dot(up) <= 1e-5f;
    last_jump_accepted_ = grounded && jump_ > 0;
    if (grounded) {
        v = ground;
        takeoff_ = ground - up * ground.Dot(up);
        if (jump_ > 0)
            v += up * jump_;
    } else
        v += takeoff_;
    jump_ = 0; // One request, never a deferred auto-jump on landing.
    const auto acceleration = velocity(gravity) * config_.gravity_factor;
    v += intent_ + acceleration * dt;
    c.SetLinearVelocity(v);
    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = -up * config_.floor_probe;
    update.mWalkStairsStepUp = up * config_.step_height;
    update.mWalkStairsStepForwardTest = config_.step_forward;
    c.ExtendedUpdate(dt, acceleration, update, system_.GetDefaultBroadPhaseLayerFilter(1),
                     system_.GetDefaultLayerFilter(1), Filter(config_), {}, allocator_);
    if (c.GetMaxHitsExceeded())
        throw std::runtime_error("Character contact budget exceeded");
}
bool Character::place(LocalTranslation position, LocalRotation logical, bool clear_velocity,
                      JPH::BodyID ignore) {
    for (auto v : {position.x, position.y, position.z})
        if (!std::isfinite(v) || std::abs(v) > 1e9)
            throw std::runtime_error("Character placement exceeds world bounds");
    const auto q = normalized(logical);
    JPH::Quat rotation(q.x, q.y, q.z, q.w);
    if (inverted_)
        rotation = rotation * JPH::Quat::sRotation(JPH::Vec3::sAxisX(), std::numbers::pi_v<float>);
    const JPH::RVec3 target(position.x, position.y, position.z);
    struct Placement final : JPH::CollideShapeCollector {
        bool blocked = false;
        unsigned hits = 0;
        void AddHit(const JPH::CollideShapeResult& hit) override {
            if (++hits > 256 || hit.mPenetrationDepth > .001f) {
                blocked = true;
                ForceEarlyOut();
            }
        }
    } contacts;
    character_->CheckCollision(target, rotation, JPH::Vec3::sZero(), 0, character_->GetShape(),
                               target, contacts, system_.GetDefaultBroadPhaseLayerFilter(1),
                               system_.GetDefaultLayerFilter(1), Filter(config_, ignore), {});
    if (contacts.blocked)
        return false;
    JPH::StateRecorderImpl before;
    character_->SaveState(before);
    const auto old_up = character_->GetUp();
    try {
        character_->SetPosition(target);
        character_->SetRotation(rotation);
        character_->SetUp(rotation * JPH::Vec3::sAxisY());
        refresh(ignore);
    } catch (...) {
        before.Rewind();
        character_->RestoreState(before);
        character_->SetUp(old_up);
        character_->SetPosition(character_->GetPosition());
        throw;
    }
    if (clear_velocity) {
        character_->SetLinearVelocity(JPH::Vec3::sZero());
        takeoff_ = JPH::Vec3::sZero();
        jump_ = 0;
    } else {
        // Preserve world velocity when the logical up axis changes. The next
        // tick assembles vertical velocity + planar intent + takeoff momentum.
        const auto up = character_->GetUp();
        const auto current = character_->GetLinearVelocity();
        takeoff_ = current - up * current.Dot(up) - intent_;
    }
    return true;
}
Double3 Character::intent() const { return copied(intent_); }
Double3 Character::takeoff_velocity() const { return copied(takeoff_); }
void Character::restore_motion(Double3 intent, Double3 takeoff, float jump, bool crouched,
                               bool jump_accepted) {
    last_jump_accepted_ = jump_accepted;
    movement(intent);
    takeoff_ = velocity(takeoff);
    this->jump(jump);
    // Recovery reconstructs all shapes before restoring the native contact cache.
    const auto& shape = crouched ? shapes_.crouched : shapes_.standing;
    if (!character_->SetShape(shape, FLT_MAX, system_.GetDefaultBroadPhaseLayerFilter(1),
                              system_.GetDefaultLayerFilter(1), Filter(config_), {}, allocator_))
        throw std::runtime_error("Character recovery shape reconstruction failed");
    character_->SetInnerBodyShape(shape);
    crouched_ = crouched;
}
} // namespace forge::physics_detail
