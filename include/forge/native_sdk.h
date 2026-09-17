#ifndef FORGE_NATIVE_SDK_H
#define FORGE_NATIVE_SDK_H
/* INTERNAL / UNSTABLE exact SDK. All pointers borrowed; owner-thread only.
   No exceptions, STL ownership or allocation ownership crosses these callbacks.
   Registering code and host context remain valid until after ecs_fini. */
#include <stdint.h>
#ifdef _WIN32
#define FORGE_SDK_CALL __cdecl
#define FORGE_SDK_EXPORT __declspec(dllexport)
#else
#define FORGE_SDK_CALL
#define FORGE_SDK_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ecs_world_t ecs_world_t;
#define FORGE_NATIVE_SDK_ABI 1u
#define FORGE_SDK_AUTHORING 1u
#define FORGE_SDK_RUNTIME 2u
#define FORGE_SDK_PREVIEW 4u
#define FORGE_SDK_VALIDATION 8u
#define FORGE_SDK_DIAGNOSTICS 1u
#define FORGE_SDK_PROFILING 2u
/* Requirement marker only: headless composition does not supply rendering. */
#define FORGE_SDK_RENDERING 4u
typedef struct ForgeSdkActionV1 {
    uint32_t size;
    uint32_t held, pressed, released;
    double x, y;
    uint64_t tick;
} ForgeSdkActionV1;
typedef struct ForgeSdkWorldV1 {
    uint32_t size, version;
    ecs_world_t* world; /* borrowed; module must not destroy it */
    void* context;
    uint32_t role, capabilities;
    uint64_t fixed_phase, fixed_tag;
    /* Returns 0 outside fixed tick, for missing action, or invalid arguments. */
    int32_t(FORGE_SDK_CALL* read_action)(void*, const char* action_uuid, ForgeSdkActionV1*);
    /* severity 0..4; context automatically includes module, role and current tick. */
    int32_t(FORGE_SDK_CALL* diagnostic)(void*, uint32_t severity, const char* text);
} ForgeSdkWorldV1;
typedef struct ForgeNativeSdkV1 {
    uint32_t size, version;
    const char* fingerprint;
    const char* module_id;
    const char* implementation_version;
    const char* const* dependencies;
    uint32_t dependency_count;
    uint32_t schema_roles, runtime_roles, required_capabilities, allowed_capabilities;
    ecs_world_t*(FORGE_SDK_CALL* flecs_init_identity)(void);
    const void* flecs_os_identity;
    int32_t(FORGE_SDK_CALL* register_schema)(const ForgeSdkWorldV1*, char* error,
                                             uint32_t capacity);
    int32_t(FORGE_SDK_CALL* start)(const ForgeSdkWorldV1*, char* error, uint32_t capacity);
    /* Stop/drain only. Flecs-owned callbacks/contexts are destroyed at world finalization. */
    void(FORGE_SDK_CALL* stop)(const ForgeSdkWorldV1*);
} ForgeNativeSdkV1;
typedef const ForgeNativeSdkV1*(FORGE_SDK_CALL* ForgeNativeSdkEntryV1)(void);
FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1(void);
#ifdef __cplusplus
}
#endif
#endif
