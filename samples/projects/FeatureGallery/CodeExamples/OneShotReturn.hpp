#pragma once
// Optional original helper for a PostPhysics System that declares Animator writes.
// Pass the callback's Flecs stage. The caller selects ordinary stable EntityRefs.
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk.h>
#include <forge/sdk_client.hpp>
inline bool forge_return_on_animation_completion(const ForgeSdkWorldV1* host, flecs::world stage,
                                                 const char* scene, const char* entity,
                                                 uint32_t return_state) {
    ForgeSdkAnimationPlaybackV1 status{};
    if (forge::sdk::Client(host).animation_playback(scene, entity, status) != 1 ||
        !status.finished || !status.state || !return_state || status.state == return_state)
        return false;
    auto target = stage.entity(status.native_entity);
    if (!target.is_alive() || !target.has<forge::Animator>())
        return false;
    auto request = target.get<forge::Animator>();
    // A newer explicit request takes precedence over this last-realized sample.
    if (!request.states.id || request.states.id.str() != status.states_uuid ||
        request.state != status.state)
        return false;
    request.state = return_state;
    target.set(request);
    return true;
}
