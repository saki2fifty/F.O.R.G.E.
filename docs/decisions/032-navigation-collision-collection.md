# ADR032 — Profile-scoped static collision collection

Date:2026-11-02. Implemented in local source through the existing authoring,
runtime and export owners. Native Windows/full-editor and physical acceptance
remain separate from portable CPU and software-renderer validation.

## Source ownership and selection

Refine ADR026's explicit-only source limit through the existing NavigationSettings,
Scene capture, candidate publication, catalog, resources and runtime validity
owners. Append `collect_static_colliders`, default false, and `collision_layers`,
default all32physics layers. The default settings representation and positive
existing navigation outputs remain unchanged. No authored entity, component,
asset identity or immutable artifact is rewritten by collection.

When enabled, collect enabled Static nonsensor bodies in the selected plane whose
physics layer matches the mask. Ignore their Dynamic/Kinematic, disabled and sensor
counterparts. An explicit navigation marker in that plane takes precedence,
including a disabled marker as an opt-out. Existing active-state, Scene-instance,
prefab, resolved transform and followed-static-ancestry rules still apply. A
selected malformed or unsupported body fails admission rather than substituting
its artwork. The existing512source bound includes collected entities.

One copied selection predicate serves effective Scene JSON and native extraction.
The extracted source descriptors are ephemeral values; they are not a second
authoring database or physics world. No state is added to the public Agent or its
single movement writer.

## Physical geometry and resources

In XY, retain explicit walkable regions and collect solid collision footprints as
obstacles. Reuse the existing physical Box/Circle/Capsule and selected Tile Map
contour adapters, including their rounding, islands, holes and variant selection.
An explicit region can still deliberately use a collider as walkable geometry.

In Spatial3D, collect the actual inline or selected Collision asset shape. Reuse
the existing bounded native shape triangle adapter and Collision resource
provider; no collision implementation is duplicated in navigation. Baking rejects
truncated geometry. Shape preparation requires no running physics world. The build
owns admitted immutable leases and records selected Collision revisions/dependency
edges alongside the existing Mesh/Map provenance. It does not infer geometry from
render primitives when a physical source was selected.

Source validity samples copied configuration, transforms and selected resource
readiness/revisions. It never triangulates physical shapes, reads mutable native
handles or hashes decoded Map cells per tick. Selected profile views reuse the
existing bounded navigation resource cache and writable synchronization boundary.
Adding/removing/disabling a collected source, changing its shape/layer/transform
or replacing a selected Collision/Map revision invalidates the affected profile.

## Compatibility and integration

Opt-in cooked data must identify the collection profile explicitly so an older
runtime cannot silently follow it using explicit-only source validation. Extend
the established envelope/provenance admissions deliberately while retaining all
older positive formats. Static meshes, dynamic obstacle layers and authored links
compose through the same existing build and Query paths. No automatic runtime
geometry rebake, physics-character routing or moving-platform attachment is added.

Editor controls and build feedback expose the same selection fields as automation
and MCP. Collection does not create Scene Undo entries because it changes only
build settings and publishes an ordinary Nav revision; explicit component edits
retain normal Scene history. A playable Gallery scene, matching source/native /
runtime/SDK/export checks and current-use documentation are required before this
workflow is reported as implemented. Physical Windows acceptance is separate.
