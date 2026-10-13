# Editable Meshes and UVs

Use an editable Mesh when you want to shape a simple static object inside FORGE. It is a separate source asset that can be reused by more than one scene entity. Imported glTF meshes remain part of their Model source and open read-only.

## Create and place a Mesh

1. Stop Play. Open **Content → Create / Register → New editable Mesh...**. Enter a project-relative filename such as `Assets/Crate.mesh.json` choose **Cube** or **Sculpt sphere**, then choose **Create**.
2. The **Editable Mesh** document opens with **Model** and **UV** tabs. Choose **Save & Publish** to make the initial Mesh available in Content.
3. Drag the published Mesh from Content into the Scene. It creates an entity with **Mesh Renderer**. Save the Scene separately.
4. Select that entity and use **Inspector → Mesh Renderer → Materials → Surface** to assign a project Material. Create or import a Texture, then assign it to the Material's color texture slot if you want to see the UV placement. See [Materials](materials.md) and [Textures](textures.md).

## Shape the Mesh

Choose **Vertex**, **Edge** or **Face** in the Model tab. Click an element in the list or wireframe; Ctrl-click adds or removes vertices or faces. Edge mode selects one edge. Enter **Move**, **Rotate (deg)** and **Scale** values, turn on **Preview transform** if useful, then **Apply transform**. **Cancel transform** clears those pending values. The operation is one source Undo step. Use **Triangulate Mesh** before deforming individual quad corners; faces still need to remain planar and convex.

In **Vertex** mode, use **Select all vertices**, **Clear vertex selection**, or **Invert vertex selection** to choose a group. Turn on **Box select vertices**, then drag a rectangle in the wireframe. Ctrl adds the rectangle's vertices to your current selection. This includes projected vertices on both sides of the Mesh. The selection count appears beside the controls. Turn box mode off to use ordinary clicks again.

Press Esc during a rectangle drag to cancel. Changing focus, the view, the source, Play state or publication also cancels an unfinished drag. Selecting vertices leaves the source and its Undo history untouched; **Apply transform** commits the edit. Closing or reopening a Mesh clears this temporary selection.

Select a face, enter a positive **Inset distance**, and use **Preview inset** or **Inset selected face** to create a border. The inner face stays selected. **Cancel inset** drops the preview. The distance is measured inward from each edge in the Mesh’s own coordinates; an inset that collapses the face is rejected.

Enter an **Extrude distance** and use **Preview extrusion** or **Extrude selected face**. A positive distance raises the face; a negative distance creates a recess. The original face becomes a new cap with side walls; the cap remains selected. **Cancel extrusion** drops the preview. Press Esc while the Model tab has focus to cancel a preview. Inset, extrusion and transform previews are exclusive. Invalid edits, such as a nonplanar face or zero scale, report an error and leave the source unchanged.

### Work with several Model faces

In **Face** mode, Ctrl-click the list or wireframe to toggle faces. **Select all
faces**, **Clear face selection**, **Invert face selection** and **Select connected
faces** help choose a surface. Connected selection follows geometric shared edges.
The selected count and active face appear beside the controls; gold faces show the
group. Selection changes leave the source and Undo history untouched.

**Apply transform** moves the group's shared vertices once. **Triangulate selected
faces** splits its polygons and selects all resulting triangles in one Undo step.
**Reverse selected faces** flips the selected surface; a partial selection that
would break shared-edge winding reports an error. Select a connected surface or
all faces when reversing a closed shape.

Inset, extrusion and **Triangulate face** use only the active selected face. Inset
keeps its inner face; extrusion selects the new cap. Group transforms still require
valid planar convex source faces, so triangulate before arbitrary corner deformation.
Empty selections disable edit controls. Source Undo/Redo keeps surviving selection
IDs and removes deleted ones; it does not replay selection history. If a topology
change removes every selected face, the editor selects the available active face.
Closing/reopening returns to one active face. **Save & Publish** updates Scene,
Play and export through the same shared Mesh resource.

### Delete and clean up topology

In **Face** mode, select the faces to remove and choose **Delete selected faces**.
At least one face must remain. The editor selects a surviving face when the whole
selection disappears. Deletion keeps unused vertices; choose **Remove unused
vertices** to remove those explicitly. This leaves used vertex data and IDs intact.
An already clean Mesh creates no extra Undo step.

To restore a quad after triangulating it, switch to **Edge**, select its diagonal
in the list or wireframe, then choose **Join triangles**. The triangles must form
a planar convex quad with matching UVs and other face/corner data. A seam or
conflict reports an error and leaves the Mesh unchanged. The resulting quad stays
selected in Face mode. Its lower face ID and shared diagonal are retained; smooth
shading can change because normals are recalculated from the joined faces.

