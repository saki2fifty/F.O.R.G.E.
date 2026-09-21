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

`PhysicsBody`: `motion` 0 Static / 1 Kinematic / 2 Dynamic; density kg/m³; mass kg (zero means density); friction; restitution; gravity multiplier. `BoxCollider` has full XYZ dimensions; `SphereCollider` has radius; `CapsuleCollider` has radius and straight cylinder height, Y axis. All use meters before effective world scale. One centered primitive per body is the current subset. A later shape-owner/compound representation can add associated shapes without changing these dimensions or turning Jolt indices into asset identity.

Optional reflected components use existing scene-v3/v4 and prefab-v1 containers. No scene version bump, extra resource manager, persisted solver handles, or separate prefab mechanism. Components retain Flecs inheritance, independent channel ownership and property-level explicit intent. `component.add` creates optional default values; `property.set`, `property.revert`, and `component.revert` share existing transaction/history behavior.

## Fixed tick and transform ownership

Input → Gameplay → PrePhysics (transform evaluation / realization / targets) → Physics (one Jolt Update with fixed dt) → PhysicsAdoption → PostPhysics consumers → final Transforms → existing presentation capture.

Static placement comes from evaluated world transforms. Kinematic targets use upstream `MoveKinematic` with fixed dt, not per-render-frame teleportation. Dynamic bodies require spatial World and write only local simulation translation/rotation after Update. WorldTransform remains exclusively derived by the transform module. Scale is folded into shape geometry. Physics rejects shear and zero or Jolt-invalid world scale; sphere/capsule world scale magnitudes must be uniform. Signed box/sphere/capsule scales describe symmetric solids: magnitudes are baked into dimensions without changing authored scale signs. Scaled collider dimensions must remain .001..10000 meters to bound mass/inertia calculations. Physics requires unambiguous scene/entity references within its world; repeated simultaneous instances of the same scene asset need a future runtime instance scope. Coordinates are bounded to ±1 billion meters with double Jolt positions; this is an input bound, not a large-world quality claim.

Config changes are compared against realized settings at a safe boundary. Compatible motion-type rebuilds preserve linear/angular velocity and activation. Motion-type changes initialize new state. Unchanged bodies are not recreated or reactivated. Invalid realization is diagnosed; no approximation discards shear. Dynamic direct transform writes are rejected; gameplay must use explicit teleport. Teleport writes supported local channels and resets existing presentation history after the tick.

## Queries, commands and contacts

Raycast returns closest hit EntityRef, point, normal and fraction; displacement defines ray length. Results are owning FORGE values and never expose BodyID. Teleport/kinematic commands are bounded to 4096 pending requests and processed at the next pre-physics boundary. PostPhysics commands therefore take effect next tick. Direct physics-component writes belong in Gameplay before synchronization; capture rejects unsynchronized component or world-pose changes after adoption, including movement inherited from nonphysics ancestors. Commands preserve world-space target semantics for supported parent transforms.

Static and moving are internal filtering classes. Static/static pairs are excluded; moving bodies interact with static/moving. These Jolt layer numbers are not serialized. Future authored collision layers need their own versioned project contract.

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

Compound authoring, mesh/heightfield collision, character/vehicle/joint/soft-body/cloth systems, authored named collision filters, advanced material assets, collider visualization, broad physics query families, and durable savegames. Physics does not authorize the audio/animation/navigation/game-UI phases.

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
