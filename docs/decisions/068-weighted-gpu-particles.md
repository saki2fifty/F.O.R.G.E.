# ADR068 — Explicit weighted GPU particle transparency

Status: accepted; implementation and coherent local integration gate passed.
Native Windows graph execution and physical acceptance remain separate.

## Composition and admission

Extend ADR060/062/065 with explicit approximate weighted GPU profiles. Existing
GPU3/4/8/9 remain opaque/masked and CPU5/6/7/10/11 retain their sorted-alpha
transport. No existing source or immutable cook implicitly gains device Blend.

MeshSceneRenderer continues to own the complete-view weighted candidate, ordinary
part queue, target validation and resolve. New GPU particle parts use the same
built-in Material shading, weighted specialization, seven-row vertex input and
bounded instance slices as ordinary Mesh draws. They require a built-in Blend
Material with depth testing and no depth writes. They share the existing weighted
color/coverage approximation with all admitted transparent Meshes in the view.
This is not exact per-fragment layer ordering or refraction.

An emitter using the new profile requires the Scene's weighted transparency
setting and successful complete-view admission. A visible incompatible source,
transmission surface, target/resource/budget failure or disabled setting omits
these device-alpha emitters for that view with an entity-scoped diagnostic.
Remaining sources retain their established sorted fallback. Emitter-depth sorting
must never stand in for unavailable device particle ordering. No partial weighted
composition, CPU readback, second queue or independent simulation clock is added.

## Device rows and view ownership

The existing analytic or authored evaluator and its47-row input layout are
unchanged. Reuse the existing supplemental64-byte view projection and same-UAV
barrier. Its bounded mode either applies the existing camera-plane basis or keeps
the evaluator's Mesh transform; both select Material RGB modulation with tint
flag2. Existing opaque/masked Mesh flags are unchanged. Device counts clear before
preparation and publish only after all required dispatches succeed. The camera
owner restores its render targets on success or failure. Each view evaluates and
projects its own rows through the existing emitter realization; nothing reads
particle transforms back to the CPU.

## Source and package compatibility

Source11 adds retained `gpu_weighted_enabled`. The shared validated source
operation selects GPU or preserves an active Graph and independent GPU camera
alignment. It preserves original Mesh, program, CPU opacity, ribbons, plane
collision, extensions and local stop identities. Disabling an absent flag is a
no-op; changing CPU settings never downgrades a source. Inactive opacity retains
CPU precision and does not acquire GPU stop restrictions.

Explicit values12/`gpu_weighted1`,13/`gpu_graph_weighted1`,
14/`gpu_weighted_billboard1`,15/`gpu_graph_weighted_billboard1` select the supported
combinations. These profiles apply Material alpha and source textures; GPU lifetime
opacity, collision and ribbons remain unsupported. Recipe3/envelope3 and importer
identity remain stable. Packages require typed `runtime_particle_gpu_weighted1`
and GPU1, plus graph1 and/or GPU billboard1 where used. Older kits reject the new
profiles; old profiles retain their previous requirements. No component or gameplay
C ABI changes.

Native controls, automation and MCP call the same source operation and existing
document/history/publication owners, preserving permissions, revision checks,
Undo/Redo, no-op behavior and Save/reopen semantics.
