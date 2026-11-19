# ADR062 — Explicit sorted-alpha CPU particles

Status: implemented with passing local source, native control, both MCP protocols,
publication/relocation, software-graphics and scoped sanitizer checks. Native
Windows acceptance remains pending. This deliberately extends the opaque/masked
admission limits of ADR060/061 through a separate value profile.

## Ownership and ordering

Use the existing CPU sampler, per-particle Mesh pose and shared render sort key.
CPU sorted-alpha particles enter the same back-to-front queue as ordinary Blend
Meshes. They retain individual draws: opaque batching cannot reorder transparent
parts. Material owns alpha, textures and blending; the ParticleEmitter component
and RGB lifetime curves retain their meanings. No second queue, particle registry,
simulation clock, GPU sorting or readback is introduced.

The new profile admits built-in Blend Materials only with depth testing and no
depth writes. It retains the existing one static triangle part/LOD/slot limits.
Transmission, custom surfaces, camera textures, skins and morphs remain refused.
Existing complete-view weighted admission falls back to sorted transparency when
it observes transparent particles. Sorting is per particle Mesh part, not per
triangle or pixel; intersecting geometry retains ordinary sorted-alpha limits.

GPU3 and graph4 remain opaque/masked. Their shared seven-row output has no
camera-sorted particle permutation. Admitting Blend into those profiles would
silently substitute emitter ordering for per-particle ordering, so it remains an
explicit refusal. CPU5 uses the established CPU random sequence and double clock;
GPU float-stop restrictions do not apply to it.

CPU5 uses ordinary Material RGB modulation. The existing instance tint row keeps
its seven-row layout: flag0 means no tint, flag1 preserves legacy blockout tint
and its Unlit directional light, and flag2 modulates RGB without that light.
Older CPU/GPU profiles continue emitting flag1. One shared instance projection
retains flag2 for single, batched and fallback CPU5 submissions; built-in Unlit
therefore retains its Material color instead of inheriting blockout shading.

## Source, cooked data and kits

Values5 use the explicit `cpu_alpha1` profile. Source5 retains optional inactive
graph settings across CPU/GPU/Graph/CPU sorted-alpha changes. The native Evaluation
control and `asset.document.particle_evaluation` operation use the same source
validation, revision, history and publication owners; MCP exposes `cpu_alpha`.

Keep recipe3/envelope3 with its explicit `value_version`, following the existing
graph4 extension. Older values1–4, recipes, source/stop IDs and CPU random patterns
keep their interpretations and bytes. New immutable source5 input produces a new
cooked selection through the existing importer; it does not overwrite old cooks.
An explicit `runtime_particle_alpha:1` kit capability is required for values5.
CPU5 does not require GPU or graph capability. Older kits reject the new values.
The ParticleEmitter layout, gameplay C ABI and asset/importer identities do not
change; no source or identity migration is required.

Per-particle opacity uses the separate CPU6 profile in
[ADR063](063-particle-opacity.md). Transparent GPU particles, billboards, collision
and trails remain outside this profile. Verification must cover actual cross-emitter
and ordinary-Mesh ordering, weighted fallback, source-hidden relocated content,
native controls, both MCP envelopes and exact kit refusal before completion.
