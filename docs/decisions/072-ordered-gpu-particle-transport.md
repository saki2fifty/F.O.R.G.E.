# ADR072 — Explicit ordered GPU particle transport

Status: accepted; bounded renderer admission, explicit source profiles and shared
native/automation/MCP authoring and package integration are implemented. The
coherent local integration checks pass. Native Windows execution and
physical acceptance remain separate.

## Existing ownership and ordering

Extend the existing renderer through an explicit GPU alpha transport. Preserve
ADR060/062/068/069/070/071 profiles and their meanings. MeshSceneRenderer remains
the owner of queue admission, resources, Material specialization, culling, camera
targets, diagnostics and draw submission. Do not add a particle registry, second
simulation clock, CPU particle readback or emitter-only sorting substitute.

Use a bounded derived view resource to gather existing device packets, generate
depth/index keys, sort indices and gather the final packet permutation. Keep
births, motion, appearance, float semantics and the seven-row packet unchanged.
Sort back to front by the transformed Mesh-part bounds center or explicit ribbon
segment midpoint. Stable ties follow the existing Scene key order: the caller
submits canonical emitter identities, followed by their birth and segment order.
Numeric signed zero compares equal. Padding remains finite degenerate geometry.

The resource reserves at most8192packets and64sources, sharing the existing Scene
limits; draws retain64-instance slices. Compare keys rather than moving seven-row
payloads at every sort stage. Explicit UAV barriers order all compare/gather
dispatches and reuse. Count clears before validation and publishes only after
successful final gathering. The camera owner restores targets on both paths.
Bindings retain derived buffers, not source assets or another camera's geometry.

## Complete-view admission

Initially admit transparent GPU particles sharing the same complete physical draw
bundle, part, raster parity, layer mask, projected decal selection and object
parameters. Exact resources include their immutable publications and generations;
matching logical Material names alone are insufficient. Preserve per-particle
RGB/opacity, ordinary Material alpha/textures and matching lighting/fog selection.

Require sorted Scene transport and admit the complete visible alpha group before
drawing it. Opaque/masked geometry remains in the ordinary path and supplies depth
occlusion. An incompatible transparent Material, geometry/resource selection,
ordinary transparent Mesh, CPU particle, Sprite/Tile layer, transmission or budget
failure refuses the complete ordered GPU group for that view with diagnostics.
Other existing paths preserve their behavior. Never partially sort the admitted
emitters, reorder only their proxies or silently substitute weighted blending.

This is object/segment depth sorting, not per-triangle or per-fragment ordering.
Intersecting geometry retains ordinary sorted-alpha limits. It does not imply
unrestricted multi-Material or mixed-source transparency. Existing explicit
weighted transparency remains a separate approximate transport.

## Integration gate

Private device proofs cover stable CPU-reference ordering, odd/full counts, ties,
signed zero, padding, exact packet bytes, multiple input buffers, real analytic
Mesh/camera-plane/ribbon producers, tails, reflection and large camera origins.
Independent ordered ordinary triangles establish texture alpha, depth occlusion,
reversed/flipped views and multiple draw slices. These proofs do not substitute
for the production renderer's admission, lighting, resource changes or recovery.

Before completion, add explicit source/value/typed-kit profiles without reinterpreting
old cooks or identities. Reconcile native controls, automation/MCP discovery,
permissions, revisions, history, publication/reopen and export in the same batch.
Test production Scene composition, incompatible-view omission/recovery, shared
Material lighting, multiple cameras and lifecycle failures. Include an editable
Gallery closure, source-hidden World/production Game, authentic useful capture
and current manual/contents. Native Windows and physical review remain distinct.
