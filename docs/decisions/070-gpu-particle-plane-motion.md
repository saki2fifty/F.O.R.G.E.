# ADR070 — GPU analytic particle plane motion

Status: accepted.

## Existing ownership

Extend the built-in GPU particle motion kernel through its existing renderer-owned
emitter, pipeline/binding, bounded instance buffer and count publication. The
emission owner supplies the same fixed-clock ordinal window and short float ages.
No particle entities, second simulation clock, physics World, persistent birth
history or runtime readback/CPU collision fallback are introduced.

A private explicit motion mode selects an analytic plane kernel. Its additional
32-byte constant buffer contains the normalized emitter-local metric plane and
bounded restitution/tangential loss/rebound response. The original47-row input and
seven-row vertex output stay unchanged. The canonical free-flight kernel is
composed from shared birth/appearance/transform fragments, retaining its exact
previous shader bytes. Authored programs retain the same source/reflection.

The plane variant reuses those fragments and the same analytic contact semantics
as ADR067 at GPU float precision. Find the first incoming contact along the path,
including crossings hidden between displayed samples; do not clamp a frame's final
position to impersonate collision. Normalized distance and velocity have at most
one relevant extremum under constant acceleration/nonnegative drag. Bounded root
search and0–16rebounds bound work; exhausted or initially penetrating births die.
Nonpenetrating tangency and finite no-force drag asymptotes are not impacts.
Source double precision and GPU visual float precision are distinct contracts.

## Drawing and failure

A killed birth writes finite zero-scale geometry in its existing reserved row
range. Count remains the admitted GPU birth-row count, including killed degenerate
rows; it is not an observed live-survivor count. This avoids a readback/compaction
queue and retains resource budgets. Position, appearance and geometry still use the
same shared Mesh drawing/Material/depth and optional weighted/opacity projections.
Counts clear before validation and publish only after required dispatches succeed.
The camera owner retains target restoration. Invalid/missing motion packets fail
before dispatch; ordinary free-flight and authored-program modes reject plane data.

An arbitrary graph position output does not define the analytic velocity required
for this contact model. Do not silently reinterpret an authored trajectory as free
flight. Source13 adds `gpu_collision_enabled`, independently of CPU
`collision_enabled`, sharing the existing plane object. Enabling GPU plane motion
selects built-in Mesh evaluation and disables GPU camera alignment. Selecting Graph
retains plane settings inactive. Explicit GPU camera alignment disables GPU plane
activation. CPU geometry/appearance/trail choices stay retained.

Values20/`gpu_collision1`,21/`gpu_weighted_collision1` and22/
`gpu_weighted_collision_opacity1` reuse ordinary Mesh geometry, adding weighted
transport and lifetime opacity through the existing projection when selected.
Require effect3/GPU1 and typed `runtime_particle_gpu_collision:1`;21/22 also require
weighted1 and22 requires GPU opacity1. Older kit admission fails closed. No CPU
alpha/opacity capability, graph program or camera geometry is implied. Existing
recipes/envelopes, importer/stop IDs, component/gameplay ABI and earlier value
meanings are unchanged. Native controls and automation/MCP commit the same source
operation, with optional `backend=cpu` default or `gpu` on `particle_collision`.

## Acceptance boundary

Detached actual-device checks compare the existing CPU motion owner with float
inputs; original birth/RNG and complete device rows are independently checked.
Oblique/surface/tangent precision discontinuities need explicit coverage and limits.
Actual runtime-class lifecycle/refusal/recovery and opaque/weighted/opacity pixels
precede public profile admission. The complete native/source/MCP/history/publication/
kit/Gallery/source-hidden export/manual gate is required before describing the
feature as available. Windows and physical acceptance remain separate.
