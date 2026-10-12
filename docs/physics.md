# Physics module and transient recovery


`forge.physics` separates reflected engine-owned authoring values from Jolt-owned simulation objects. `physics_components.hpp` contains engine-owned typed settings; `physics_service.hpp` contains owner-thread queries/commands. `physics.hpp` is host composition/internal runtime infrastructure, excluded from the experimental SDK distribution.

## Effective spatial ancestry

Before body realization, each fixed-tick synchronization, checkpoint capture, and recovery reconstruction, Static/Kinematic bodies are checked against the existing Phase 3 effective spatial-parent graph. Traversal includes nonphysics intermediaries and Explicit links and stops at World binding. Reaching a Dynamic PhysicsBody raises `physics.unsupported_dynamic_ancestry`. The structured diagnostic carries the affected EntityId/scene AssetId, `related_entity` as the Dynamic ancestor's EntityRef, module, property, tick and an explanation. Runtime candidate failures retain this diagnostic after candidate destruction and leave the previous runtime unchanged while discarding the failed candidate.

Validation covers body/configuration changes, spatial reparenting and prefab realization/reconciliation when runtime synchronization consumes those changes, including scene replacement and recovery. Authoring worlds still have no solver; authoring an unsupported arrangement does not silently repair it. Physics realization rejects it. Structural ChildOf with spatial World is allowed; nonphysics visual descendants may follow Dynamics; supported non-Dynamic ancestry remains allowed. Separate joint entities provide physical connections without changing the spatial graph. Compound collider assets already describe one rigid body; neither arrangement makes separate Static/Kinematic bodies valid descendants of Dynamics.

Movement commands are prepared in FIFO order in detached transform values. Parent commands therefore affect subsequent child conversion, and final targets for all bodies are evaluated after the batch. Validation of ancestry, shear, shape scale and translation/rotation-only scale preservation precedes local-channel/Jolt mutation. Only changed local translation/rotation channels are written; LocalScale is never overridden by a pose target. Invalid batches preserve values and prefab channel ownership. Teleports are applied before final kinematic velocities are calculated. Derived WorldTransform remains owned exclusively by the transform system. These changes do not alter scene, identity, ABI1 or private recovery-envelope versions.

## Dependency and lifetime

Jolt **5.6.0**, commit **e77f175595e64cb44218cc9d9d56fc365ad0e36a**, MIT. Official [release](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0), [build configuration](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Build/CMakeLists.txt), [initialization example](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/HelloWorld/HelloWorld.cpp).

CMake selects static Jolt, double world positions, SSE2 baseline, RTTI for compatible UBSan boundary checks, matched MSVC CRT, upstream CPU rigid-body jobs, and no compute backend, standalone upstream applications, debug renderer, Jolt profiler, LTO, or compiler flag override. Jolt headers/definitions are private to `forge_physics`. Linux baseline is GCC12+, Windows VS2022. Other dependency pins are unchanged.

Authoring/Validation/Preview worlds receive schemas only. The runtime explicitly composes `physics_module()`, whose dependencies are `forge.core` and `forge.transforms`. One reference-counted registration lease owns upstream allocator/type registration and Factory. Each simulated world owns filters, listener, temp allocator, upstream worker pool, PhysicsSystem, shapes and body mapping. `Update` joins workers before returning; no worker accesses Flecs or editor state. Consumers stop before physics; stopped service references reject use. Module state and code leases survive Flecs finalization.

The service slot is per world, while diagnostics/profiling retain existing engine ownership. A required Physics capability needs an explicit dependency on its provider. Register schemas first, then start runtime providers/consumers in dependency order. Capability availability is refreshed for native runtime startup.

## Authored model

`PhysicsBody`: `motion` 0 Static / 1 Kinematic / 2 Dynamic; density kg/m³; mass kg (zero means density); friction; restitution; gravity multiplier. `BoxCollider` has full XYZ dimensions; `SphereCollider` has radius; `CapsuleCollider` has radius and straight cylinder height, Y axis. All use meters before effective world scale. CylinderCollider adds a radius and full height. Each enabled body requires exactly one inline collider or AssetCollider. AssetCollider references a separately authored CollisionAsset, including local shape poses and compounds; native subshape indices are not asset identities.

Optional reflected components use existing scene-v3/v4 and prefab-v1 containers. No scene version bump, extra resource manager, persisted solver handles, or separate prefab mechanism. Components retain Flecs inheritance, independent channel ownership and property-level explicit intent. `component.add` creates optional default values; `property.set`, `property.revert`, and `component.revert` share existing transaction/history behavior.

## Fixed tick and transform ownership

Input → Gameplay → PrePhysics (transform evaluation / realization / targets) → Physics (one Jolt Update with fixed dt) → PhysicsAdoption → PostPhysics consumers → final Transforms → existing presentation capture.

Static placement comes from evaluated world transforms. Kinematic targets use upstream `MoveKinematic` with fixed dt, not per-render-frame teleportation. Dynamic bodies require spatial World and write only local simulation translation/rotation after Update. WorldTransform remains exclusively derived by the transform module. Scale is folded into shape geometry. Physics rejects shear and zero or Jolt-invalid world scale; sphere/capsule world scale magnitudes must be uniform. Signed box/sphere/capsule scales describe symmetric solids: magnitudes are baked into dimensions without changing authored scale signs. Scaled collider dimensions must remain .001..10000 meters to bound mass/inertia calculations. Physics requires unambiguous scene/entity references within its world; repeated simultaneous instances of the same scene asset need a future runtime instance scope. Coordinates are bounded to ±1 billion meters with double Jolt positions; this is an input bound, not a large-world quality claim.

