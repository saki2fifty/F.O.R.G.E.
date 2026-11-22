# ADR078 — Resting CPU Scene particles

Status: accepted; implementation and the coherent local integration gate passed.
Native Windows package execution and physical acceptance remain separate.

## Ownership and compatibility

Extend the existing ParticleSceneState and SceneHistory from
[ADR073](073-scene-particle-collision.md), [ADR075](075-xy-scene-particle-collision.md),
[ADR076](076-historical-world-particle-trails.md) and
[ADR077](077-scene-particle-impact-notifications.md). Keep the copied supporting
collider reference and unit normal in the birth's existing transient state.
PhysicsService still owns native queries; ParticleRuntime still owns detached
whole-candidate admission, emission age, traces and lifecycle. Introduce no
particle entities, second World, solver service, registry, clock or saved state.

Append source21 with required Boolean scene_resting_enabled and immutable values
62–73. Retain all earlier profile meanings and IDs. Mesh, camera-plane and World
ribbons each compose with 3D/XY queries and the independent impact-notification
choice. Select profiles through the existing canonical value-profile table.
Native controls and automation/MCP call the same source operation, history,
revision, permissions, publication and persistence owners. Inactive intent remains
saved across evaluation changes; disabling an absent choice is an exact no-op.

## Supported motion and bounded work

An incoming zero-restitution contact can remain alive when inward acceleration
presses it against a surface and at least one rebound is permitted. Apply the
existing contact friction once to tangent velocity. Constrain only inward normal
acceleration, retaining tangent acceleration, velocity and drag. Existing initial
penetration/zero-normal XY overlap and explicit rebound-limit termination remain.

Revalidate copied support against the current filtered native sphere/circle query.
A stationary supported birth needs one probe per advancing interval. Sliding uses
intervals no longer than 1/120 s with a radius/4 travel bound, plus ordinary motion
casts and endpoint support checks. Missing or changed support releases ordinary
motion; a replacement surface must establish its own contact. XY depth remains
analytic and independent. The existing metric/coordinate-aware departure bias
and contact-knot presentation remain authoritative.

Charge every probe and cast to the existing 256-query birth and 8192-query World
ceilings. Overflow or malformed/failed queries reject the detached candidate,
preserving the accepted history. This approximates discrete support and edges;
it is not continuous moving-surface collision, a rigid-body solver, two-way
impulses, surface-velocity transfer or static/Coulomb friction simulation.

## Observations, authoring and export

Optional copied impacts retain the existing SDK record and callback shapes. The
landing is a Bounce; continuing support emits at most one Touch per birth in an
advancing interval. These are copied observations after whole-candidate adoption,
readable in the next fixed tick under the existing permission/timing contract.
No synchronous callback or second event store is introduced. No new installed
SDK ABI is required; exact-boundary evidence is checked at the coherent gate.

Native **Rest on Scene surfaces (CPU)** and
asset.document.particle_scene_resting share the source setter. Isolated Effect,
Timeline and stopped Scene previews still require Play. Reset, disabling,
seed/effect replacement and ordinary expiry clear support through existing owners.
Exports additionally require typed runtime_particle_scene_resting:1, alongside
selected query, geometry and notification capabilities; reject incompatible kits
before replacing output. Gallery, source-hidden runtime/rendering, UI/MCP, strict
compilation, scoped graceful sanitizer checks and manual reconciliation are part
of the complete feature gate. Design or focused proof alone is not delivery.
