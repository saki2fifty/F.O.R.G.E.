# ADR065 — GPU camera-plane particles

Status: accepted; implementation and coherent local integration gate passed.
Native Windows graph9 cook/export/Game passed at b82d90ca. Physical acceptance remains separate.
This extends ADR060/064 through explicit new profiles. It does not change the
meaning of GPU3/graph4 or CPU camera-plane7.

## Device and rendering ownership

Retain ParticleRuntime's fixed clock and the existing renderer-owned per-emitter
GPU realization. The original analytic or authored evaluator produces its seven
static instance rows with the unchanged47-row input layout. An optional second
compute dispatch projects those same output rows in place. Existing immutable
graph programs and their input reflection remain valid; no new RNG draws, CPU
particle transform array, readback, simulation registry or clock is introduced.

Separate64-byte view constants originally carry the admitted camera frame and bounded count.
[ADR069](069-gpu-particle-opacity.md) deliberately extends this private upload for
lifetime appearance; the immutable47-row program input and7-row output remain unchanged.
A same-UAV transition orders the two dispatches. Both use the shared native shader
and pipeline cache and the same bounded vertex/UAV buffer. The realization clears
its published count before preparation and publishes only after both dispatches
succeed. The existing camera composition owner restores targets on success or
failure. Each camera evaluates its own rows before drawing; another view does
not mutate cached source poses or simulation state.

The projection preserves device-relative centers and RGB. Width and height are
the lengths of the evaluator's X/Y world columns. The aligned columns use camera
right, camera up and minus camera forward, with Z scale max(width,height), matching
the CPU camera-plane geometry. Scaled column norms avoid intermediate square
overflow; the existing static instance basis uses max-absolute normalization.
Tint flag2 selects ordinary Material RGB modulation, without legacy blockout
lighting. The seven-row packet and flags of earlier profiles remain unchanged.

The existing per-view CPU placeholder supplies matching raster parity and static
Mesh bounds. Whole-lifetime culling uses the shared isotropic camera-plane envelope
and existing GPU float padding; authored programs retain their declared position
and maximum-size bounds. The admitted geometry is Engine/Quad. Capacity, scene,
per-draw and resource lifetime limits retain their existing owners.

## Source, history and compatibility

Source8 adds a separate `gpu_billboard_enabled` boolean. Existing CPU
`billboard_enabled`, opacity settings, original Mesh, program, extensions and local
stop IDs remain retained. Reading old source7 never activates GPU alignment.
An explicit GPU operation promotes a validated candidate without reinterpreting
older source or overwriting immutable cooks. Disabling an absent GPU setting does
not promote a source. CPU controls never downgrade source8.

The source projects values8/`gpu_billboard1` for the built-in evaluator and
values9/`gpu_graph_billboard1` for an active authored program. Enabling GPU alignment
preserves an active Graph; otherwise it selects built-in GPU evaluation. Disabling
it restores GPU3 or graph4 with the original Mesh. Backend changes preserve the
independent inactive CPU/GPU selections. Inactive CPU opacity keeps CPU precision
and does not acquire GPU stop restrictions.

`asset.document.particle_billboards` keeps its existing CPU behavior when
`backend` is omitted or `cpu`. Explicit `backend: gpu` calls the GPU source operation.
The native control uses the operation for its selected evaluation backend. Source
history, revisions, permissions, publication and MCP discovery remain shared.

Recipe3/envelope3 carry the new explicit values. Values8/9 require effect3, GPU1
and `runtime_particle_gpu_billboard:1`; values9 also require graph1. They do not
require the CPU alpha, opacity or billboard capabilities. Older, malformed or
incompatible runtime kits reject before distribution replacement. Importer/asset
identities, previous source/cook bytes, ParticleEmitter layout and gameplay C ABI
remain unchanged. No migration is required.

## Scope and acceptance

Only admitted built-in opaque/masked Materials are supported. GPU Blend and
per-particle opacity remain outside these profiles. Emitter ordering is not a
substitute for the cross-emitter/ordinary-Mesh particle ordering required by
ADR062. Per-eye look-at, axis/velocity alignment, spin, collision and trails remain
outside this extension.

Acceptance requires independent packet/projection checks, actual device readback,
original evaluator/graph compatibility, per-view single-sided textured rendering,
bounds and recovery, source/history/native/MCP controls, exact kit admission and
source-hidden Gallery/Game export. Software, native Windows and physical-device
acceptance are distinct and must be recorded within their actual tested scopes.