Each action is one source Undo step. Finish previews and stop Play before editing;
**Save & Publish** updates Scene, Play and export. Try joining a diagonal on Gallery
**16-authored-triangulation**, or deleting faces on editable Meshes in Gallery 12/19.
Unused-vertex cleanup affects this Mesh source; it does not rewrite a separate rig
or deformation asset that refers to those vertices.

## Sculpt and paint

Create a **Sculpt sphere** for a small smooth Mesh, or open any editable Mesh. In **Model → Tool**, choose **Sculpt**, adjust **Brush radius** and **Brush strength**, then drag across the preview. Positive strength raises vertices along their surface normals; negative strength pushes inward. Release the mouse to apply one Undo step. Press Esc during a drag to cancel. The preview updates after release.

Choose **Paint** to color Mesh vertices. Pick **Paint color**, **Paint opacity** and **Brush radius**, then drag across the preview. Save & Publish to see the color in Scene, Play and exported games. This colors existing vertices; it does not edit texture image pixels. A low-poly Mesh gives broad color patches. Paint and sculpt both preserve the existing source and cooked Mesh workflow.

Enable **Mirror X**, **Mirror Y** or **Mirror Z** to repeat a brush across that Mesh's
local origin planes. You can combine axes. Reflected hover rings show the other
brush locations; overlapping locations keep the same strength. Brush settings are
held for the whole drag. Symmetry follows the existing shape and surface normals;
it does not make an asymmetric Mesh symmetric. Open **15-brush-symmetry** in the
Feature Gallery for a comparison of one-sided sculpt, mirrored sculpt and paint.

## Place the UVs

The UV tab shows the whole Mesh's texture coordinates. The outlined 0–1 tile has (0,0) at the top left. Gold corners and edges are selected; the remaining wireframe gives context. Choose **Pick UV → Corner**, **Face** or **Island**, then click the canvas. An island follows shared geometric edges with matching UV endpoints; it stops at authored seams. **Select face** and **Select island** use the active face. Ctrl-click toggles individual corners or adds faces/islands. Click empty canvas to clear the selection.

Drag a selected corner, face or island to move its UVs. The preview leaves the source unchanged until release, then creates one Undo step. Press Esc during a drag to cancel. Use the mouse wheel to zoom around the pointer and middle-drag to pan. **Frame selection** fits the selected region in the view; **Frame tile** restores the 0–1 tile. These navigation actions do not change the Mesh.

Enter **Move UV**, **Rotate UV (deg)** and two **Scale UV** values to transform the selection around its shared center. Negative scale mirrors an axis. **Preview UV** shows the candidate; **Apply UV transform** accepts it, and **Cancel UV preview** clears it. Finish a numeric preview before dragging. **Mirror U** / **Mirror V** flip the selection. **Fit selected UVs** centers the selection in the tile with the chosen **Tile padding**, preserving its proportions. This fits one selected region; it does not pack multiple islands into an atlas. Empty selections, zero scale, invalid coordinates and point-only fits report an error without changing the source.

**Planar project face** still projects just the active face along its dominant normal axis. Face corners remain independent, so editing one side of a seam does not weld it to the other side. Stop Play before editing; navigation remains available when the Mesh is read-only.

### Select and arrange UV corners

Use **Select all UVs**, **Clear UV selection** and **Invert UV selection** to choose
face corners quickly. Turn on **Box select UVs**, then drag a rectangle in the UV
canvas. Plain replaces the selection, Ctrl adds, and Shift removes. In **Face**
or **Island** pick mode, a corner inside the rectangle selects its whole face or
connected island. A held rectangle shows a preview; release accepts the selection.
Escape cancels it. Changing focus, source, view or Play state also cancels it.
Turn box mode off to move selected corners with ordinary dragging again.

Under **Arrange selected corners**, choose **Arrange axis → U** (horizontal) or
**V** (vertical). **Align UV corners** puts that coordinate at the selection's
minimum, center or maximum. Center means halfway between its bounds. **Distribute
UV corners** spaces distinct coordinate levels evenly between the endpoints;
coincident points stay together. Each applied edit is one source Undo step.

These actions edit individual corners. They can flatten a region, change a seam
or stretch its texture; they do not move whole islands as rigid shapes. Use packing
when you want to arrange whole islands while keeping their proportions. Selecting
corners changes no Mesh data; applying alignment or spacing needs **Save & Publish**
to update Scene, Play and export. Try these controls on Gallery 14 or 44 Meshes.

### Pack several islands together

In **UV → Island packing**, choose **Packing padding** to leave space at the tile
edges and between islands. **Rotate tall islands** allows quarter turns without
mirroring. **Preview all UV islands** arranges every island in the 0–1 tile. To
work with a subset, select corners, faces or islands first, then use **Preview
selected UV islands**; each touched island moves as a whole.