Config changes are compared against realized settings at a safe boundary. Compatible motion-type rebuilds preserve linear/angular velocity and activation. Motion-type changes initialize new state. Unchanged bodies are not recreated or reactivated. Invalid realization is diagnosed; no approximation discards shear. Dynamic direct transform writes are rejected; gameplay must use explicit teleport. Teleport writes supported local channels and resets existing presentation history after the tick.

## Body motion tuning

PhysicsBody's trailing `linear_damping` and `angular_damping` default to .05 per
second. `max_linear_velocity` defaults to 500 m/s; `max_angular_velocity` defaults to 2700
**degrees/s**, converted once at native creation. This yields the exact pinned
native float 47.1238899230957 rad/s. `allow_sleeping` defaults to true. Missing authored
fields use these defaults without rewriting old Scene/Prefab sources.

Damping is independent of contact friction: at each Dynamic integration Jolt uses
`v *= max(0,1-c*dt)` and the corresponding angular equation. Damping is admitted
from 0..10000/s; both limits from 0..1000000 in their authored units. Limits clamp
the vector magnitude, not each coordinate. Zero allows no velocity on that
channel. Native impulses and same-motion replacement velocity adoption clamp too;
contact solving can subsequently exceed the integration cap temporarily.
Kinematic movement still follows its explicit native target, not Dynamic drag or
an invented force. Inactive-mode choices remain authored for later use.

Configuration changes use the existing replacement/preflight boundary. They
preserve pose and compatible velocity, clamped to lowered limits. Sleeping state
is retained only when permitted; turning permission off wakes a previously
sleeping Dynamic body and keeps it active without adding motion. Unchanged bodies
are not rebuilt or reactivated. Native recovery mapping includes all five values;
exact SDK fingerprint/layout changes require module rebuilds, while ABI1 and
durable formats remain unchanged. Private recovery is version4 and exact-build
compatible. The existing generic Inspector, history and prefab intent own edits.

Original compiler-free Gallery 23 compares gravity-driven free fall, damping,
linear caps and sleep policy. Numerical integration, camera/render-resource bounds
and relocated source-free content are separate proof from native Windows images
and physical desktop acceptance.

## Queries, commands and contacts

Raycast returns closest hit EntityRef, point, normal and fraction; displacement defines ray length. Results are owning FORGE values and never expose BodyID. Teleport/kinematic commands are bounded to 4096 pending requests and processed at the next pre-physics boundary. PostPhysics commands therefore take effect next tick. Direct physics-component writes belong in Gameplay before synchronization; capture rejects unsynchronized component or world-pose changes after adoption, including movement inherited from nonphysics ancestors. Commands preserve world-space target semantics for supported parent transforms.

Static and moving are internal filtering classes. Static/static pairs are excluded; moving bodies interact with static/moving. These Jolt layer numbers are not serialized. Authored layers use stable named project slots and symmetric masks through native CollisionGroup filtering, described below.

Contact callbacks append bounded token records under a mutex. After Update, the owner thread resolves current tokens to EntityRefs, drops retired-body tokens and exposes `PhysicsContact` values. The existing list expires at the next successfully admitted synchronization or is replaced by adoption; failed preflight preserves it. These are **solver contact-cache Begin/End**, not a promise of geometric overlap Begin/End: Jolt can remove contacts when bodies sleep, and complex shapes can yield multiple subshape events. Events are transient; no generic bus, overlap tracker or persisted contact entities.

`contact_event(index)` returns a checked owning copy from that same list. Both
targets must still resolve to the current realized body/character generation.
Repeated reads do not consume slots or wake bodies. Retirement can make a slot
unavailable; do not compact it while iterating. Expiration at an admitted
synchronization prevents an old pair from being associated with newly realized
bodies that reuse the same UUIDs.

The exact SDK tail `physics_contact_count` and `physics_contact_event` requires
owner thread, a fixed-step input context and Physics capability. Count is bounded
to16384 stable slots; -1 means invalid context. A current tick with no observations
returns0. Old-tick observations are filtered, including before physics on the next
tick. Getter requires exact `ForgeSdkPhysicsContactV1` size, returns1 for a copied
current event,0 for unavailable/index/retired, and-1 for invalid context/output.
Correct-size output clears on failure; wrong-size output is not written. The DTO
contains only Begin, tick and the canonical scene/entity UUID pair. It carries no
native body handles, contact point, normal or impulse. An otherwise valid fixed
consumer without Physics capability is denied.

Fresh paused recovery restores native contact-cache state without replaying the
transient adopted list. C++ game-owned counters/reaction state can use existing
reflected components; PostPhysics commands take effect at the next boundary.
Private checkpoint4 and authored formats remain unchanged. The public header
fingerprint changes, requiring a matching exact SDK/module rebuild; ABI1 remains.

## Instantaneous volume queries

`PhysicsService::overlap_shape` queries current realized geometry using centered
Box, Sphere, Capsule or Cylinder, with the same dimensions and orientation as
linear shape casts. It returns copied canonical `EntityRef` values, sorted and
unique per object even when several compound children or mesh triangles hit.
Static and sleeping bodies are eligible; reads do not wake bodies or create
contact events. Layer masks and sensor inclusion use the existing query filter,
independently of body pair masks. Retired or replaced Flecs generations are omitted.

Native narrow-phase tolerances apply near boundaries. Mesh targets test their
surfaces; a query wholly inside a closed mesh is not promised to hit. Queries are
limited to256 unique native bodies and16384 subshape callbacks. Exceeding either
budget rejects the query, without partial results. Resolution uses existing
body/character owner maps after synchronous native callbacks, with no persistent
overlap owner or hidden result lifetime.

