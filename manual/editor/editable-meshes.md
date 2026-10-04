# Editable Meshes and UVs

Use an editable Mesh when you want to shape a simple static object inside FORGE. It is a separate source asset that can be reused by more than one scene entity. Imported glTF meshes remain part of their Model source and open read-only.

## Create and place a Mesh

1. Stop Play. Open **Content → Create / Register → New editable Mesh...**. Enter a project-relative filename such as `Assets/Crate.mesh.json` and choose **Create**. FORGE starts with a cube.
2. The **Editable Mesh** document opens with **Model** and **UV** tabs. Choose **Save & Publish** to make the initial Mesh available in Content.
3. Drag the published Mesh from Content into the Scene. It creates an entity with **Mesh Renderer**. Save the Scene separately.
4. Select that entity and use **Inspector → Mesh Renderer → Materials → Surface** to assign a project Material. Create or import a Texture, then assign it to the Material's color texture slot if you want to see the UV placement. See [Materials](materials.md) and [Textures](textures.md).

## Shape the Mesh

Choose **Vertex**, **Edge** or **Face** in the Model tab. Click an element in the list or wireframe; Ctrl-click adds or removes vertices. Edge and face modes select one element. Enter **Move**, **Rotate (deg)** and **Scale** values, turn on **Preview transform** if useful, then **Apply transform**. **Cancel transform** clears those pending values. The operation is one source Undo step.

Select a face, enter a positive **Distance**, and use **Preview extrusion** or **Extrude selected face**. The original face becomes a new cap with side walls; the cap remains selected. **Cancel extrusion** drops the preview. Invalid edits, such as a nonplanar face or zero scale, report an error and leave the source unchanged.

## Place the UVs

The UV tab shows the selected face's independent corner coordinates. The outlined 0–1 tile has (0,0) at the top left, matching FORGE's texture convention. Click corners in the list or diagram; Ctrl-click adds corners. With no corners selected, a transform applies to all corners of that face. Enter **Move UV**, **Rotate UV (deg)** and **Scale UV**, preview if needed, then **Apply UV transform**. **Planar project face** projects the face along its dominant normal axis. **Cancel UV preview** clears pending values. Separate face corners can have different UVs, so seams remain independent.

Choose **Save & Publish** after shape or UV edits. An accepted publication updates the shared Mesh resource used by scene entities; an invalid cook keeps the previous published revision. The scene's position, scale and material assignments are separate from Mesh source history. Stop Play before editing. Exported games contain the selected cooked Mesh and UVs, without `.mesh.json` or modeling tools.

Source Undo/Redo belongs to the active Editable Mesh document; Scene Undo/Redo remains separate. Close prompts offer **Save**, **Discard** and **Keep editing** when source changes have not been published. Reopen the registered Mesh from Content to continue editing.

## Current scope

The first modeling workflow supports bounded, planar convex polygons with explicit vertex and face IDs, face extrusion, selection transforms and face-corner UV editing. It does not edit imported Model meshes, skin/morph targets, LODs, sculpting or painting. Complex or nonmanifold geometry is rejected rather than silently changed.
