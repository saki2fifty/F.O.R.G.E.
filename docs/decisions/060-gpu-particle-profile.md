# ADR060 — GPU mesh-particle profile

Status: accepted. The bounded source/authoring/runtime/render/export workflow is
implemented and locally validated. Windows and physical-device acceptance are
recorded separately.

## Ownership

Keep ParticleEffect as one logical asset family and ParticleRuntime as the
fixed-clock emission owner. GPU evaluation is an explicit source profile, not a
second World, particle registry, clock or persisted set of particle entities.
MeshSceneRenderer retains effect/Mesh/Material selections and renderer-owned
compute buffers. Presentation retains shared native shader/pipeline caching.
Existing source history, publication, recovery and automation ownership remain.

Source3/profile `gpu_mesh1` selects device evaluation. Source1/2 retain their CPU
meaning and original standard-library random sequence. CPU array sampling rejects
GPU values rather than silently returning another profile's visual pattern.
The GPU profile specifies FNV-1a over little-endian seed32/ordinal64 bytes and the
48271/2147483647 linear congruential sequence. Its bounded32bit modular step is
checked against an independent64bit reference. It generates birth inputs,
analytic motion, lifetime appearance and instance transforms on the device.

CPU double precision admits the bounded active ordinal window and supplies short
relative ages. It never sends billion-second float timestamps to the shader.
GPU float arithmetic is a visual evaluation contract, without cross-backend
lockstep or physical-performance guarantees. Lifetime stop times must remain
distinct at float precision. Existing emitter/capacity, Mesh/Material and static
opaque/masked limits remain; general VFX graphs are outside this profile.

## Source settings and cooked compatibility

Keep the existing importer identifier: it is persisted in source settings and
sidecars. Selecting an evaluation backend must not replace that identity or
require source/settings migration. Use the neutral `forge.particles.mesh.v3`
recipe and a version3 cooked envelope for newly published CPU and GPU effects.
The envelope retains the root AssetId and contains explicit value profile2 or3.
Legacy CPU source1 normalizes to CPU values2 on cooking, without rewriting source.
Source/stop IDs, opaque source extensions and ordinary Undo/Redo stay unchanged.

Existing immutable recipe1/raw values1 and recipe2/raw values2 remain readable
under their exact original provenance. New recipe3 requires envelope3, explicit
value-version metadata and matching typed dependency/source evidence. Reject mixed
recipe/envelope/value versions, unexpected cooked extensions and unknown wrapper
fields. Runtime-kit/export requirements must advertise the new cooked boundary
and GPU support before the complete workflow is accepted.

## Rendering and publication

The GPU emits the existing base static seven-row instance layout into a bounded
vertex/UAV buffer. The shared MeshDraw/Bundle accepts an exclusive, validated
buffer range and retains its existing64-instance draw batch bound. Sprite, wind,
temporal and indirect inputs do not share this base packet. The renderer owns
camera target restoration after compute, immutable selection/generation checks,
per-view submission and retirement. A failed submission must not publish stale
instances as a new World/time observation.

Native controls and automation/MCP must use the same explicit source operation,
validation, revisions, history and publication. A complete feature requires an
editable example, actual preview/Play and source-hidden export, negative lifecycle
proof, compatible older CPU values and accurate user instructions/screenshots.