The matching SDK tail `physics_overlap_shape` requires Physics capability, owner
thread and fixed scope. `ForgeSdkOverlapV1` and every output reference need exact
sizes. Caller capacity is1..256. Success returns the complete count; -2 means
insufficient caller capacity and -1 means invalid input/context or native budget.
Valid output arrays clear on failure and unused slots clear on success. Invalid
capacity or any wrong-size output slot rejects without writing that array. The
request is copied before outputs are cleared. No pointers or native handles are
returned. C++ consumers must rebuild against the matching SDK fingerprint; ABI1,
private checkpoint4 and authored formats remain unchanged.

The editable `PhysicsVolumes` C++ example counts two static objects and a moving
sensor in a sphere. Reflected game-owned observation/launch progress uses normal
component recovery; the query itself adds no checkpoint state.

## Private recovery

Runtime protocol 2 adds an optional private recovery payload beside the existing scene response. The former movement-only ABI1 header has been retired. Controllers retain separate latest and pending-activation checkpoints; probes receive the corresponding checkpoint without advancing the live runtime.

At a completed fixed boundary, the outer envelope captures scene plus available prefab source revisions, simulation frequency, source session and tick, and physics envelope. Physics envelope version4 preserves authored joint identities/settings in actual native constraint order, build/source/runtime fingerprint, exact Jolt configuration, gravity, tick, EntityRef-to-explicit-BodyID/config mapping, pending tagged FORGE body loads/movement/velocity, and Jolt StateRecorder bytes. It deliberately supersedes version3 to admit the two velocity command tags; version3 added loads and version2 added joint ordering. A content checksum detects accidental alteration; this trusted local native-process transport is not an authentication/security boundary.

Restoration constructs an unpublished candidate world, validates metadata and expected source session/tick/content, restores scene/configuration, validates unique body indices/sequences and EntityRefs/configs, constructs bodies with **CreateBodyWithID**, reconstructs constraints in captured native index order, then calls Jolt **RestoreState**. Only successful restoration swaps the candidate into use. BodyID is a transient reconstruction token. Jolt owns the solver encoding. See upstream [PhysicsSystem](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp), [BodyManager](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyManager.cpp), and [StateRecorderImpl](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/StateRecorderImpl.h).

Paused restoration performs no Update. Clock tick is restored; elapsed/accumulator debt is reset; input is released; previous/current presentation samples coincide. Step advances one tick. Controllers resume only when prior policy calls for it. A failure leaves the prior published runtime intact where it exists, or reports that recovery failed and clean Play is needed.

Limits: 8192 bodies, 16384 contact events, 2 MiB raw solver bytes, 6 MiB physics envelope, 8 MiB total checkpoint, existing 16 MiB transport. Exceeding limits is a diagnostic, not silent truncation. Checkpoints never enter scene/prefab/project files. No save/load-game UI, cross-build/platform compatibility, deterministic replay, or arbitrary native global/resource recovery is promised.

## Experimental SDK

### Copied body motion

`PhysicsService::body_state(EntityRef)` returns an optional copied snapshot of the
realized native body: motion kind, activation, absolute world center of mass,
linear velocity at that center in m/s, and world angular velocity in radians/s.
The authored maximum angular speed remains in degrees/s. Static bodies report
zero velocity and inactive; distinguish their motion kind from sleeping Dynamic
bodies. Kinematic values are the native target velocity, rather than Dynamic drag.

`point_velocity(EntityRef, LocalTranslation)` copies world velocity at an absolute
world point, using `linear + angular cross (point - COM)`. It uses actual native COM,
including collision-asset offsets and signed scale. Points must be finite within
the existing +/-1e9-meter bound. Reads neither wake bodies nor flush queued loads
or pending configuration. Until synchronization they describe the currently
realized body. Unrealized, unavailable or retired identities have no value;
disabled bodies become unavailable when that configuration is adopted.

The exact SDK exposes `physics_body_state` and `physics_point_velocity` on the
owner thread during a fixed tick with Physics capability. Their results are
1 available, 0 unavailable/unrealized, or -1 invalid context/arguments. Initialize
`ForgeSdkPhysicsBodyStateV1::size` to its exact size. A correctly sized output is
cleared before failures; an invalid size or null output is rejected without
writing. Point output is cleared on failure and may alias the input point array.
No native pointers, body handles, authored fields or new recovery state are added.
Changed exact SDK headers require rebuilding project modules; ABI1 and durable
document formats remain unchanged.

### Setting body velocity

`set_linear_velocity(EntityRef, Double3)` queues world center-of-mass velocity in
m/s; `set_angular_velocity(EntityRef, Double3)` queues world angular velocity in
radians/s. Enabled Dynamic bodies, including sensors, use the existing bounded
4096-command FIFO and positive pre-physics boundary. Finite vector coordinates
must be within +/-1e6. Identity, body settings, shape and role are preflighted
before any mutation. Kinematic movement retains its target owner.

Each setter preserves the opposite channel. Zero executes and stops that channel;
zero force/impulse remains a no-op. Native magnitude caps apply when setting the
velocity, followed by ordinary damping and other solver effects during the step.
The selected Jolt setter activates an inactive body for a non-near-zero request,
even if its speed cap reduces the result to zero. A zero request does not wake a
sleeping body. FIFO order with impulses and clearing teleports is observable:
setting 3 m/s then adding a 4 N*s impulse to a 2 kg body gives 5 m/s before damping;
reversing those commands gives 3 m/s. A later clearing teleport clears velocity,
while accumulated one-step force remains governed by the existing load contract.
Forces and torques integrate after the queued commands; setters do not erase their
accumulators. Setting 3 m/s and applying 4 N to a 2 kg body gives
`3 + 2 * dt` m/s before damping in either submission order.

