# Layered heightfield terrain

Terrain is project-authored source that publishes an ordinary root Mesh asset.
The source editor, AssetCatalog, immutable cooked cache, resource pool,
MeshRenderer, picking, static collision, navigation and export retain their
existing ownership. See [ADR020](decisions/020-heightfield-terrain.md).

## Source, identity and limits

`.terrain.json` uses `forge.heightfield`, version1, profile `heightfield_mesh1`.
Values are asset-local meters, with +Y up. A common tile size and samples-per-side
resolution apply to sparse integer tile coordinates. Each tile and height layer
has an independently allocated UUID, scoped to the root Mesh AssetId. Moving,
resampling and editing preserve these IDs. Duplicating a source creates a new
root AssetId; the copied local IDs are independent within that root.

The profile admits eight tiles, three layers, sides2–129, tile sizes1–1000m,
coordinates±16, raw/composed heights±10000m and source8MiB. Parsing admits one
million JSON events and depth16. Layer strength and masks lie in [0,1]. Enabled
layers compose as `sum(raw_height * mask * strength)`. Every shared sample must
agree in each raw layer and mask, including disabled layers and diagonal corners.
Root extension metadata survives edits without becoming runtime data.

New tiles inherit shared sample values; their interior starts flat with full
masks. New layers start at zero height with full masks. Resolution changes
bilinearly resample raw layer heights and masks, preserving exact endpoints and
seams. Invalid edits reject the entire candidate before history changes.

## Geometry and consumers

Cooked vertices are float, while ordinary scene placement retains double world
transforms. Each tile supplies one triangle part; the Mesh has one LOD and one
stable `default` material binding. Winding faces +Y. Shared-coordinate derivative
normals, tangents and continuous tile-unit UVs agree across borders. Height queries
and tile selection follow actual packed float coordinates and triangle heights;
bilinear interpolation is not substituted for the rendered triangle surface.

The trusted CPU importer `forge.terrain.heightfield` publishes `mesh.bin` through
the existing candidate/cache/catalog transaction. Root admission verifies the
exact importer, recipe, target, generation, hashes, metadata and regular topology.
It does not relax Model or editable-Mesh admission. Failed cooks and invalid
automatic updates retain the selected working artifact. Content discovery,
source-document routing, copy, reference inspection and the real asset CLI use
the same existing owners. Automatic updates defer while the source editor has an
unsaved draft or active stroke, and publication receipts suppress self-write loops.

Ordinary MeshRenderer and Inspector material overrides place and shade the result.
Existing Mesh-derived Collision sources provide triangulated **static** collision.
Collision is a separate asset and must be rebuilt after changing its Mesh input;
terrain painting does not silently rebuild a collider. Static Navigation Surface
geometry uses the same placed triangles. Existing navigation limits still apply:
16384 total triangles and its smaller Mesh admission budget can reject terrain
that is valid for rendering. No optimized native heightfield collider is claimed.

## Source editor and performance

The Terrain document offers tile/layer controls, resolution changes, layer
enable/strength, Raise / lower, Flatten layer, Smooth layer and Paint layer mask.
Flatten edits the selected layer's raw height, not the final composed surface.
Brushes use quadratic falloff, pressure, spacing and optional X/Z mirrors. The
maximum influence across recorded stamps makes results independent of stationary
frame count. Smooth reads a snapshot of cardinal neighbors.

Mouse release commits one SourceDocument history entry. Escape, lost focus,
collapsed/closed view, Play lock or source revision changes cancel the uncommitted
stroke. Source Undo/Redo is separate from Scene Undo. Save / Publish saves the
source and selects a new cook; assigning to the selected Mesh Renderer is an
explicit Scene edit with its own Undo.

An isolated host renders draft geometry through the shared native mesh renderer.
It cannot override ordinary game hosts, write preview sources, change the Scene,
publish a temporary catalog or tick gameplay. Revision caches retain derived
rasters/meshes; stationary cursor frames do not recompute a stroke. Middle mouse
orbits, wheel zooms and Frame terrain fits the bounds. Dense profiles still do
more CPU work on edits; admission limits are not a frame-rate guarantee.

Runtime packages contain selected Mesh/collision cooks and scene data, with no
terrain grids, source editor, compiler or per-sample Scene entities. No new native
gameplay component or SDK ABI is introduced. This profile does not provide
streaming, volumes, overhangs, holes, water, splines or foliage.

See [the user guide](../manual/editor/terrain.md) and Gallery55. Local normal and
sanitized tests cover source/history/cook/watch/resources, navigation triangles,
real collision and relocated source-offline export. Actual ImGui input checks
cover100/150/200% full/compact layouts; native Vulkan checks exercise pixels,
picking, resize, culling and private drafts. Windows physical acceptance is pending.
