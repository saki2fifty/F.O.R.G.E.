# ADR064 — CPU camera-plane particle quads

Status: implemented with passing local source/runtime, native controls, both MCP
protocols, source-hidden publication/relocation/Game, software graphics and scoped
sanitizer checks. Native Windows and physical acceptance remain separate.

## Source and simulation ownership

Values7/`cpu_billboard1` extend the existing CPU sampler with camera-plane geometry.
Source7 retains the original Mesh, `billboard_enabled`, opacity settings and optional
graph program. `enable_billboards` and `asset.document.particle_billboards` share the
source/history owner with native controls. Enabling selects CPU sorted-alpha.
Disabling projects the retained Mesh through values6 when opacity is enabled or
values5 otherwise. Other backends retain but do not apply camera alignment.

The active projection uses Engine/Quad; source Mesh selection remains intact.
Values7 carry opacity fields, with an identity projection when opacity is disabled.
No additional RNG draws, entities, clocks, per-birth Materials or simulation manager
are introduced. Values1–6 and old source/stop identities retain their meanings.

## Per-view geometry and bounds

Simulation produces the ordinary full-affine world center and scale. For each
camera, detached matrices align X/Y with its admitted right/up vectors, preserving
the lengths of the original X/Y columns. The +Z lighting normal faces opposite the
camera forward vector. Alignment is parallel to the camera plane, including for
perspective cameras; it does not independently point each quad at the eye.
Emitter rotation, reflection and shear continue to position trajectories, while
alignment removes their orientation and mirroring from the quad plane.

A reserved view-local vector owns aligned matrices; draw objects borrow stable
addresses. Simulation and cached source poses remain immutable across views. Its
per-birth matrix cost counts toward the existing8MiB CPU payload budget. Instance
and part bounds, depth sorting, parity and actual draw packets all use that same
matrix. The existing bounds owner admits a static-world override and rejects
skinning/wind overrides. Other consumers keep their original behavior.

Whole-lifetime bounds transform the motion envelope, then expand isotropically by
half the maximum particle size times the hypotenuse of emitter X/Y column lengths.
This covers arbitrary camera directions and emitters with zero Z scale; rotating a
flat local AABB would not provide that guarantee. Bounds round outward.

## Materials and compatibility

Built-in opaque/masked Materials are permitted with identity opacity and retain
ordinary opaque instancing. Nonidentity opacity requires Blend, depth testing and
no depth writes. Blend uses the existing per-birth sorted queue. The seven-row
packet and tint flag3 remain unchanged. Single-sided winding and texture orientation
follow admitted camera bases and lens flips rather than requiring two-sided output.

Recipe3/envelope3 add explicit values7 admission, requiring effect3, alpha1,
opacity1 and `runtime_particle_billboard:1` in the runtime kit. No gameplay C ABI,
ParticleEmitter layout, dependency revision, importer identity or old cook changes.
No migration is required. GPU billboards, per-eye look-at, axis/velocity alignment,
spin, collision and trails remain outside this profile.