The exact SDK tail `physics_set_velocity` accepts a copied three-double vector and
channel `FORGE_SDK_PHYSICS_LINEAR_VELOCITY` or
`FORGE_SDK_PHYSICS_ANGULAR_VELOCITY`. It returns1queued or0rejected and requires the
fixed owner thread and Physics capability. PostPhysics queues for the following
step. Existing load-kind meanings are unchanged. Private checkpoint4 adds strict
tags6/7; old ephemeral envelopes reject. ABI1 and durable authoring formats stay
unchanged; rebuild project modules against the matching SDK.

The ordinary `Examples/PhysicsVelocity` C++ project demonstrates launch/coast,
timed zero stops of each channel and both, and reflected one-shot recovery state.
Its combined-package gate compiles the project with the installed SDK, measures
independent motion and true fresh-process recovery, then exports and removes the
editable source before checking production startup and actual graphical motion.

### Applying forces and impulses

`PhysicsService` exposes `add_force`, `add_impulse`, `add_torque` and
`add_angular_impulse`. They queue copied world-space vectors in the existing FIFO
beside teleport/kinematic commands. Units are N, N*s, N*m and N*m*s, respectively.
Force and torque act for one positive fixed step; repeat them for sustained loads.
An impulse acts once. Do not multiply an impulse by frame time or multiply a force
by dt before submitting it. The native solver applies mass and current inertia.
Optional force/impulse application points are absolute world positions; omission
means the body's native center of mass. An off-center point adds native torque.
Body transform origin and COM can differ for collision assets and signed scale.

Commands require available, enabled Dynamic bodies. Dynamic sensors accept loads.
Finite vector components are bounded to +/-1e6 in the corresponding units; world
points use the existing +/-1e9-meter position bound. Invalid commands never append.
Zero vectors are validated no-ops and do not wake sleepers. Nonzero loads wake
through the native API. All body commands share the4096 bound. Complete target,
role, pose and shape admission precedes native writes. Pending loads reject
zero-dt synchronization instead of being consumed while paused. Changing/removing
a target before consumption rejects the candidate without losing valid queued
intent. New/replaced bodies begin at the pre-command pose, so a point impulse
before a later teleport uses the correct earlier COM. FIFO ordering includes
impulse then clearing teleport versus clearing teleport then impulse. Teleport's
clear flag clears velocity, not an already accumulated one-step force/torque.

The exact SDK's tail `physics_apply` callback takes kind0Force/1Impulse/2Torque/
3AngularImpulse, a three-double vector and optional three-double world point for
Force/Impulse. It returns1queued or0invalid/unavailable. Existing owner-thread,
fixed-tick, identity and Physics capability checks apply; no Jolt handle crosses
the boundary. PostPhysics submission queues for the following fixed step.
The public header/fingerprint changes require rebuilding exact SDK modules;
native ABI1 and authored formats remain unchanged. The separate ordinary
`Examples/PhysicsLoads` project uses a real compiled C++ System and reflected
progress so one-shot intent survives supported runtime recovery.

The exact fingerprint covers the callback table plus FORGE physics component definitions. The existing callback table has size-checked extensions for Physics capability, PostPhysics phase, raycast and movement. Modules still use one shared Flecs implementation, matched compiler/config/CRT, and startup-only code leases. No Jolt headers or static implementation enter a gameplay module. The former movement-only ABI1 gameplay contract was later retired.

## Deferred

Heightfield collision, vehicles, soft bodies, cloth, advanced material assets and
additional query families remain future work. Durable game saves use explicit
game-owned data; private solver checkpoints are not a save-game format. Compound,
triangle-mesh, character, named-layer, collision-preview, joint and continuous
collision support are described below.

### Signed visual scale versus collider admission

Verified against Jolt5.6.0/e77f175595e64cb44218cc9d9d56fc365ad0e36a:
`Shape::IsValidScale` rejects axes with magnitude below1e-6; `BoxShape` uses
absolute dimensions; `SphereShape` and `CapsuleShape` also require uniform absolute
scale. FORGE calls the actual shape-specific validator and retains its stricter
1e-5 absolute uniform-magnitude difference and .001–10000meter dimension bounds.
No `MakeScaleValid` or hidden hierarchy/local-scale write is used. All eight sign
combinations represent the same centered symmetric solid with the selected proper
rotation. Mesh/compound/asymmetric colliders are not covered by this conversion.
Zero remains valid visual data but invalid physics configuration; failed candidate
synchronization leaves previously realized bodies intact. Recovery uses the same
admission. Existing World-bound Dynamic policy and effective-spatial Dynamic
ancestry restrictions remain unchanged.

## Animated model transforms

The runtime composition validates complete model animation poses before animation
writes ECS channels. It reuses PhysicsRuntime's ordinary configuration and effective
spatial-parent checks. The same Dynamic ownership predicate is shared with normal
physics synchronization; animation is not translated into an implicit teleport.
Before first body realization, animation also cannot change a Dynamic starting pose.
Kinematic model nodes can receive representable animated transforms. Unsupported
sphere/capsule world scale or any other existing configuration failure rejects the
animation pose before mutation. This is host admission, not a new physics service,
constraint system, matrix authority or scene-format revision. Animation recovery
uses this boundary too.

## Collision assets

The independent `CollisionAsset` tag, bounded CPU collision envelope, shared
import/publication, asynchronous native preparation, AssetCollider realization and
runtime package adapter share the existing asset, scene and physics owners.
Build260923-000066 predates this Collision/Character implementation.

