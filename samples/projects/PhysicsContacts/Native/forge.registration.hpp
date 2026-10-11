#pragma once
#include <forge/native_sdk.h>
// FORGE_COMPONENT_INCLUDES
#include "Components/ContactReaction.hpp"
// FORGE_SYSTEM_DECLARATIONS
void forge_register_system_ContactReactionSystem(const ForgeSdkWorldV1*);
inline int32_t forge_register_components(const ForgeSdkWorldV1* host, char* error,
                                         uint32_t capacity) {
    // FORGE_COMPONENT_CALLS
    if (!forge_register_component_ContactReaction(host, error, capacity))
        return 0;
    return 1;
}
inline void forge_register_systems(const ForgeSdkWorldV1* host) {
    // FORGE_SYSTEM_CALLS
    forge_register_system_ContactReactionSystem(host);
}
