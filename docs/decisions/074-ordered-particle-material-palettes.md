# ADR074 — Bounded Materials in ordered GPU particle views

Status: accepted for implementation; integration is in progress. Local device
proofs, native platform execution and physical acceptance are separate evidence.
This record does not describe an available authoring workflow.

## Existing owners and compatibility

Extend ADR072 through explicit new source/value profiles. Preserve every earlier
profile, recipe, publication identity, component layout and module ABI. Existing
single-bundle sorted transport retains its original admission rules. Append a
typed runtime-kit capability for the new transport; do not silently admit it with
an older kit.

MeshSceneRenderer owns complete-view admission, source leases, budgets, camera
targets, diagnostics and draw submission. MeshDrawBundle prepares a detached draw
from its existing physical Mesh/Material resources. Object values use the existing
Material parameter projection. MaterialShader owns packing, UV transforms and
texture sampling. MeshDrawShader composes scoped canonical helpers around one
existing geometry/lighting body. Do not duplicate the physical lighting model or
add an asset registry, particle clock or rendering manager.

## Bounded admission

The entire visible alpha contribution must select the new transport. Admit at
most four distinct physical Material/value selections, eight unique sampler
states and 32 texture bindings, over the existing 64-source/8192-packet limits.
The renderer deduplicates complete physical bundle and projected-value selections;
logical Material names alone are insufficient.

Require the same physical static triangle Mesh publication, LOD/part, raster
parity, layer and shadow policy. Built-in Materials must share their model,
texture-helper interface and generated geometry/lighting body. Compare that body
with its canonical Material helper component removed instead of maintaining a
second feature-compatibility table. Values, texture resources, UV transforms and
sampler states can differ within those constraints.

Initial palette admission excludes custom Material surfaces, transmission,
projected decals/light images, skinning, morphs, wind and opaque temporal/MRT
specializations. Analytic Mesh, camera-plane and ribbon producers retain their
existing geometry/appearance ownership. An incompatible alpha source, resource,
profile mixture or budget refuses the complete group for that view. Do not draw a
partial group or substitute emitter-only ordering or approximate blending.

## Device transport and lifetime

ParticleGpuOrder carries an opaque per-source uint through its existing stable
keys and final gather into a separate device index buffer. Tags never decide
depth or ties. Preserve all seven instance rows and the existing 64-instance draw
slices. Use a separate integer instance input and a noninterpolated pixel value;
out-of-palette indices discard rather than selecting another Material. No CPU
particle readback is introduced.

Keep one bounded derived palette candidate with the renderer. Its key includes
the exact complete source bundles, projected values, part and lighting layout.
Retain those bundles until native draw bindings are destroyed. Charge retained
values and the additional draw through the existing scene budgets, including
replacement peaks. Validate native leases on submission. Prepare and adopt a
complete candidate atomically; a rejected view clears its derived candidate.
Scene/resource transitions and multiple cameras must not reuse stale selections.
Restore camera attachments after both successful and failed preparation/ordering.

## Integration gate

Real-device proofs compare shared canonical shaders with independently ordered
ordinary Material draws. Cover distinct factors/textures/UV/samplers, all admitted
built-in models, reversed views, multiple slices, invalid index/range refusal,
lighting/fog and detached value projection with recovery. These proofs do not
establish complete Scene admission or native/physical acceptance.

Before authoring availability, integrate explicit source/value/kit profiles,
native controls, automation/MCP discovery and history, publication/reopen, export,
an editable Gallery closure and authentic manual guidance. Prove the production
Scene path, complete-view refusal/recovery, resource replacement, budgets,
multiple cameras and source-hidden Game execution. Broader geometry/source mixes
and per-triangle/per-fragment transparency remain separate capabilities.
