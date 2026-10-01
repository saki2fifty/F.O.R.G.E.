#pragma once
// FORGE-managed source registration. Create C++ Component/System updates the
// marked lines; Flecs remains the only runtime component/system authority.
#include <forge/native_sdk.h>
// FORGE_COMPONENT_INCLUDES
// FORGE_SYSTEM_DECLARATIONS
inline int32_t forge_register_components(const ForgeSdkWorldV1* host, char* error,
                                         uint32_t capacity) {
    (void)host;
    (void)error;
    (void)capacity;
    // FORGE_COMPONENT_CALLS
    return 1;
}
inline void forge_register_systems(const ForgeSdkWorldV1* host) {
    (void)host;
    // FORGE_SYSTEM_CALLS
}
