# ADR066 — Emitter-local analytic particle trails

Status: accepted; implementation and coherent local integration gate passed.
Native Windows trail/control/export and physical acceptance remain separate.
This adds explicit source/value/kit support alongside ADR060–065. It does not
change previous source profiles, immutable cooks or gameplay component ABI.

## Simulation and frame ownership

Trails use the existing fixed particle clock, emitter identity, seeded birth
ordinal and analytic trajectory/appearance evaluator. Sample past ages from the
same birth state rather than reverse-integrating current velocity. No second
clock, per-particle entity registry or persistent pose-history system is added.

The renderer's existing ParticleSource holds detached samples for the current
frame. Clear those samples when updating the frame. One admitted mesh/resource
proxy remains in the existing particle-instance collection. Each camera derives
its own segment edges through the ordinary draw append, bounds and sort owners;
view storage is reserved before any addresses are borrowed.

This is an emitter-local path. The current complete emitter affine transforms
every sampled position; its X-column length scales ribbon width. Moving the
emitter moves the whole path. Historical world-space emitter motion, collision,
GPU trails and a GPU-to-CPU fallback are outside this profile.

## Sampling and geometry

Trail duration is 0.001–30 seconds and each birth has at most 1–16 segments.
Sample ages cover the visible interval clipped to the original particle life.
After a head reaches its lifetime, its tail shrinks until life plus duration.
Appearance is evaluated against original life, never the extended tail lifetime.
Use flat point storage and ordered birth ranges; no per-birth heap or saved handle.

The strip is a bounded piecewise-linear approximation. Width, color and opacity
are evaluated at sampled ages and interpolated along each segment. Increasing
segments resolves more trajectory and appearance detail; a discontinuous lifetime
curve is still represented by these sampled vertices. Path V coordinates run
from 0 at the visible tail to 1 at the visible head, including a shrinking tail.

Adjacent segments use the same endpoint side/width values. Independent affine
rectangles cannot represent arbitrary joined curved or widening strips. Extend
the existing seven-row static Mesh instance packet with private flag4 and explicit
start/end positions, side vectors, appearance and V coordinates instead. This is
transient renderer data, not another mesh asset or installed gameplay layout.
Select endpoint values directly in the shared vertex factory so joins retain
identical device arithmetic. Reuse the shared safe surface-frame helpers for
normals/tangents and retain canonical Quad UV handedness.

Bounds enclose all four actual corners with outward padding for camera-relative
float conversion and shader arithmetic. The whole-lifetime envelope retains the
existing analytic motion bound with an isotropic width allowance. Every segment
enters ordinary per-camera culling and Material sorting. Genuine birth ordinals
remain intact; a separate transient segment index supplies deterministic ties.
Emitter-only transparent ordering is insufficient.

## Budgets and Materials

Continuous capacity covers `ceil(rate * (life + duration))`; burst count still
fits capacity. Reserve `capacity * segments` against the existing 8192 Scene
instance limit and retain the 64-emitter limit. Charge flat samples and generated
view geometry to the existing 8 MiB CPU frame payload budget. Whole-candidate
rejection must precede adoption; do not silently truncate paths or segments.

Use Engine/Quad and double-sided built-in Materials without skinning, morphs,
wind, transmission, render-texture inputs or custom Shader surfaces. Opaque and
masked Materials require identity particle opacity. Blend requires depth testing
without depth writes and uses ordinary cross-emitter/ordinary-Mesh sorted alpha.
Endpoint color/opacity multiply the Material and vertex appearance once. Reuse
the complete-batch Mesh upload validation before publishing instance bytes.

## Source, programmatic authoring and compatibility

Source9 retains `trail_enabled` and `trail: {duration, segments}` independently of
the original Mesh, CPU/GPU alignment, program, opacity values and local stop IDs.
An active trail selects values10/`cpu_trail1`. Disabling restores the retained CPU
sorted-alpha projection. Backend changes preserve inactive trail settings without
imposing trail segment/capacity restrictions on an active GPU profile. An inactive
GPU alignment operation must not downgrade Source9.

Native controls and `asset.document.particle_trails` call the same source operation
and existing document/history/publication owner. Optional settings replace both
duration and segments; omission retains them. Preserve grants, exact revisions,
no-op behavior, Undo/Redo, save conflicts and reopen semantics. There is no second
agent implementation of trail editing.

Recipe/envelope3 remain current. Values10 require effect3 plus independent typed
`runtime_particle_trail:1`, alpha1 and opacity1 capabilities. They do not acquire
GPU, graph or camera-plane capabilities. Missing, malformed or unsupported kits
reject before replacing a distribution. Previous value/source bytes and importer
identity remain unchanged; no migration is required.

Acceptance includes original sampler regression, codec/source retention and
negative cases, joined geometry/bounds/budgets, actual device comparison with
ordinary baked triangles, cross-emitter/ordinary-Mesh composition and view/error
recovery, native/MCP history and publication, and source-hidden Gallery/Game
export. Local software, native Windows and physical-device acceptance are recorded
separately; the accepted design alone does not establish a completed capability.
