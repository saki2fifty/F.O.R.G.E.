#pragma once
#include <forge/sdk_client.hpp>
// Call from an ordinary Gameplay System. The incoming duration is in seconds.
// The copied entity reference belongs to this world's current entity handle.
inline bool forge_seek_clip(const ForgeSdkWorldV1* host, std::uint64_t entity, double seconds) {
    const forge::sdk::Client client(host);
    ForgeSdkEntityRefV1 ref{};
    return client.entity_reference(entity, ref) == 1 &&
           client.animation_seek(ref.scene_uuid, ref.entity_uuid, seconds) == 1;
}
