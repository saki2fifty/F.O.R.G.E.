# Editable Meshes and UVs

Use an editable Mesh when you want to shape a simple static object inside FORGE. It is a separate source asset that can be reused by more than one scene entity. Imported glTF meshes remain part of their Model source and open read-only.

## Create and place a Mesh

1. Stop Play. Open **Content → Create / Register → New editable Mesh...**. Enter a project-relative filename such as `Assets/Crate.mesh.json` choose **Cube** or **Sculpt sphere**, then choose **Create**.
2. The **Editable Mesh** document opens with **Model** and **UV** tabs. Choose **Save & Publish** to make the initial Mesh available in Content.
3. Drag the published Mesh from Content into the Scene. It creates an entity with **Mesh Renderer**. Save the Scene separately.
4. Select that entity and use **Inspector → Mesh Renderer → Materials → Surface** to assign a project Material. Create or import a Texture, then assign it to the Material's color texture slot if you want to see the UV placement. See [Materials](materials.md) and [Textures](textures.md).

## Shape the Mesh

Choose **Vertex**, **Edge** or **Face** in the Model tab. Click an element in the list or wireframe; Ctrl-click adds or removes vertices. Edge and face modes select one element. Enter **Move**, **Rotate (deg)** and **Scale** values, turn on **Preview transform** if useful, then **Apply transform**. **Cancel transform** clears those pending values. The operation is one source Undo step.

Select a face, enter a positive **Inset distance**, and use **Preview inset** or **Inset selected face** to create a border. The inner face stays selected. **Cancel inset** drops the preview. The distance is measured inward from each edge in the Mesh’s own coordinates; an inset that collapses the face is rejected.

Enter an **Extrude distance** and use **Preview extrusion** or **Extrude selected face**. A positive distance raises the face; a negative distance creates a recess. The original face becomes a new cap with side walls; the cap remains selected. **Cancel extrusion** drops the preview. Press Esc while the Model tab has focus to cancel a preview. Inset, extrusion and transform previews are exclusive. Invalid edits, such as a nonplanar face or zero scale, report an error and leave the source unchanged.

## Sculpt and paint

Create a **Sculpt sphere** for a small smooth Mesh, or open any editable Mesh. In **Model → Tool**, choose **Sculpt**, adjust **Brush radius** and **Brush strength**, then drag across the preview. Positive strength raises vertices along their surface normals; negative strength pushes inward. Release the mouse to apply one Undo step. Press Esc during a drag to cancel. The preview updates after release.

Choose **Paint** to color Mesh vertices. Pick **Paint color**, **Paint opacity** and **Brush radius**, then drag across the preview. Save & Publish to see the color in Scene, Play and exported games. This colors existing vertices; it does not edit texture image pixels. A low-poly Mesh gives broad color patches. Paint and sculpt both preserve the existing source and cooked Mesh workflow.

## Place the UVs

The UV tab shows the whole Mesh's texture coordinates. The outlined 0–1 tile has (0,0) at the top left. Gold corners and edges are selected; the remaining wireframe gives context. Choose **Pick UV → Corner**, **Face** or **Island**, then click the canvas. An island follows shared geometric edges with matching UV endpoints; it stops at authored seams. **Select face** and **Select island** use the active face. Ctrl-click toggles individual corners or adds faces/islands. Click empty canvas to clear the selection.

Drag a selected corner, face or island to move its UVs. The preview leaves the source unchanged until release, then creates one Undo step. Press Esc during a drag to cancel. Use the mouse wheel to zoom around the pointer and middle-drag to pan. **Frame selection** fits the selected region in the view; **Frame tile** restores the 0–1 tile. These navigation actions do not change the Mesh.

Enter **Move UV**, **Rotate UV (deg)** and two **Scale UV** values to transform the selection around its shared center. Negative scale mirrors an axis. **Preview UV** shows the candidate; **Apply UV transform** accepts it, and **Cancel UV preview** clears it. Finish a numeric preview before dragging. **Mirror U** / **Mirror V** flip the selection. **Fit selected UVs** centers the selection in the tile with the chosen **Tile padding**, preserving its proportions. This fits one selected region; it does not pack multiple islands into an atlas. Empty selections, zero scale, invalid coordinates and point-only fits report an error without changing the source.

**Planar project face** still projects just the active face along its dominant normal axis. Face corners remain independent, so editing one side of a seam does not weld it to the other side. Stop Play before editing; navigation remains available when the Mesh is read-only.

Choose **Save & Publish** after shape or UV edits. An accepted publication updates the shared Mesh resource used by scene entities; an invalid cook keeps the previous published revision. The scene's position, scale and material assignments are separate from Mesh source history. Stop Play before editing. Exported games contain the selected cooked Mesh and UVs, without `.mesh.json` or modeling tools.

Element IDs stay reserved after Undo, so restoring the original shape can still leave the source needing a save. New edits never reuse those discarded IDs.

Try **Feature Gallery → Scenes/14-uv-islands.scene.json** to compare connected, split and mirrored/fitted checker mappings. Open the three UV Mesh sources from Content to edit them.

Try **Feature Gallery → Scenes/12-inset-panels.scene.json** for a flat frame, a raised panel and a recessed panel. Open their editable Meshes from Content to inspect the borders and UVs; Play and export use the published geometry.

Source Undo/Redo belongs to the active Editable Mesh document; Scene Undo/Redo remains separate. Close prompts offer **Save**, **Discard** and **Keep editing** when source changes have not been published. Reopen the registered Mesh from Content to continue editing.

## Current scope

The first modeling workflow supports bounded, planar convex polygons with explicit vertex and face IDs, face inset and signed extrusion, selection transforms and face-corner UV editing. It does not edit imported Model meshes, skin/morph targets, LODs, automatic subdivision, dense sculpting or texture-pixel painting. Complex or nonmanifold geometry is rejected rather than silently changed.