The version1 envelope uses the common cooked-envelope reader with at most1MiB
metadata and64MiB total file data. Its shape tree has stable typed member UUIDs,
local TRS, at most1024nodes/depth32 and aggregate1,048,576vertices/triangles.
Convex inputs have a65,536point preparation budget; Jolt's256point limit applies
to the resulting hull, not the input cloud. These are initial bounded processing
profiles, not measured maximum production scene sizes. Geometry/translation and
composed shape bounds stay within±1million meters; primitive dimensions retain
the existing1mm–10km profile. These collision limits do not change visual scale.

Supported prepared families are Box, Sphere, Capsule, Cylinder, ConvexHull,
TriangleMesh and immutable StaticCompound. A compound can contain other compounds
and has stable child identity independent of array order. A mesh anywhere in the
tree marks the prepared result static-only under FORGE's initial policy. Native
Jolt supports some wider moving-mesh cases; this is an engine admission policy.

Exact Jolt5.6.0/e77f175595e64cb44218cc9d9d56fc365ad0e36a source governs shape
creation and scale. Sphere/capsule require uniform magnitudes; cylinder requires
uniform XZ magnitudes. Box/hull/mesh support signed nonuniform scale; compounds
must additionally satisfy child rotation/scale representability. Native zero-scale
and near-zero checks apply. No MakeScaleValid approximation silently changes
authored content. Shape origin and center of mass remain distinct.

Mesh generation explicitly chooses admitted LOD/parts and hull or triangle mode.
Only referenced base POSITION vertices participate; skin/morph deformation is not
baked implicitly. Degenerate triangles reject by default, or are removed under an
explicit policy with a count; removal that leaves empty geometry still rejects.
Jolt retains its own hull creation, duplicate-triangle sanitation and result errors.

FORGE validates geometry before native construction; it does not treat native
Shape::RestoreBinaryState as an untrusted-file validator. Shared registration
leases outlive immutable shapes, including preparation results produced off-thread.
`AssetCollider` holds a typed Collision AssetId and is mutually exclusive with the
inline collider shapes. Scene preparation waits for required collision resources;
failed preparation preserves the active world. Native shapes are shared within the
physics resource owner. Paused polling queues CPU work without adopting a new
shape; replacements are adopted at physics synchronization boundaries. Checkpoint
configuration records the realized collision AssetId/revision. Native shape bytes
are never a durable saved-game format.

Source Mesh edges are Build dependencies. Cooked collision bundles retain their
provenance but contain all required geometry, so runtime packages omit those render
sources unless another runtime dependency needs them. Explicit part ordinals bind
to a reviewed Mesh revision and reject stale revisions; whole-Mesh selection follows
new revisions. Collision source copies receive new AssetIds while asset-local child
IDs can remain stable inside the independent copied asset.

PhysicsBody adds enabled/sensor/layer/mask fields. Older component data defaults to
enabled, solid, layer zero and all masks, preserving its previous behavior. Project
settings hold 32 stable named slots; names may change without remapping components.
Both bodies' masks must admit the other's layer. Exact Jolt CollisionGroup supplies
32-bit values to a stateless GroupFilter; native ObjectLayer stays the existing
Static/Moving broadphase partition. No Jolt ABI width change or callback into Flecs.
Sensors use Jolt native overlap semantics; static/static pairs remain excluded.
Filtered rays independently choose queried layers and whether sensors count.

The Collision document uses the same UI-independent source/history owner as Material,
with format-specific validation. Source Save and cooked publication remain separate:
failed cooking preserves the saved source for correction and the prior usable artifact.
Collision debug drawing and character/SDK integration are implemented below. Final
Windows execution and visual acceptance are tracked separately from source support.

### Character controller integration

`CharacterController` contains authored mechanics only: enabled, capsule/cylinder,
radius, standing/crouched straight height, mass, maximum push force, walkable slope
angle, step height/forward probe, floor probe, gravity factor and project layer/mask.
A controller uses **World** spatial binding and cannot share an enabled PhysicsBody.
Visual children may follow it. Separate Static/Kinematic bodies may not spatially
follow it; use World binding or deliberately authored compound collision instead.

PhysicsRuntime owns one Jolt CharacterVirtual and a native inner body per
controller. Character was evaluated: its rigid-body/PostSimulation route does not
provide the same native ExtendedUpdate stair/floor and collision-tested SetShape
workflow. FORGE uses CharacterVirtual's native mechanics rather than implementing
a second character collision solver. The inner body supplies ordinary body/query
presence; its native ID is resolved back to the owning EntityRef.

The fixed pipeline synchronizes copied requests, advances rigid bodies, advances
characters, then adopts simulation poses. FORGE integrates gravity explicitly:
ExtendedUpdate's gravity argument applies support force and does not integrate the
character's velocity. Movement intent is persistent world-space velocity planar to
the character up axis. Jump is a one-boundary request accepted only with walkable
support and no upward separation; it is not buffered until landing. Takeoff retains
platform planar velocity. Input keys, cameras, sprint and game rules remain outside
this service. There is no hidden platform parenting.

Characters use feet-origin translated native shapes. Capsule scale magnitudes must
be uniform; cylinder X/Z magnitudes must match, with independent Y permitted.
Radial signs preserve the symmetric solid; negative Y reverses the native feet-axis
orientation without rewriting authored rotation/scale. Native scale admission and
1mm–10km scaled dimension bounds apply. Crouching/standing updates the virtual and
inner shapes together. Blocked expansion retains the previous shape. Placement
first checks the candidate for solid overlap and reports rejection without moving.
Movement adopts LocalTranslation only; checked placement changes rotation only
when requested. No generic combined-TRS write materializes unrelated overrides.

