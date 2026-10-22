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
#define FORGE_SDK_PHYSICS_FORCE 0u
#define FORGE_SDK_PHYSICS_IMPULSE 1u
#define FORGE_SDK_PHYSICS_TORQUE 2u
#define FORGE_SDK_PHYSICS_ANGULAR_IMPULSE 3u
#define FORGE_SDK_PHYSICS_LINEAR_VELOCITY 0u
#define FORGE_SDK_PHYSICS_ANGULAR_VELOCITY 1u
#define FORGE_SDK_AUDIO 16u
#define FORGE_SDK_AUDIO_STOP 0u
#define FORGE_SDK_AUDIO_RESTART 1u
#define FORGE_SDK_AUDIO_PAUSE 2u
#define FORGE_SDK_AUDIO_RESUME 3u
#define FORGE_SDK_AUDIO_ONE_SHOT 4u
#define FORGE_SDK_NAVIGATION 32u
#define FORGE_SDK_UI 64u
/* Intrinsic fixed-input query ID, not a descriptor permission/provider bit. */
#define FORGE_SDK_INPUT 128u
#define FORGE_SDK_RESOURCES 256u
/* Intrinsic owner-thread runtime entity requests; not a descriptor permission bit. */
#define FORGE_SDK_RUNTIME_ENTITIES 512u
#define FORGE_SDK_CONTROL_INPUT 1024u
#define FORGE_SDK_GAME 2048u
#define FORGE_SDK_ANIMATION 4096u
/* Intrinsic CPU map access; not descriptor permissions or GPU readiness. */
#define FORGE_SDK_TILE_MAPS 8192u
#define FORGE_SDK_TILE_MAP_EDITS 16384u
#define FORGE_SDK_CAPABILITY_VERSION 1u
#define FORGE_SDK_FIXED_ONLY 1u
#define FORGE_SDK_OWNER_THREAD 2u
#define FORGE_SDK_CONTROL_ONLY 4u
typedef struct ForgeSdkCapabilityV1 {
    uint32_t size, version, available, callable, flags;
} ForgeSdkCapabilityV1;
typedef struct ForgeSdkAnimationEventV1 {
    uint32_t size, version, state, marker;
    uint64_t tick;
    uint64_t native_entity; /* Borrowed handle in this matching world/current tick only. */
    double offset, phase, value;
    char scene_uuid[37], entity_uuid[37], states_uuid[37], clip_uuid[37], name[65];
} ForgeSdkAnimationEventV1;
#define FORGE_SDK_ANIMATION_PLAY 1u
#define FORGE_SDK_ANIMATION_PAUSE 2u
#define FORGE_SDK_ANIMATION_RESTART 3u
typedef struct ForgeSdkEntityRefV1 {
    uint32_t size, version;
    char scene_uuid[37], entity_uuid[37];
} ForgeSdkEntityRefV1;
typedef struct ForgeSdkAnimationPlaybackV1 {
    uint32_t size, version, state;
    uint32_t enabled, loop, playing, finished, transitioning;
    uint64_t
        native_entity; /* Borrowed matching-world handle; use only during this fixed callback. */
    double time, duration, progress, speed, transition_weight, blend_weight;
    char scene_uuid[37], entity_uuid[37], states_uuid[37], clip_uuid[37], blend_clip_uuid[37];
    char state_name[65];
} ForgeSdkAnimationPlaybackV1;
typedef struct ForgeSdkAnimationRootMotionV1 {
    uint32_t size, version, applied;
    uint64_t tick;
    double translation[3];
    float rotation_xyzw[4];
    char scene_uuid[37], entity_uuid[37], joint_uuid[37], clip_uuid[37];
} ForgeSdkAnimationRootMotionV1;
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
typedef struct ForgeSdkOverlapV1 {
    uint32_t size, shape, layer_mask, include_sensors;
    double dimensions[3],
        origin[3];     /* Box full XYZ; sphere radius; capsule/cylinder radius,height. Unused0. */
    float rotation[4]; /* normalized XYZW; identity is0,0,0,1 */
} ForgeSdkOverlapV1;
#define FORGE_SDK_PHYSICS_OVERLAP_LIMIT 256u
typedef struct ForgeSdkCharacterV1 {
    uint32_t size, ground, crouched, shape_change_blocked, jump_accepted;
    double position[3], velocity[3], ground_position[3], ground_normal[3], ground_velocity[3];
    float rotation_xyzw[4];
    char support_scene[37], support_entity[37]; /* empty when unsupported */
} ForgeSdkCharacterV1;
typedef struct ForgeSdkPhysicsBodyStateV1 {
    uint32_t size, motion, active;
    double centre_of_mass[3], linear_velocity[3], angular_velocity[3];
} ForgeSdkPhysicsBodyStateV1;
typedef struct ForgeSdkPhysicsContactV1 {
    uint32_t size, begin;
    uint64_t tick;
    char first_scene_uuid[37], first_entity_uuid[37];
    char second_scene_uuid[37], second_entity_uuid[37];
} ForgeSdkPhysicsContactV1;
/* Distinct XY-domain values. World metres, radians and seconds; never native IDs. */
typedef struct ForgeSdkPhysicsHit2DV1 {
    uint32_t size;
    char scene[37], entity[37];
    double position[2], normal[2], fraction;
} ForgeSdkPhysicsHit2DV1;
typedef struct ForgeSdkPhysicsBodyState2DV1 {
    uint32_t size, motion, active;
    uint64_t epoch; /* Private simulation realization, not a durable identity. */
    double position[2], centre_of_mass[2], linear_velocity[2];
    double angle, angular_velocity, mass, rotational_inertia;
} ForgeSdkPhysicsBodyState2DV1;
typedef struct ForgeSdkPhysicsLoad2DV1 {
    uint32_t size, kind, at_point;
    /* FORCE/IMPULSE use linear; TORQUE/ANGULAR_IMPULSE use angular.
       Unused channels/point must be zero. at_point is0/1, linear loads only.
       Linear N/N*s, angular N*m/N*m*s, optional absolute world XY point. */
    double linear[2], angular, point[2];
} ForgeSdkPhysicsLoad2DV1;
#define FORGE_SDK_PHYSICS_2D_BOX 0u
#define FORGE_SDK_PHYSICS_2D_CIRCLE 1u
#define FORGE_SDK_PHYSICS_2D_CAPSULE 2u
typedef struct ForgeSdkOverlap2DV1 {
    uint32_t size, shape, layer_mask, include_sensors;
    double dimensions[2], origin[2],
        angle; /* Full box XY; circle radius,0; capsule radius,straight height. */
} ForgeSdkOverlap2DV1;
typedef struct ForgeSdkSweep2DV1 {
    uint32_t size, shape, layer_mask, include_sensors, ignore_initial_overlap;
    double dimensions[2], origin[2], displacement[2], angle;
    /* Both empty: no exclusion. Otherwise a complete canonical body reference. */
    char ignore_scene[37], ignore_entity[37];
} ForgeSdkSweep2DV1;
typedef struct ForgeSdkPhysicsContact2DV1 {
    uint32_t size, begin, sensor, first_available, second_available;
    uint64_t tick, first_epoch, second_epoch;
    char first_scene_uuid[37], first_entity_uuid[37];
    char second_scene_uuid[37], second_entity_uuid[37];
} ForgeSdkPhysicsContact2DV1;
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
/* Agent phases:0 disabled,1 idle(no destination),2 pending,3 moving,
   4 paused(zero speed),5 arrived,6 blocked(no advance this tick),7 failed.
   Navigation status is the independent query/admission status above. */
