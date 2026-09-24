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
#define FORGE_SDK_RESOURCES 256u
/* Intrinsic owner-thread runtime entity requests; not a descriptor permission bit. */
#define FORGE_SDK_RUNTIME_ENTITIES 512u
#define FORGE_SDK_CONTROL_INPUT 1024u
#define FORGE_SDK_GAME 2048u
#define FORGE_SDK_CAPABILITY_VERSION 1u
#define FORGE_SDK_FIXED_ONLY 1u
#define FORGE_SDK_OWNER_THREAD 2u
#define FORGE_SDK_CONTROL_ONLY 4u
typedef struct ForgeSdkCapabilityV1 {
    uint32_t size, version, available, callable, flags;
} ForgeSdkCapabilityV1;
typedef struct ForgeSdkSaveSchemaV1 {
    uint32_t size, version;
    void* userdata;
    /* Input is {scene: AssetId, data: explicitly admitted game state}. */
    int32_t(FORGE_SDK_CALL* validate)(void*, const char* save_json, char* error, uint32_t capacity);
    /* Pure N -> N+1 migration; output capacity is 1 MiB including terminator.
       Return 0 on failure; never retain any borrowed input/output pointers. */
    int32_t(FORGE_SDK_CALL* migrate)(void*, uint32_t from_version, const char* save_json,
                                     char* output, uint32_t capacity);
    const uint32_t* migration_versions;
    uint32_t migration_count;
} ForgeSdkSaveSchemaV1;
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
typedef struct ForgeSdkSweepV1 {
    uint32_t size, shape, layer_mask, include_sensors;
    double dimensions[3], origin[3], displacement[3];
    float rotation[4]; /* normalized XYZW; identity is0,0,0,1 */
} ForgeSdkSweepV1;
typedef struct ForgeSdkCharacterV1 {
    uint32_t size, ground, crouched, shape_change_blocked, jump_accepted;
    double position[3], velocity[3], ground_position[3], ground_normal[3], ground_velocity[3];
    float rotation_xyzw[4];
    char support_scene[37], support_entity[37]; /* empty when unsupported */
} ForgeSdkCharacterV1;
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
/* CPU admission states use the engine resource state names. A retained revision
   can remain usable while a replacement is pending/failed. No GPU readiness claim. */
