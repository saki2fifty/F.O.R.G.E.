#pragma once
// Call from Gameplay before Animation, passing a current matching-world entity handle.
// The existing world registry supplies identity; this helper stores no handles or clocks.
#include <forge/native_sdk.h>
#include <forge/sdk_client.hpp>
inline int32_t forge_control_clip(const ForgeSdkWorldV1* host, uint64_t native_entity,
                                  uint32_t operation) {
    const forge::sdk::Client client(host);
    ForgeSdkEntityRefV1 reference{};
    const auto found = client.entity_reference(native_entity, reference);
    if (found != 1)
        return found;
    return client.animation_control(reference.scene_uuid, reference.entity_uuid, operation);
}
