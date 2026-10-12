#pragma once
#include <cmath>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>

// Original C++ example: an unparented, unit-scale model holder with a Kinematic
// Body. Read in host->post_animation_phase, then queue the existing physics move.
// A controller with a parent, visual scale or character collision policy should
// explicitly convert the delta and use its own physics/character operation.
inline bool forge_root_motion_kinematic(const ForgeSdkWorldV1* host, const char* scene,
                                        const char* entity, const forge::LocalTranslation& position,
                                        const forge::LocalRotation& rotation) {
    const forge::sdk::Client client(host);
    ForgeSdkAnimationRootMotionV1 delta{};
    if (client.animation_root_motion(scene, entity, delta) != 1 || delta.applied ||
        !client.callable(forge::sdk::Capability::Physics))
        return false;
    const double cross[] = {rotation.y * delta.translation[2] - rotation.z * delta.translation[1],
                            rotation.z * delta.translation[0] - rotation.x * delta.translation[2],
                            rotation.x * delta.translation[1] - rotation.y * delta.translation[0]};
    const double target[] = {
        position.x + delta.translation[0] +
            2 * (rotation.w * cross[0] + rotation.y * cross[2] - rotation.z * cross[1]),
        position.y + delta.translation[1] +
            2 * (rotation.w * cross[1] + rotation.z * cross[0] - rotation.x * cross[2]),
        position.z + delta.translation[2] +
            2 * (rotation.w * cross[2] + rotation.x * cross[1] - rotation.y * cross[0])};
    const auto* q = delta.rotation_xyzw;
    float next[] = {rotation.w * q[0] + rotation.x * q[3] + rotation.y * q[2] - rotation.z * q[1],
                    rotation.w * q[1] - rotation.x * q[2] + rotation.y * q[3] + rotation.z * q[0],
                    rotation.w * q[2] + rotation.x * q[1] - rotation.y * q[0] + rotation.z * q[3],
                    rotation.w * q[3] - rotation.x * q[0] - rotation.y * q[1] - rotation.z * q[2]};
    double length = 0;
    for (const auto value : next)
        length += double(value) * value;
    if (!std::isfinite(length) || length < 1e-12)
        return false;
    for (auto& value : next)
        value = float(value / std::sqrt(length));
    return host->physics_move &&
           host->physics_move(host->context, scene, entity, target, next, 1, 0) == 1;
}
