# ADR079 — Linear Scene-motion query intervals

Status: accepted bounded design; implementation and integration are in progress.
This does not announce unrestricted moving-surface continuous collision support.

## Boundary and compatibility

Extend the existing PhysicsRuntime/PhysicsService and its subordinate Native2D
backend. They retain native geometry, canonical bindings, filtering, fixed phases,
commands, discontinuities and lifetime. Keep copied pre/post-step native poses
and immutable shape references only as transient observations within that owner.
Use the existing native broad phases and shape-query implementations; do not add
a particle-owned physics world, geometry database, registry, solver or clock.

This deliberately extends the current-snapshot restriction in
[ADR073](073-scene-particle-collision.md), [ADR075](075-xy-scene-particle-collision.md)
and [ADR078](078-resting-scene-particles.md) through separate opt-in profiles.
Earlier query methods, cooked profiles, IDs, authored defaults and SDK callbacks
retain their meanings. The internal service interface is outside the installed
project-code SDK. Any actual installed boundary change needs a separate deliberate
fingerprint/compatibility check; do not assume it from a method name.

## Native observations and query contract

Host consumers opt into motion observation through the existing service before
the needed fixed step. Activation is World-scoped and remains enabled until that
service retires; no lease registry or durable project setting is introduced.
Consumers cannot reconstruct an interval before activation. Worlds that do not
request it retain their ordinary physics path without per-body motion recording.
The existing positive fixed physics step captures realized rigid/character poses
before native movement and observes the end pose after native updates. Publish the
interval only after successful adoption. Retain the existing physics tick and
actual step duration. Admission, reconstruction, retirement, shape replacement,
teleport and static relocation must not invent a historical sweep. Stale or
partially adopted intervals reject; failed candidates do not publish observations.

A query supplies the existing convex sweep, normalized interval slice 0..1 and
an explicit remaining native narrow-phase budget. A copied result carries optional
contact, world surface velocity and actual charged narrow-phase work. Query space
remains typed: XYZ and XY do not reinterpret each other. XY depth stays with its
existing analytic particle owner.

For unchanged orientation and geometry, cast relative displacement against the
native shape at the slice start. Map the native fraction/witness back to physical
contact time. Surface velocity comes from the copied translation over the actual
fixed duration. Use native shape/filter/binding owners for compounds, meshes,
sensors, masks, exclusions and precision; do not implement a replacement narrow
phase. The selected native APIs and their contact tolerances are the contract.

This bounded profile covers linear translation between adopted poses. It does not
establish an angular sweep, curved surface trajectory, historical emitter-pose
reconstruction or two-way rigid-body impulses. Potentially intersecting unsupported
rotation/geometry changes must report an explicit diagnostic rather than silently
skip surfaces or claim continuous coverage. Use conservative native candidate
bounds; ordinary current-snapshot profiles remain available independently.
Use the existing FORGE orientation-equivalence policy for copied endpoint poses:
native normalization can change numeric components without angular motion. Keep
observed nonzero native angular velocity as a separate rejection condition even
when endpoint orientations compare equivalent.

XY interval casts use a first-contact adapter around the native query. Keep the
actual-radius initial-distance check, compensate the native encroachment skin in
the cast proxy and disable encroachment there. Include the endpoint with one float
step of cast fraction allowance, admitting at most that rounding adjustment back
to1. For a native zero-normal contact in the padded band, reuse the positive
actual-distance query's native witness/normal. Genuine initial overlap retains its
zero-normal sentinel. This avoids turning short-chord contact into a later initial
penetration; it does not add a narrow-phase implementation or change older
current-snapshot queries. Both native operations retain their ordinary work cost.

## Budgets, time and response

Charge every native shape cast and explicit XY initial-distance check to the
existing per-birth and World particle query
budgets, including misses and support probes. Bound broad-phase callbacks using
the existing query work policy; overflow rejects the complete candidate. Reuse
native spatial indexes and bounded collectors instead of a second acceleration
structure. Empty queries still charge an operation, preventing zero-cost loops.

SceneHistory maps each birth/chord slice from particle playback time to the actual
fixed interval. Surface velocity must use the same time unit as response motion;
never mix scaled particle age with real-step velocity. Paused playback preserves
its existing freeze semantics. Ongoing support and relative normal/tangent response
remain with the existing motion/history owner, not a new platform controller.
Copied impact observations retain post-adoption publication and next-tick reads.

## Integration gate

Complete implementation requires both native domains, preserved legacy behavior,
chronological contacts and witness/velocity tests, filtering, retirement/teleport,
support/playback interactions, strict native work accounting and failure atomicity.
Native controls, shared automation/MCP revisions/history/publication, explicit
source/value and typed-kit admission, editable/source-hidden Gallery, runtime/Game
checks and accurate manual limits belong to the same coherent batch. Strict/scoped
graceful sanitizer evidence, exact SDK-boundary checks and platform/physical
acceptance remain distinct. A pure native geometry probe is design evidence only.