#define FORGE_SDK_RESOURCE_MESH 1u
#define FORGE_SDK_RESOURCE_MATERIAL 2u
#define FORGE_SDK_RESOURCE_TEXTURE 3u
#define FORGE_SDK_RESOURCE_SHADER 4u
#define FORGE_SDK_RESOURCE_COLLISION 5u
#define FORGE_SDK_TEXTURE_AUTOMATIC 0u
#define FORGE_SDK_TEXTURE_COLOR 1u
#define FORGE_SDK_TEXTURE_DATA 2u
#define FORGE_SDK_TEXTURE_NORMAL 3u
#define FORGE_SDK_TEXTURE_HDR_COLOR 4u
typedef struct ForgeSdkResourceV1 {
    uint32_t size;
    char state[32], requested_revision[65], retained_revision[65], diagnostic[1024];
    uint64_t source_generation;
} ForgeSdkResourceV1;
#define FORGE_SDK_ENTITY_PENDING 1u
#define FORGE_SDK_ENTITY_READY 2u
#define FORGE_SDK_ENTITY_FAILED 3u
#define FORGE_SDK_ENTITY_GONE 4u
typedef struct ForgeSdkEntityV1 {
    uint32_t size, state;
    char scene_uuid[37], entity_uuid[37], diagnostic[1024];
    uint64_t native_entity; /* Borrowed Flecs handle in this world only when Ready. */
} ForgeSdkEntityV1;
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
    /* Fixed tick or control frame. Publish a finite copied number for an authored entity. */
    int32_t(FORGE_SDK_CALL* ui_publish_number)(void*, const char* entity_uuid, const char* name,
                                               double value);
    /* Fixed tick or control frame: 1 event, 0 no event/unavailable, -1 invalid. Entity buffer
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
    /* Runtime owner thread, start/running only. Zero is rejection. Tokens belong
       only to this module/world; release on stop is automatic. Maximum64/module,
       256/world. Copied observation only; never native data/device ownership. */
    /* Texture variant: 0 automatic HDR/color, 1 color, 2 data, 3 normal, 4 HDR.
       Non-texture requests require0. Variants must already be published. */
    uint64_t(FORGE_SDK_CALL* resource_request)(void*, uint32_t kind, const char* asset_uuid,
                                               uint32_t texture_variant);
    int32_t(FORGE_SDK_CALL* resource_inspect)(void*, uint64_t token, ForgeSdkResourceV1*);
    int32_t(FORGE_SDK_CALL* resource_release)(void*, uint64_t token);
    /* Queue an empty registered scene entity for the next fixed boundary.
       NULL/empty scene UUID requires exactly one loaded scene at publication.
       Name is1..255UTF-8 bytes. Host generates EntityId. Poll Ready, then set
       native render/gameplay components through ordinary Flecs APIs.
       Release cancels an unstarted request or drops a completed observation;
       it never deletes an already-created entity. Limits64/module,256/world. */
    uint64_t(FORGE_SDK_CALL* entity_request)(void*, const char* scene_uuid, const char* name);
    int32_t(FORGE_SDK_CALL* entity_inspect)(void*, uint64_t token, ForgeSdkEntityV1*);
    int32_t(FORGE_SDK_CALL* entity_release)(void*, uint64_t token);
    /* Fixed owner thread; copied observations, no native character/body pointers.
       ground:0 OnGround,1 OnSteepGround,2 NotSupported,3 InAir. */
    int32_t(FORGE_SDK_CALL* character_state)(void*, const char* scene_uuid, const char* entity_uuid,
                                             ForgeSdkCharacterV1*);
    /* Queued:0 planar velocity,1 jump speed,2 crouch flag,3 checked placement.
       value[3] required for0/3, rotation[4] for3, flag is crouched/clear velocity. */
    int32_t(FORGE_SDK_CALL* character_command)(void*, const char* scene_uuid,
                                               const char* entity_uuid, uint32_t operation,
                                               const double value[3], const float rotation_xyzw[4],
                                               float speed, uint32_t flag);
    int32_t(FORGE_SDK_CALL* raycast_filtered)(void*, const double origin[3],
                                              const double displacement[3], uint32_t layer_mask,
                                              uint32_t include_sensors, ForgeSdkPhysicsHitV1*);
    /* Linear Box/Sphere/Capsule/Cylinder sweep(0..3).1 hit,0 miss,-1 invalid. */
    int32_t(FORGE_SDK_CALL* shape_cast)(void*, const ForgeSdkSweepV1*, ForgeSdkPhysicsHitV1*);
    /* Control callback only. Sequence is a control-frame index, NOT simulation time. */
    int32_t(FORGE_SDK_CALL* read_control)(void*, const char* action_uuid, ForgeSdkActionV1*);
    /* Copied bounded JSON. Query/inspect return required bytes including NUL;
       output is written only when capacity suffices. 0 means rejection.
       Requests execute only after the calling module callback returns. */
    uint32_t(FORGE_SDK_CALL* game_query)(void*, char* output, uint32_t capacity);
    uint64_t(FORGE_SDK_CALL* game_request)(void*, const char* command_json);
    uint32_t(FORGE_SDK_CALL* game_inspect)(void*, uint64_t token, char* output, uint32_t capacity);
    int32_t(FORGE_SDK_CALL* game_release)(void*, uint64_t token);
    int32_t(FORGE_SDK_CALL* game_save_schema)(void*, const ForgeSdkSaveSchemaV1*);
    /* Explicit opt-in to one bounded string argument; module startup only. */
    int32_t(FORGE_SDK_CALL* ui_allow_value_action)(void*, const char* command);
    /* Publish a copied protocol-supported value in initialization, fixed tick
       or control frame; no native presentation pointers. */
    int32_t(FORGE_SDK_CALL* ui_publish_json)(void*, const char* entity_uuid, const char* name,
                                             const char* value_json);
    /* Copied {entity,command,value}; two-pass sizing retains the event until a
       sufficient output buffer is supplied. 0 means no event/rejection. */
    uint32_t(FORGE_SDK_CALL* ui_poll_event)(void*, const char* command, char* output,
                                            uint32_t capacity);
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
    /* Optional owner-thread menu/control callback. Runs while paused, outside
       the fixed pipeline. Return 0 to fault the active world with a diagnostic.
       Session requests must be queued, never replace the world from this callback. */
    int32_t(FORGE_SDK_CALL* controls)(const ForgeSdkWorldV1*, char* error, uint32_t capacity);
    /* Candidate only, after scene restoration and before physics realization.
       Restore only the game's explicitly admitted state; failure rejects candidate. */
    int32_t(FORGE_SDK_CALL* restore)(const ForgeSdkWorldV1*, const char* state_json, char* error,
                                     uint32_t capacity);
    /* Optional scene initialization, before saved-state restoration and physics.
       No simulation tick or gameplay input; failure rejects the candidate. */
    int32_t(FORGE_SDK_CALL* scene_ready)(const ForgeSdkWorldV1*, char* error, uint32_t capacity);
} ForgeNativeSdkV1;
typedef const ForgeNativeSdkV1*(FORGE_SDK_CALL* ForgeNativeSdkEntryV1)(void);
FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1(void);
#ifdef __cplusplus
}
#endif
#endif