The wireframe shows the preview while the source stays unchanged. **Apply UV
packing** commits one Mesh Undo step. **Cancel UV packing** or Escape restores
the applied mapping. Save publishes applied edits; apply the preview first if you
want to keep it. Changing source/history, entering Play, closing or returning to
Model cancels an unaccepted packing preview.

Packing keeps island proportions and relative sizes, and arranges their bounding
rectangles. It does not unwrap faces or repair distortion inside an island. A
selected-only pack leaves other islands in place, so they may overlap the packed
selection. Use the all-islands preview when you need the whole Mesh separated.
Excessive padding or an island collapsed to a line/point reports an error and
preserves the current mapping.

Open **44-uv-island-packing** in the Feature Gallery. Three checker-textured panels
compare overlapping regions, separated rectangles and packing with quarter turns.
Open **UvPackingOriginal.mesh.json**, **UvPackingPacked.mesh.json** or
**UvPackingRotated.mesh.json**, choose UV and inspect the layouts. Save & Publish
updates the normal Scene, Play and export workflow.

Choose **Save & Publish** after shape or UV edits. An accepted publication updates the shared Mesh resource used by scene entities; an invalid cook keeps the previous published revision. The scene's position, scale and material assignments are separate from Mesh source history. Stop Play before editing. Exported games contain the selected cooked Mesh and UVs, without `.mesh.json` or modeling tools.

Element IDs stay reserved after Undo, so restoring the original shape can still leave the source needing a save. New edits never reuse those discarded IDs.

Try **Feature Gallery → Scenes/14-uv-islands.scene.json** to compare connected, split and mirrored/fitted checker mappings. Open the three UV Mesh sources from Content to edit them.

Try **Feature Gallery → Scenes/12-inset-panels.scene.json** for a flat frame, a raised panel and a recessed panel. Open their editable Meshes from Content to inspect the borders and UVs; Play and export use the published geometry.

Source Undo/Redo belongs to the active Editable Mesh document; Scene Undo/Redo remains separate. Close prompts offer **Save**, **Discard** and **Keep editing** when source changes have not been published. Reopen the registered Mesh from Content to continue editing.

## Current scope

The first modeling workflow supports bounded, planar convex polygons with explicit vertex and face IDs, face inset and signed extrusion, selection transforms and face-corner UV editing. It does not edit imported Model meshes, skin/morph targets, LODs, automatic subdivision, dense sculpting or texture-pixel painting. Complex or nonmanifold geometry is rejected rather than silently changed.

## Make editable triangles

In Model, select a face and choose **Triangulate face**, or choose **Triangulate
Mesh** for all polygons. Each action makes one Undo step. Positions, colors and
UVs stay in place. Triangles can bend independently when you sculpt or move their
vertices; polygon faces must remain planar. Smooth shading may change because the
triangle connections change. Save & Publish to update Scene, Play and export.

Open **16-authored-triangulation** in the Feature Gallery. The first two cubes look
the same in the game; open their Mesh sources to compare quad and triangle edges.
The third cube has an edited, colored corner made possible by its triangle faces.

## Choose flat or smooth shading

In Model, choose **Shade flat** for distinct facets or **Shade smooth** for blended
lighting across shared vertices. Each change makes one Undo step. Smooth shading
adds no polygons and keeps the same outline. The wireframe shows the geometry;
choose **Save & Publish**, then view the Mesh in Scene or Play to see the lighting
change. Published shading is also included in exported games.

Open **17-mesh-shading** in the Feature Gallery to compare two spheres with exactly
the same shape. The left sphere uses flat normals; the right uses smooth normals.

## Change which side faces outward

In Model, select a face and choose **Reverse connected surface**. FORGE reverses
that face and the faces joined to it by shared edges, keeping their winding
consistent. Faces that only touch at a vertex belong to separate surfaces.
**Reverse Mesh facing** reverses every surface in the source.

The vertex positions, colors and corner UVs stay attached to the same geometry.
Each action makes one Undo step. Reversing twice restores the original source.
The visible side and lighting direction change; the wireframe keeps the same
shape. Choose **Save & Publish** and inspect the result in Scene, Play or export.
UV corner selection resets because the corner order changed. If smooth normals
would cancel to an undefined direction at a shared vertex, FORGE rejects the edit
without replacing the source.

Open **19-mesh-facing** in the Feature Gallery. Two original open-front rooms
have the same shape. The left has outward-facing surfaces; the right faces inward
for viewing the room interior. Open **OutwardRoom.mesh.json** or
**InwardRoom.mesh.json** in Content to reverse their facing.

## Model responsiveness

The Model workspace reuses its calculated wireframe while the Mesh and view stay
still. It refreshes the view when you edit geometry, orbit, resize, switch sources
or change a preview. Vertex selection and highlighting work with the same controls.
