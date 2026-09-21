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
#define FORGE_SDK_PHYSICS 8u
#define FORGE_SDK_AUDIO 16u
#define FORGE_SDK_NAVIGATION 32u
#define FORGE_SDK_UI 64u
/* Intrinsic fixed-input query ID, not a descriptor permission/provider bit. */
#define FORGE_SDK_INPUT 128u
#define FORGE_SDK_CAPABILITY_VERSION 1u
#define FORGE_SDK_FIXED_ONLY 1u
#define FORGE_SDK_OWNER_THREAD 2u
typedef struct ForgeSdkCapabilityV1 {
    uint32_t size, version, available, callable, flags;
} ForgeSdkCapabilityV1;
typedef enum ForgeSdkNavStatusV1 {
    FORGE_SDK_NAV_SUCCESS = 0,
    FORGE_SDK_NAV_PARTIAL,
    FORGE_SDK_NAV_MISSING,
    FORGE_SDK_NAV_STALE,
    FORGE_SDK_NAV_START_OUTSIDE,
    FORGE_SDK_NAV_END_OUTSIDE,
    FORGE_SDK_NAV_NO_PATH,
    FORGE_SDK_NAV_LIMIT,
    FORGE_SDK_NAV_INVALID,
    FORGE_SDK_NAV_UNAVAILABLE
} ForgeSdkNavStatusV1;
typedef struct ForgeSdkPhysicsHitV1 {
    uint32_t size;
    char scene[37], entity[37]; /* FORGE UUIDs, never Jolt BodyID */
    double position[3], normal[3], fraction;
} ForgeSdkPhysicsHitV1;
typedef struct ForgeSdkActionV1 {
    uint32_t size;
    uint32_t held, pressed, released;
    double x, y;
    uint64_t tick;
} ForgeSdkActionV1;
/* Navigation statuses: 0 success, 1 partial (no points), 2 missing, 3 stale,
   4 start outside, 5 end outside, 6 no path, 7 limit, 8 invalid, 9 unavailable. */
typedef struct ForgeSdkNavResultV1 {
    uint32_t size, status, count, capacity;
    double* xyz; /* Caller-owned capacity*3 doubles; capacity 1..64. */
} ForgeSdkNavResultV1;
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
    uint64_t post_physics_phase;
    /* Owner fixed-tick thread. 1 hit, 0 miss, -1 unavailable/invalid. */
    int32_t(FORGE_SDK_CALL* raycast)(void*, const double origin[3], const double displacement[3],
                                     ForgeSdkPhysicsHitV1*);
    /* Queued for next pre-physics boundary. motion: 0 teleport, 1 kinematic target. */
    int32_t(FORGE_SDK_CALL* physics_move)(void*, const char* scene_uuid, const char* entity_uuid,
                                          const double position[3], const float rotation_xyzw[4],
                                          uint32_t motion, uint32_t clear_velocity);
    /* Fixed owner thread; source is (scene AssetId, EntityId). 0 stop, 1 restart/play.
       Returns 1 queued, 0 invalid/unavailable. No device/sample-clock authority. */
    int32_t(FORGE_SDK_CALL* audio_source)(void*, const char* scene_uuid, const char* entity_uuid,
                                          uint32_t play);
    /* Fixed owner tick; operation 0 projects start, 1 finds start-to-end path.
       1 means a status was returned, 0 invalid callback arguments/unavailable tick.
       Failure returns no points; undersized buffers report limit without truncation. */
    int32_t(FORGE_SDK_CALL* navigation_query)(void*, const char* navmesh_uuid, uint32_t operation,
                                              const double start[3], const double end[3],
                                              ForgeSdkNavResultV1*);
    /* Private exact-build UI bridge. No RmlUi/native callbacks cross to presentation.
       Register an argument-free semantic action during module start. */
    int32_t(FORGE_SDK_CALL* ui_allow_action)(void*, const char* command);
    /* Fixed owner tick only. Publish a finite copied number for an authored entity. */
    int32_t(FORGE_SDK_CALL* ui_publish_number)(void*, const char* entity_uuid, const char* name,
                                               double value);
    /* Fixed owner tick: 1 event, 0 no event/unavailable, -1 invalid. Entity buffer
       is caller-owned and must hold at least 37 bytes. Consumes only this command. */
    int32_t(FORGE_SDK_CALL* ui_poll_action)(void*, const char* command, char* entity_uuid,
                                            uint32_t capacity);
    /* Live query, never cache availability across ticks/start/stop. Returns 1 for
       recognized ID (even absent provider/version mismatch); 0 invalid/unknown.
       requested_version must match version for available/callable to be set.
       Ui callable denotes fixed publish/poll; allow_action is startup-only. */
    int32_t(FORGE_SDK_CALL* query_capability)(void*, uint32_t capability,
                                              uint32_t requested_version, ForgeSdkCapabilityV1*);
    /* Bounded copied CPU timing sample. Instrumentation only, never gameplay time.
       Returns 1 accepted (also when recording disabled), 0 invalid/unavailable. */
    int32_t(FORGE_SDK_CALL* profile_sample)(void*, const char* name, double seconds);
    /* Schema stage only. Opts a fully reflected plain-value native component into
       safe authoring. Native ID stays process-local; defaults are bounded JSON.
       Structure is read from native Meta. No project code runs in the editor.
       Returns1 accepted;0 rejected with caller-owned diagnostic (capacity1..8192).
       This does not enable arbitrary pointers, resources or opaque project values. */
    int32_t(FORGE_SDK_CALL* authoring_type)(void*, uint64_t native_type, const char* type_key,
                                            uint32_t schema_version, const char* defaults_json,
                                            const char* category, char* error, uint32_t capacity);
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
