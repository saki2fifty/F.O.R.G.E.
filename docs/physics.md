# Physics module and transient recovery


`forge.physics` separates reflected engine-owned authoring values from Jolt-owned simulation objects. `physics_components.hpp` contains POD settings; `physics_service.hpp` contains owner-thread queries/commands. `physics.hpp` is host composition/internal runtime infrastructure, excluded from the experimental SDK distribution.

## Effective spatial ancestry

Before body realization, each fixed-tick synchronization, checkpoint capture, and recovery reconstruction, Static/Kinematic bodies are checked against the existing Phase 3 effective spatial-parent graph. Traversal includes nonphysics intermediaries and Explicit links and stops at World binding. Reaching a Dynamic PhysicsBody raises `physics.unsupported_dynamic_ancestry`. The structured diagnostic carries the affected EntityId/scene AssetId, `related_entity` as the Dynamic ancestor's EntityRef, module, property, tick and an explanation. Runtime candidate failures retain this diagnostic after candidate destruction and leave the previous runtime unchanged while discarding the failed candidate.

Validation covers body/configuration changes, spatial reparenting and prefab realization/reconciliation when runtime synchronization consumes those changes, including scene replacement and recovery. Authoring worlds still have no solver; authoring an unsupported arrangement does not silently repair it. Physics realization rejects it. Structural ChildOf with spatial World is allowed; nonphysics visual descendants may follow Dynamics; supported non-Dynamic ancestry remains allowed. This initial restriction leaves compound colliders, constraints and deliberate attachment semantics for future work.

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

## Queries, commands and contacts

Raycast returns closest hit EntityRef, point, normal and fraction; displacement defines ray length. Results are owning FORGE values and never expose BodyID. Teleport/kinematic commands are bounded to 4096 pending requests and processed at the next pre-physics boundary. PostPhysics commands therefore take effect next tick. Direct physics-component writes belong in Gameplay before synchronization; capture rejects unsynchronized component or world-pose changes after adoption, including movement inherited from nonphysics ancestors. Commands preserve world-space target semantics for supported parent transforms.

Static and moving are internal filtering classes. Static/static pairs are excluded; moving bodies interact with static/moving. These Jolt layer numbers are not serialized. Authored layers use stable named project slots and symmetric masks through native CollisionGroup filtering, described below.

Contact callbacks append bounded token records under a mutex. After Update, the owner thread resolves current tokens to EntityRefs, drops retired-body tokens and exposes `PhysicsContact` values until the next adoption. These are **solver contact-cache Begin/End**, not a promise of geometric overlap Begin/End: Jolt can remove contacts when bodies sleep. Events are transient; no generic bus or persisted contact entities. The narrow exact SDK currently exposes raycast and movement, not contact iteration.

## Private recovery

Runtime protocol 2 adds an optional private recovery payload beside the existing scene response. ABI1 `module_api.h` layout/version is unchanged. Controllers retain separate latest and pending-activation checkpoints; probes receive the corresponding checkpoint without advancing the live runtime.

At a completed fixed boundary, the outer envelope captures scene plus available prefab source revisions, simulation frequency, source session and tick, and physics envelope. Physics adds format version, build/source/runtime fingerprint, exact Jolt configuration, gravity, tick, EntityRef-to-explicit-BodyID/config mapping, pending FORGE commands, and Jolt StateRecorder bytes. A content checksum detects accidental alteration; this trusted local native-process transport is not an authentication/security boundary.

Restoration constructs an unpublished candidate world, validates metadata and expected source session/tick/content, restores scene/configuration, validates unique body indices/sequences and EntityRefs/configs, constructs bodies with **CreateBodyWithID**, then calls Jolt **RestoreState**. Only successful restoration swaps the candidate into use. BodyID is a transient reconstruction token. Jolt owns the solver encoding. See upstream [PhysicsSystem](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp), [BodyManager](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyManager.cpp), and [StateRecorderImpl](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/StateRecorderImpl.h).

Paused restoration performs no Update. Clock tick is restored; elapsed/accumulator debt is reset; input is released; previous/current presentation samples coincide. Step advances one tick. Controllers resume only when prior policy calls for it. A failure leaves the prior published runtime intact where it exists, or reports that recovery failed and clean Play is needed.

Limits: 8192 bodies, 16384 contact events, 2 MiB raw solver bytes, 6 MiB physics envelope, 8 MiB total checkpoint, existing 16 MiB transport. Exceeding limits is a diagnostic, not silent truncation. Checkpoints never enter scene/prefab/project files. No save/load-game UI, cross-build/platform compatibility, deterministic replay, or arbitrary native global/resource recovery is promised.

## Experimental SDK

The exact fingerprint covers the callback table plus FORGE physics component definitions. The existing callback table has size-checked extensions for Physics capability, PostPhysics phase, raycast and movement. Modules still use one shared Flecs implementation, matched compiler/config/CRT, and startup-only code leases. No Jolt headers or static implementation enter a gameplay module. ABI1 retains its existing host-owned movement contract; it gains no physics API.

## Deferred

Heightfield collision, vehicles, joints, soft bodies, cloth, advanced material assets and additional query families remain future work. Durable game saves use explicit game-owned data; private solver checkpoints are not a save-game format. Compound, triangle-mesh, character, named-layer and collision-preview support are described below.

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