Ground observations are copied values: OnGround, OnSteepGround, NotSupported, InAir,
normal, velocity, contact position and safely resolved supporting EntityRef. They
are transient. Character settings inherit and override through the same native
Flecs/component/property prefab paths as other registered components.

Runtime recovery must save both PhysicsSystem and each CharacterVirtual, plus
controller intent/crouch/request state. Recovery first reconstructs matching scene,
shape/filter configuration and native inner IDs, then restores native state. This
remains same-build ephemeral recovery, with the existing aggregate solver budget;
it is neither an authored Scene format nor a durable game save. Limits of1024
controllers and8192 combined native bodies are admission bounds, not a throughput
promise. No arbitrary gameplay callback executes inside native physics workers.

`PhysicsService` exposes copied character observations and queued movement, jump,
crouch and checked placement. Filtered linear shape casts support centered Box,
Sphere, Capsule and Cylinder with fixed orientation. Query mask/sensor policy is
independent of body pair masks. Sweep results use contact on the obstacle, outward
obstacle normal, fraction and EntityRef; initial overlap has fraction zero. Mesh
sweeps and angular sweeps are not claimed. Exact-SDK callbacks use size checks,
module capability and owner/fixed-tick guards; no Jolt pointers cross the boundary.

Validation covers steps/tall steps/stairs, allowed/steep slopes, translating/rotating
platforms, jump carry, light/heavy dynamics and filtered queries. Linux core/shared
SDK and strict sanitizer suites pass. Windows audit35944038189 passed339 general
editor input steps and76 physics-level steps, plus standalone-only startup. Physics
captures were retrieved and reviewed; physical graphics/audio remain separate.
Build260923-000066 predates these features.

### Collision resource subscriptions and selected-object preview

The runtime resource service's Collision kind delegates to PhysicsRuntime's existing
pool. It does not allocate another native shape cache. Subscriptions remain scoped
to the SDK module/world; releasing an observer does not retire a body using that
shape. Bad optional preloads report failure without making unrelated simulation
assets required. With realized rigid bodies, ready-result adoption waits for the
next physics synchronization boundary, including while Play is paused. Gameplay
must observe readiness before adding a previously unloaded AssetCollider.

The Scene's optional selected-object collision preview prepares immutable copied
geometry on a worker. The editor first extracts an immutable native shape snapshot
on the resource-owner thread; the worker never dereferences an owner-thread resource
lease. The snapshot retains its Jolt registration and shape across resource retirement.
It extracts triangles from native **leaf** shapes collected
by `CollectTransformedShapes`, preserving decorator/compound transformations and
center of mass. Direct GetTrianglesStart on a non-leaf is invalid in pinned Jolt.
Geometry remains in scaled local coordinates; moving the object only changes the
presentation transform. The8192-triangle preview limit is visible and never clips
actual simulation geometry. Preview colors distinguish prepared, disabled,
pending/stale and rejected candidates. It is not a substitute for full Play-world
hierarchy validation and does not appear in Game View.
When both controller and body components are retained, preview selection follows
the enabled representation. A disabled Character does not hide an active rigid
body; a disabled body's unused asset is not requested for a character preview.
Both enabled remain invalid. Both disabled preview the authored character shape.

Play requests copied character debug state only for the inspected entity. Ground
normal and a quarter-second velocity guide accompany the crouched/standing shape.
Queries and drawing do not take simulation ownership or serialize debug state into
Scene/prefab documents. Windows/WARP captures verify imported/convex/compound
outlines, the character reference envelope and grounded Play feedback.

### Physics material decision

This block retains per-body friction/restitution rather than adding an unnecessary
PhysicsMaterial asset. The existing reflected PhysicsBody fields are copied into
Jolt BodyCreationSettings. At the exact5.6.0 pin,
`Jolt/Physics/Constraints/ContactConstraintManager.h` initializes combined friction
as the square root of the product and restitution as the maximum of the two bodies.
FORGE uses those native defaults; it does not advertise artist-selectable combine
modes or per-triangle materials. Compound children currently share the owning body's
material response. Future per-surface reuse can justify a separate asset contract.

### Headless collision authoring

When asset tools are built, `forge_tools --assets import PROJECT SOURCE.collision.json`
uses the same Collision importer and publication validation as the editor. It reads
the authored AssetId, selects the platform's backend-neutral CPU profile, and rejects
subasset correspondence decisions (collision members are authored identities). Failed
cooking retains the previous catalog publication. Ordinary users can use the editor's
Save workflow; this entry point serves automation and acceptance-project generation.

## Authored physics joints

`forge.physics_joint` is an optional reflected `PhysicsJoint` component. A joint
entity selects two distinct enabled, nonsensor Physics Bodies by `EntityRef`, with
at least one Dynamic body. Multiple joint entities may connect the same pair.
Disabled joints create no native constraint and may keep empty or unavailable
references. Enabled unresolved/ambiguous/missing references or invalid settings
raise `physics.invalid_joint` with the joint identity and related body reference.
The complete desired joint set is checked before body/command mutation, including
animation transform candidate admission. Up to 4096 enabled joints are admitted.

Distance preserves a finite min/max interval in meters (.001..10000); equal
bounds make a fixed-length tether. Hinge joins two anchor points and allows
rotation around one selected X/Y/Z axis in each rigid body frame. Each selected
normal axis must differ from its hinge axis. The angle is zero when the two normal
frames agree; degree limits enclose zero, from [-180, 0] and [0, 180]. Equal zero hinge
limits are rejected; use Fixed when zero angular freedom is required. Position motors, springs, break force, ragdolls and joint-specific connected-pair
collision exclusion are not implemented.
Existing project layers/masks continue to own collision filtering.

