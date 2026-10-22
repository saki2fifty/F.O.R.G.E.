#pragma once
#include <forge/native_sdk.h>
// FORGE_COMPONENT_INCLUDES
#include "Components/CharacterMovement2D.hpp"
// FORGE_SYSTEM_DECLARATIONS
void forge_register_system_CharacterMovement2DSystem(const ForgeSdkWorldV1*);
inline int32_t forge_register_components(const ForgeSdkWorldV1* host, char* error,
                                         uint32_t capacity) {
    // FORGE_COMPONENT_CALLS
    return forge_register_component_CharacterMovement2D(host, error, capacity);
}
inline void forge_register_systems(const ForgeSdkWorldV1* host) {
    // FORGE_SYSTEM_CALLS
    forge_register_system_CharacterMovement2DSystem(host);
}