#define FORGE_SDK_NAV_AGENT_DISABLED 0u
#define FORGE_SDK_NAV_AGENT_IDLE 1u
#define FORGE_SDK_NAV_AGENT_PENDING 2u
#define FORGE_SDK_NAV_AGENT_MOVING 3u
#define FORGE_SDK_NAV_AGENT_PAUSED 4u
#define FORGE_SDK_NAV_AGENT_ARRIVED 5u
#define FORGE_SDK_NAV_AGENT_BLOCKED 6u
#define FORGE_SDK_NAV_AGENT_FAILED 7u
typedef struct ForgeSdkNavigationAgentV1 {
    uint32_t size, version, phase, status, sampled, has_path;
    uint64_t tick;
    double position[3], velocity[3], projected_destination[3], remaining_distance;
} ForgeSdkNavigationAgentV1;
/* CPU admission states use the engine resource state names. A retained revision
   can remain usable while a replacement is pending/failed. No GPU readiness claim. */
#define FORGE_SDK_RESOURCE_MESH 1u
#define FORGE_SDK_RESOURCE_MATERIAL 2u
#define FORGE_SDK_RESOURCE_TEXTURE 3u
#define FORGE_SDK_RESOURCE_SHADER 4u
#define FORGE_SDK_RESOURCE_COLLISION 5u
#define FORGE_SDK_RESOURCE_TILE_SET 6u
#define FORGE_SDK_RESOURCE_TILE_MAP 7u
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
typedef struct ForgeSdkTileMapV1 {
    uint32_t size, version, ready, current, edited, layer_count;
    uint64_t generation, revision;
    double cell_size[2];
    char requested_uuid[37], adopted_uuid[37], tile_set_uuid[37];
    char requested_revision[65], adopted_revision[65], diagnostic[1024];
} ForgeSdkTileMapV1;
typedef struct ForgeSdkTileLayerV1 {
    uint32_t size, version, visible;
    int32_t order;
    uint64_t key;
    char name[129];
} ForgeSdkTileLayerV1;
typedef struct ForgeSdkTileLocationV1 {
    uint64_t layer;
    int32_t x, y;
} ForgeSdkTileLocationV1;
typedef struct ForgeSdkTileCellV1 {
    uint64_t tile;            /* Zero means empty, with flags zero. */
    uint32_t flags, reserved; /* Flags0..7; reserved must be zero. */
} ForgeSdkTileCellV1;
typedef struct ForgeSdkTileWriteV1 {
    ForgeSdkTileLocationV1 location;
    ForgeSdkTileCellV1 value;
} ForgeSdkTileWriteV1;
#define FORGE_SDK_TILE_EDIT_PENDING 1u
#define FORGE_SDK_TILE_EDIT_APPLIED 2u
#define FORGE_SDK_TILE_EDIT_FAILED 3u
#define FORGE_SDK_TILE_BATCH_LIMIT 4096u
typedef struct ForgeSdkTileEditV1 {
    uint32_t size, version, state;
    uint64_t generation, revision;
    char diagnostic[1024];
} ForgeSdkTileEditV1;
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
    /* Fixed owner thread; source is (scene AssetId, EntityId).
       operation: 0 stop, 1 restart/play, 2 pause source, 3 resume paused source.
       Returns 1 queued, 0 invalid/unavailable. No device/sample-clock authority. */
    int32_t(FORGE_SDK_CALL* audio_source)(void*, const char* scene_uuid, const char* entity_uuid,
                                          uint32_t operation);
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
       Requests execute only after the calling module callback returns.
       Host operation "time_scale" accepts finite numeric "scale" in [0,4],
       latches session simulation speed, and does not change fixed physics dt. */
    uint32_t(FORGE_SDK_CALL* game_query)(void*, char* output, uint32_t capacity);
    uint64_t(FORGE_SDK_CALL* game_request)(void*, const char* command_json);
    uint32_t(FORGE_SDK_CALL* game_inspect)(void*, uint64_t token, char* output, uint32_t capacity);
    int32_t(FORGE_SDK_CALL* game_release)(void*, uint64_t token);
    int32_t(FORGE_SDK_CALL* game_save_schema)(void*, const ForgeSdkSaveSchemaV1*);
    /* Explicit opt-in to one bounded string argument; module startup only.
       Pause/Resume/Step/TimeScale are reserved host UI actions. TimeScale takes
       a numeric string; the other three actions remain argument-free. */
    int32_t(FORGE_SDK_CALL* ui_allow_value_action)(void*, const char* command);
    /* Publish a copied protocol-supported value in initialization, fixed tick
       or control frame; no native presentation pointers. */
    int32_t(FORGE_SDK_CALL* ui_publish_json)(void*, const char* entity_uuid, const char* name,
                                             const char* value_json);
    /* Copied {entity,command,value}; two-pass sizing retains the event until a
       sufficient output buffer is supplied. 0 means no event/rejection. */
    uint32_t(FORGE_SDK_CALL* ui_poll_event)(void*, const char* command, char* output,
                                            uint32_t capacity);
    /* Current fixed tick broadcast; read in post_physics_phase after animation.
       1 copied event, 0 end/unavailable, -1 invalid argument or removed target.
       Indexes remain stable for this tick. No consumption or native ownership. */
    int32_t(FORGE_SDK_CALL* animation_read_event)(void*, uint32_t index, ForgeSdkAnimationEventV1*);
    /* Realized clip or AnimationSequence controller. Controllers have an empty
       clip_uuid; bound tracks are observable but only their controller is controllable.
       Fixed-only; Gameplay sees the prior realization, PostPhysics sees this tick.
       1 copied status,0 unavailable,-1 bad args. */
    int32_t(FORGE_SDK_CALL* animation_playback)(void*, const char* scene, const char* entity,
                                                ForgeSdkAnimationPlaybackV1*);
    /* Registered matching-world entity only. Intrinsic RuntimeEntities read.
       1 canonical UUID pair,0 unavailable/unregistered/stale/ambiguous,-1 bad args/context. */
    int32_t(FORGE_SDK_CALL* entity_reference)(void*, uint64_t native_entity, ForgeSdkEntityRefV1*);
    /* Current admitted player; Gameplay before Animation only. No pose writes here.
       1 applied,0 unavailable/late phase/completed Play,-1 bad args/context.
       Ordinary pending component selection takes precedence at synchronization. */
    int32_t(FORGE_SDK_CALL* animation_control)(void*, const char* scene, const char* entity,
                                               uint32_t operation);
    /* Fixed owner thread. One-step world load on enabled Dynamic Body.
       kind: FORCE(N), IMPULSE(N*s), TORQUE(N*m), ANGULAR_IMPULSE(N*m*s).
       vector components finite, +/-1e6. Optional absolute world point for Force/
       Impulse only; NULL means center of mass. 1 queued,0 invalid/unavailable. */
    int32_t(FORGE_SDK_CALL* physics_apply)(void*, const char* scene, const char* entity,
                                           uint32_t kind, const double vector[3],
                                           const double* world_point);
    /* Fixed owner-thread read; realized state only, no wake/queue flush.
       COM/linear velocity: world m / m/s. Angular velocity: world radians/s.
       Motion0 Static,1 Kinematic,2 Dynamic. Inactive Static is not sleeping.
       1 available,0 unavailable/unrealized,-1 invalid/context; exact output size. */
    int32_t(FORGE_SDK_CALL* physics_body_state)(void*, const char* scene, const char* entity,
                                                ForgeSdkPhysicsBodyStateV1*);
    /* Absolute world point, finite +/-1e9m; copied point velocity in world m/s.
       1 available,0 unavailable/unrealized,-1 invalid/context. No wake/queue flush. */
    int32_t(FORGE_SDK_CALL* physics_point_velocity)(void*, const char* scene, const char* entity,
                                                    const double world_point[3],
                                                    double out_velocity[3]);
    /* Fixed owner-thread Physics command; enabled Dynamic body only. Channel
       LINEAR(m/s) or ANGULAR(rad/s); world vector finite +/-1e6. Zero stops that
       channel, preserving the other. Native magnitude caps/activation apply.
       Queued in body-command order;1 accepted,0 invalid/context/unavailable. */
    int32_t(FORGE_SDK_CALL* physics_set_velocity)(void*, const char* scene, const char* entity,
                                                  uint32_t channel, const double velocity[3]);
    /* Current fixed-tick adopted contact-cache broadcast, intended for PostPhysics.
       Count0..16384, -1 bad owner/fixed/Physics context. No prior-tick replay.
       Stable slots, nonconsuming reads; sleeping/removal can cause cache End,
       multiple subshape contacts can produce multiple Begin/End events. */
    int32_t(FORGE_SDK_CALL* physics_contact_count)(void*);
    /* Exact size required; correct-size output clears on failure.1 copied event,
       0 unavailable/index/retired target, -1 bad context/output. UUIDs only. */
    int32_t(FORGE_SDK_CALL* physics_contact_event)(void*, uint32_t index,
                                                   ForgeSdkPhysicsContactV1*);
    /* Fixed/owner/Physics. All output slots need exact size, capacity1..256.
       Owning unique sorted UUID references;0..capacity complete results.
       -2 insufficient caller capacity, -1 invalid/context/native budget overflow.
       Valid-capacity/size arrays clear failures; invalid-capacity/size arrays are not written.
       Native surfaces/tolerances, not enclosed-mesh containment or cache events. */
    int32_t(FORGE_SDK_CALL* physics_overlap_shape)(void*, const ForgeSdkOverlapV1*,
                                                   ForgeSdkEntityRefV1*, uint32_t capacity);
    /* Current fixed tick only. Model-holder local pre-scale rigid delta, meters
       and x/y/z/w unit quaternion. Reads do not consume it. Read in PostAnimation
       before PrePhysics to queue solver-owned movement. 1 available,0 unavailable,
       -1 invalid/context; unavailable outputs are cleared for valid size. */
    int32_t(FORGE_SDK_CALL* animation_root_motion)(void*, const char* scene, const char* entity,
                                                   ForgeSdkAnimationRootMotionV1*);
    uint64_t post_animation_phase;
    /* Before Animation, seconds in [0, incoming duration]; loop end maps to zero.
       Retains pause/fade; skipped time emits no markers or root displacement. */
    int32_t(FORGE_SDK_CALL* animation_seek)(void*, const char* scene, const char* entity,
                                            double seconds);
    /* Fixed owner thread/Navigation permission. Copied latest completed navigation
       tick; Gameplay precedes movement, PostPhysics follows it. Config changes
       report Pending. sampled/has_path gate metrics; no asset IO or world mutation.
       Exact size;1 observation,0 absent/unavailable,-1 bad arguments/context.
       Valid-size outputs clear on failure. Net world velocity m/s; remaining
       waypoint-route distance meters, goal is the projected navmesh endpoint. */
    int32_t(FORGE_SDK_CALL* navigation_agent_state)(void*, const char* scene_uuid,
                                                    const char* entity_uuid,
                                                    ForgeSdkNavigationAgentV1*);
    /* Queue an admitted Prefab instance at the next fixed boundary. Uses the
       entity request ownership/limits/inspection/release contract above. The
       returned observation identifies its root. Source transforms/spatial policy
       are retained; C++ may set native values after Ready. Runtime-only Prefabs
       require a typed Runtime Dependency reachable from the loaded Scene. */
    uint64_t(FORGE_SDK_CALL* prefab_request)(void*, const char* prefab_uuid, const char* scene_uuid,
                                             const char* name);
    /* Copied owner-thread runtime queries; paused reads allowed. Exact output
       size, version1. 1 success,0 unavailable/not ready,-1 invalid,-2 stale
       generation. Valid-size outputs clear on failure. Status1 can be not ready.
       No query initializes an instance, performs IO or exposes a data pointer. */
    int32_t(FORGE_SDK_CALL* tile_map_inspect)(void*, const char* scene, const char* entity,
                                              ForgeSdkTileMapV1*);
    /* Index only enumerates up to8 layers; key identifies a layer within this
       adopted map. Generation must match an inspected ready instance. */
    int32_t(FORGE_SDK_CALL* tile_map_layer)(void*, const char* scene, const char* entity,
                                            uint64_t generation, uint32_t index,
                                            ForgeSdkTileLayerV1*);
    /* Count1..4096. All locations validated before copying any cell. Valid
       output/count clears on failure; invalid pointer/count is not written.
       Empty cells are successful values, distinct from unavailable maps. */
    int32_t(FORGE_SDK_CALL* tile_map_cells)(void*, const char* scene, const char* entity,
                                            uint64_t generation, const ForgeSdkTileLocationV1*,
                                            ForgeSdkTileCellV1*, uint32_t count);
    /* Fixed-only, copied atomic batch, next fixed boundary; ordered last write
       wins. Returns owned observation token or0. Same generation is rechecked
       at publication. 64 observations/module,256/world,4MiB pending writes.
       Runtime edits never change authored files. Reset explicitly adopts latest
       assets and discards edits. Stop cancels all pending owned observations. */
    uint64_t(FORGE_SDK_CALL* tile_map_write)(void*, const char* scene, const char* entity,
                                             uint64_t generation, const ForgeSdkTileWriteV1*,
                                             uint32_t count);
    uint64_t(FORGE_SDK_CALL* tile_map_reset)(void*, const char* scene, const char* entity,
                                             uint64_t generation);
    int32_t(FORGE_SDK_CALL* tile_edit_inspect)(void*, uint64_t token, ForgeSdkTileEditV1*);
    /* Release pending cancels; release applied only forgets its observation. */
    int32_t(FORGE_SDK_CALL* tile_edit_release)(void*, uint64_t token);
    /* XY physics: same owner/fixed/Physics permission and4096-command FIFO.
       motion0 teleport,1 kinematic target; clear_velocity0/1, teleport only.
       Position +/-1e6m, angle +/-1e6rad.1 queued,0 invalid/context/unavailable. */
    int32_t(FORGE_SDK_CALL* physics_move_2d)(void*, const char* scene, const char* entity,
                                             const double position[2], double angle,
                                             uint32_t motion, uint32_t clear_velocity);
    int32_t(FORGE_SDK_CALL* physics_apply_2d)(void*, const char* scene, const char* entity,
                                              const ForgeSdkPhysicsLoad2DV1*);
    int32_t(FORGE_SDK_CALL* physics_set_linear_velocity_2d)(void*, const char* scene,
                                                            const char* entity,
                                                            const double velocity[2]);
    int32_t(FORGE_SDK_CALL* physics_set_angular_velocity_2d)(void*, const char* scene,
                                                             const char* entity, double velocity);
    /* Reads never flush commands or wake bodies. Exact output size; valid-size
       outputs clear on failure.1 available,0 unavailable,-1 invalid/context. */
    int32_t(FORGE_SDK_CALL* physics_body_state_2d)(void*, const char* scene, const char* entity,
                                                   ForgeSdkPhysicsBodyState2DV1*);
    int32_t(FORGE_SDK_CALL* physics_point_velocity_2d)(void*, const char* scene, const char* entity,
                                                       const double point[2],
                                                       double out_velocity[2]);
    int32_t(FORGE_SDK_CALL* physics_raycast_2d)(void*, const double origin[2],
                                                const double displacement[2], uint32_t mask,
                                                uint32_t include_sensors, ForgeSdkPhysicsHit2DV1*);
    /* Same complete-results/capacity1..256/-2 insufficient/-1 invalid contract
       as physics_overlap_shape. Sorted canonical copied references. */
    int32_t(FORGE_SDK_CALL* physics_overlap_shape_2d)(void*, const ForgeSdkOverlap2DV1*,
                                                      ForgeSdkEntityRefV1*, uint32_t capacity);
    /* Current fixed-tick broadcast; sensor first/visitor second, solids in
       canonical order. Retired Ends retain UUID/epoch with availability false.
       Count0..16384,-1 invalid; event1 copied,0 absent/stale,-1 invalid. */
    int32_t(FORGE_SDK_CALL* physics_contact_count_2d)(void*);
    int32_t(FORGE_SDK_CALL* physics_contact_event_2d)(void*, uint32_t index,
                                                      ForgeSdkPhysicsContact2DV1*);
    /* Linear box/circle/capsule sweep. Geometry matches overlap_shape_2d; no
       rotation sweep. Endpoint +/-1e6m; zero displacement is valid. Optional
       exclusion skips every shape of that canonical body. Initial overlaps
       report fraction0/zero normal; ignore_initial_overlap0/1 skips all native
       fraction0 hits (also touching).1 hit,0 miss,-1 invalid/context. Reads only
       realized state; no command flush, wake or depenetration. Valid-size output
       clears on failure. Request/output may alias; request copied first. */
    int32_t(FORGE_SDK_CALL* physics_shape_cast_2d)(void*, const ForgeSdkSweep2DV1*,
                                                   ForgeSdkPhysicsHit2DV1*);
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