Anchor coordinates use each entity's local origin, including effective signed
scale. Native construction converts them into Jolt COM-relative coordinates by
subtracting each scaled shape's center of mass. Scaled anchors are finite within
+/-10000 meters. Axis/normal choices use the rigid body's rotation frame, independent
of shape scale. The joint entity's transform is not a solver frame or an additional
transform parent; an explicit joint does not relax existing ancestry restrictions.

PhysicsRuntime owns all native constraints. Unchanged joints retain solver state;
edits or body replacement rebuild affected joints at synchronization. Constraints
retire before either body's destruction. A surviving enabled joint whose body was
removed rejects the candidate, preserving the live realization until the joint is
removed or disabled too. Adding/removing joints can swap Jolt's internal indices;
checkpoint capture records actual native order and restoration validates the full
joint identity/configuration set before native mutation. Version 1 ephemeral physics
checkpoints are intentionally incompatible; authored scene/prefab versions and
native ABI1 are unchanged. The exact SDK fingerprint changes because its public
physics settings header now includes PhysicsJoint; gameplay modules require a
matching SDK rebuild. Recovery retains the existing 2 MiB native/6 MiB envelope caps.

Joint values can be inherited from a structured prefab and overridden on an
instance. Its EntityRef fields still name explicit scene entities; they are not
prefab-definition member IDs. Reusable rigs with automatic instance-local body
binding require a separate typed prefab-member reference contract. The current
body picker assigns the instance's body targets explicitly. Scene/subtree
duplication remaps admitted intra-scene joint references; opaque extensions remain
untouched. Source publication and snapshot reopening preserve normal inheritance.

Kinds2 Point and3 Fixed extend the same PhysicsJoint value; kinds0/1 retain their
Distance/Hinge meaning. Point constrains only its two COM-converted body-origin
anchors. Fixed additionally aligns two explicit body-local frames: normal as X,
axis cross normal as Y. Perpendicular choices are required. The frames are stable
authored data; arbitrary current relative pose is not implicitly captured during
runtime realization or recovery. Both use exact pinned LocalToBodyCOM settings;
Fixed disables automatic point detection. Distance/angle settings are inactive
but preserved and bounded for these kinds. The existing native2 recovery envelope
and lifetime/admission/identity rules are unchanged. Public enum documentation
updates the exactSDK fingerprint; native ABI1 and field layout remain unchanged.

### Slider joints

Kind4 Slider locks relative rotation and both transverse directions while allowing
translation along Body A's selected rigid-frame axis. Perpendicular normal and axis
choices define each body's matching frame. Explicit body-local anchors use the same
signed-scale and COM conversion as other joints; automatic native point detection
is disabled. Travel is the signed projection of Body B's anchor minus Body A's
anchor onto Body A's axis, in world meters. Shape scale does not multiply limits.

`min_travel` is finite in [-10000, 0] and `max_travel` in [0, 10000]. Zero belongs to
the interval; zero-width travel is rejected in favor of Fixed. Inactive distance
and angle values remain saved and bounded. Separate travel fields default to -1/+1
when absent through the existing builtin admission hook and joint codec. Legacy
kinds0..3 keep their meanings; older readers reject kind4. The added public fields
change SDK layout and fingerprint, requiring a matching rebuild. Native ABI1,
private checkpoint4 and authored formats remain unchanged. Native order, retirement
and recovery use the existing joint owner.

Scene24 contains vertical and inclined carriages under gravity. Inspector travel
controls support ordinary Undo/save/reopen; Scene guides show the allowed interval
and current signed travel. Velocity and position drives are described below;
break behavior is not implemented.

### Bounded velocity motors

Hinge and Slider use the existing joint value's disabled-by-default `motor_enabled`.
Hinge's `hinge_motor_speed` is signed degrees/s ([-36000, 36000]); Slider's
`slider_motor_speed` is signed meters/s ([-1000, 1000]). Targets describe Body B
relative to Body A along the admitted frame. The owner converts hinge degrees to
native radians once. `max_motor_torque` (N*m) and `max_motor_force` (N) bound native
effort symmetrically in [0, 1000000]. Defaults are90degrees/s,1m/s,10N*m and100N.
Zero effort coasts; an enabled zero target brakes with its finite effort budget.
Body speed clamps, external loads, damping and joint limits can prevent attaining
a target. A Dynamic support receives reaction effort; a Static support stays fixed.

The Inspector shows only the active kind's motor controls. Inactive fields remain
saved and bounded without driving or waking bodies. Legacy builtin admission and
the codec backfill disabled motor defaults. Motor-only edits update the existing
native constraint after complete candidate preflight, retain geometric frames,
manager order and solver state, and wake moving bodies only when active controls
change. Body replacement and structural edits retain their existing retirement path.
No new command queue, constraint manager or motion owner is introduced.

C++ can write the existing typed PhysicsJoint in the fixed phase before Physics.
Recovery capture still requires synchronized component values; unsynchronized
motor edits reject like other joint edits. Matching configuration plus exact native
solver state restore active drives through private checkpoint4. Public field layout
and SDK fingerprint change, requiring matching modules; ABI1 and authored versions
are unchanged. Position targets and spring drives are described below.

Gallery25 uses zero-gravity, force-limited slider and hinge drives to their positive
stops. A separate compiled SDK fixture writes the same typed Slider controls before
Physics, observes actual copied body velocity afterward, and checks fresh worker
recovery. Independent centered-box checks use F/m and torque/I: a0.6m cube of mass2
has I=0.12kg*m²;1N yields0.5m/s² and0.06N*m yields0.5rad/s². These are fixture-specific
checks, not a universal settling guarantee for arbitrary geometry or impacts.

