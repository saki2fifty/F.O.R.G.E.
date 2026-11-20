# ADR071 — GPU analytic particle ribbon trails

Status: accepted design. Complete feature acceptance requires the integration
gate below; the design alone does not establish a shipped workflow.

## Existing ownership and geometry

Extend ADR060's renderer-owned emitter realization with explicit built-in ribbon
geometry. Retain the fixed emission clock, seeded birth ordinal, source/history
and Material owners. Sample the emitter-local analytic trajectory at bounded past
ages rather than storing history, reverse-integrating velocity or reading device
positions back to the CPU. Arbitrary authored trajectories and plane collision
are separate profiles and cannot silently acquire analytic trails.

Use ADR066's existing seven-row flag4 joined ribbon packet and shared Mesh vertex
factory. Each birth reserves 1–16 segment slots in the existing vertex/UAV buffer;
the complete effect reserves at most 8192. Draw through ordinary 64-instance slices.
Each thread calculates a birth's endpoint values once, so adjacent segments reuse
identical positions, sides, lifetime appearance and path-V bits. Unused or collapsed
paths retain finite degenerate slots. Published count means reserved segment slots,
including degenerate slots, rather than an observed survivor count.

Birth membership uses CPU double precision over life plus trail duration. Only
short relative float ages reach the device; sample ages clip to original life.
The tail shrinks after the head expires. Appearance remains normalized to original
life and path V spans the visible tail to head. Float quantization can collapse a
very short interval; it does not change the admitted birth window or invoke a CPU
fallback. Trail duration remains 0.001–30 seconds, as in ADR066.

Derive tangent differences from transformed motion displacement before adding the
birth origin or camera-relative emitter translation. Otherwise a tiny trail can
lose its frame through cancellation even near an ordinary camera. Add those offsets
when packing endpoints. Use the existing safe surface normalization helper and
camera-forward side/sign continuity. Width uses the emitter X-column norm; current
emitter motion transforms the entire path, without historical world-space motion.
Packed endpoints that coincide after translation retain a degenerate slot, even
when their unshifted displacements differ. Use the existing view-local matrix
storage and CPU payload budget for an identity-linear draw proxy: the kernel has
already transformed the ribbon, so emitter parity must not flip its lit face again.

## Private upload, resources and compatibility

Share the original declarations, birth/RNG, analytic motion and appearance shader
fragments. Preserve the complete original free-flight, plane and view-projection
shader strings while extracting those fragments. No runtime textual patching is
part of product shader construction.

Reuse the existing private 352-byte view/appearance buffer. For this distinct
built-in kernel, its geometry-count lane carries segment count and the unused
fourth timing lane carries duration. Camera rows and opacity endpoints/stops keep
their existing meanings. Bind this buffer to the ribbon kernel directly; running
the ordinary Mesh/billboard projection afterward would overwrite flag4 geometry.
The immutable 47-row analytic/authored-program input and seven-row vertex layout
remain unchanged. This private upload is neither serialized nor a gameplay ABI.

The existing realization clears count before validation, validates capacity and
finite parameters before dispatch, and publishes count only after success. The
private evaluator checks the universal 60-second head-age bound, without
reconstructing birth membership from separately rounded lifetime/duration inputs.
The double-precision window remains the membership authority. The
camera composition owner restores targets on success and failure. Reuse existing
resource retirement, per-view evaluation and conservative whole-life bounds.

Explicit source/value/typed-kit profiles must distinguish GPU trails from the
existing CPU trail profile. Keep independent CPU/GPU activation with shared trail
settings, retaining inactive geometry, program, opacity, collision and stop IDs.
Support double-sided Engine/Quad with built-in opaque/masked Materials or explicit
weighted Blend transport. Lifetime opacity requires the weighted GPU profile.
Complete-view weighted refusal/omission remains ADR068's responsibility. Device
emitter sorting is not a replacement for per-particle sorted transparency.

Source14 adds `gpu_trail_enabled`, independently of CPU `trail_enabled` and
sharing `trail`. Values23/`gpu_trail1`,24/`gpu_weighted_trail1` and25/
`gpu_weighted_trail_opacity1` require effect3/GPU1 and typed
`runtime_particle_gpu_trail:1`;24/25 add weighted1 and25 adds GPU opacity1.
Older source/value meanings and recipe/envelope3 remain unchanged. CPU and GPU
activation, source revisions, Undo/Redo and publication use the existing owner.

## Required integration gate

Compare actual device rows with the independent CPU motion, lifetime appearance
and joined geometry owners, including all spawn shapes, duration/segment limits,
full capacity, shrinking tails, float boundaries, repeated queries and recovery.
Compare actual draws with ordinary baked triangles, without deriving the reference
geometry from device output. Preserve previous shader bytes and profile behavior.

Native controls, automation/MCP discovery, validation, permissions, exact revisions,
Undo/Redo, source publication/reopen, typed-kit refusal and source-hidden exported
World/Game must agree. Include an editable Gallery case and accurate manual/contents
and technical documentation. Local software rendering, native Windows execution,
visual review and physical acceptance remain distinct evidence scopes.