### Bounded position drives

`motor_mode` is Velocity(0, default) or Position(1). Position uses
`hinge_motor_angle` in signed degrees relative to the admitted rest frame, or
`slider_motor_position` as signed anchor displacement in world meters. It shares
the enable switch and physical torque/force budgets with velocity mode. An enabled
position drive rejects targets outside the matching authored angle/travel interval
before native mutation; it does not silently clamp an authored target. The angle
is bounded to[-180,180] and displacement to[-10000,10000]. Hinge angle error uses
the native wrapped rest-frame convention; a target is not a prescribed rotation path.

`motor_frequency` is finite[.01,100]Hz (default2); `motor_damping` is a damping ratio
in[0,10] (default1, critical). The existing pinned solver normalizes stiffness and
damping by effective mass/inertia: k=m_eff*(2*pi*f)^2 and
c=2*m_eff*ratio*(2*pi*f). Effort budgets, external load, speed limits and body
geometry still affect motion and can prevent reaching the target. Zero effort
coasts. Zero frequency is rejected because the selected Position motor would stop
applying a restoring drive.

The spring uses implicit Euler integration, which adds numerical damping even at
ratio zero. Large frequency relative to the fixed rate has substantial integration
damping; admitted input bounds are not an arbitrary-geometry settling guarantee.
Independent centered single-axis checks use
v_next=(v+dt*w^2*(target-x))/(1+2*ratio*w*dt+w^2*dt^2) and
x_next=x+dt*v_next, with w=2*pi*f. Separate bounded-effort checks use F/m and torque/I.

Mode/target/spring edits retain native geometry, manager order and warm state;
inactive mode and kind values remain authored without driving or waking bodies.
C++ writes the same typed controls before Physics. Synchronized checkpoint4 restores
actual native spring trajectories in a fresh runtime; no additional owner, queue,
public callback or durable state is introduced. Legacy values backfill Velocity,
zero position targets and2Hz/ratio1. Matching public-layout/header fingerprint
requires module rebuilds; ABI1 and authored versions remain unchanged.

Inspector mode selection shows only relevant targets and spring controls, using
existing Undo/save/reopen. Selected Scene guides show the target, frequency, damping
and budget with a separate target marker. Original Gallery26 returns its slider to
-.75m and hinge to-45degrees. It supports ordinary Play and source-free export.

### Editor joint guides and numerical limits

The existing Scene PhysicsOverlay derives selected-joint guides from the effective
snapshot using the same admitted world-pose decomposition hints as body realization.
It applies signed scale to body-origin anchors and only rigid rotation to axes;
collision center of mass does not move an authored anchor. The guide owns no native
body, asset preparation job or document/history state. Missing/ambiguous/other-scene
references and disabled/sensor/non-dynamic pairs receive authored-preview diagnostics.
Other loaded runtime scenes are not resolved by the one-scene editor snapshot.
Selected collision geometry and joint guides have separate saved visibility controls.
All guide primitives/text are Scene-clipped; there is no gameplay/export overlay.

Pinned Jolt's finite iteration profile is not an exact weld/transform-parent promise.
A regression that moved a support 2.45m in one 1/60s tick excited persistent fixed
joint oscillation (147m/s implied target). Normal continuous 2.45m/s translation plus
0.4rad/s rotation instead passes a 4cm pivot bound on each tick, and one-degree frame
agreement after settling under gravity. Frame construction is separately checked
without gravity to 0.003 unit-vector chord. These are attributable fixture bounds,
not universal engine accuracy/performance guarantees or a solver-setting upgrade.

A small mirrored cooked box with a distant attachment also rebounds between slider
stops after an 8m/s impact under the current finite solver profile. Anchor/COM
conversion is checked independently under a steady 0.5N load: both signed endpoints
settle within4mm after six seconds, with speed below1mm/s. Centered/scaled/rotated/
hierarchical fixtures separately check fast endpoints and locked frames. These are
fixture-specific bounds; exact impact settling is not promised for arbitrary
leverage, geometry or loads.

## Optional continuous collision

PhysicsBody's trailing boolean `continuous_collision` defaultsfalse and is backfilled
by the existing admitted-builtin optional-field hook for old Scene/Prefab values.
Reflection, generic authoring/UI/Undo, snapshots, body configuration equality and
private checkpoint mappings include it. Matching exactSDK fingerprint/layout is
required; scene versions and native ABI1 are unchanged. Configuration changes use
the existing replacement path, preserving same-motion velocities and sleep policy.

Native `EMotionQuality::LinearCast` is selected only for Dynamic nonsensor bodies;
other modes preserve the authored flag but use Discrete. Before writes/retirements,
the existing shape/mass admission validates a finite positive native inner radius.
Resource replacement and detached transform-candidate checks share that admission.
Pinned Jolt5.6.0 integrates step rotation then casts translation, not a full angular
sweep. Native finite penetration/time-stealing/contact-cache behavior remains native;
CCD is not a guarantee against every angular or repeated small-penetration failure.

The exact ScaledShape implementation retains signed scale and returns signed minimum
scale times child inner radius. Some mirrored AssetCollider/native compound trees
therefore cannot use this CCD profile. Do not silentlyabs scales, vendorpatch or
replace the native solver. Default discrete mirrored assets remain supported;
inline symmetric shapes use their already-admitted positive native geometry.

Original Gallery22 uses ordinary gravity and two thin platforms placed to expose a
discrete fixed-step miss. Native tests use independent first-step thin-wall controls
on X/Y/Z, continued checkpoint motion and explicit old-value default admission.
Actual render bounds/resources and relocated source-free content consume the saved
flag. Windows delivery/physical acceptance are separate from local proof.
