# Viewport

The Scene panel previews authored primitives. The separate Game panel presents the running world. Camera navigation changes your view without editing entity positions.

## Orbit and look

Hold the middle mouse button (MMB) and drag to orbit. Drag left to see more of the right face; drag down to see more of the top.

Hold the right mouse button (RMB) and drag to look around from the current camera position. Start the gesture over the Scene image. You do not need to select the Scene tab first when its image is visible.

## Fly and pan

While holding RMB, use W/S to fly forward/backward and A/D to fly left/right. Space raises altitude; Shift lowers it along world Y. Holding both cancels vertical movement. Release RMB to stop flight.

Hold Shift+MMB to pan. Dragging left moves the camera right; dragging down moves the camera up. The mouse wheel changes viewing distance.

Gestures that begin outside the image do not acquire viewport navigation. Losing application focus stops navigation.

## Frame what you need

Select an entity in Hierarchy, hover the Scene image, and press F to frame its geometry and structural descendants. **View → Frame selected** provides the same command. Imported models use their current loaded mesh, morph and skeletal bounds; a model root frames its complete placed subtree. **Fit scene** frames visible meshes together; an explicitly selected hidden mesh can still be framed. **Reset view** restores the default camera.

If part of the requested geometry is still loading or has failed, framing preserves the camera and reports that it is not ready. Retry after loading completes. It does not fit an incomplete subset or substitute a cube for a missing model. Very large extents can exceed the current camera framing range.

Left-click a visible block to select the nearest block under the pointer. Clicking empty space clears selection. The selected object has an amber outline of its rotated bounds; it can show through other blocks to help locate the selection. Selection changes Inspector and Hierarchy. Selection through this view is disabled during Play.

## Move a selected block

Choose **Move** in the Scene toolbar, or hover the Scene image and press **W**. The selected block shows red X, green Y, and blue Z handles plus a center square. **Select** (Q) hides the handles and only selects objects. The active tool is visible in the toolbar and the bottom-of-view hint.

If object axes are missing, check that **Move** is selected and Play is stopped. On the first launch after the old Move handles checkbox is replaced, the tool defaults to Move. Subsequent launches remember your explicit Select/Move choice. RMB+W still flies forward and does not switch tools.

1. Drag an axis handle to move only along that world axis.
2. Drag the center square to move in the camera's viewing plane.
3. Release the left mouse button to apply the move as one undoable edit.
4. Press Escape before release to cancel.

A drag previews the new position without saving partial changes. Losing application focus, resizing the viewport, or changing scenes cancels it. An axis pointing nearly straight toward the camera is hidden because it has too little screen length to drag; orbit to expose it. The move changes only the selected object’s local translation. Children set to **Follow parent** move with it; children set to **World** remain independent. See [Transforms](transforms.md).

Turn on **Snap** or hold Ctrl while dragging to snap moved coordinates to multiples of **View → Snap spacing**. Step is measured in world units. Ctrl+Plus/Minus continues to scale the interface independently of the camera. Rotation and scale are editable in Inspector. Rotation/scale handles, multi-selection, and local-axis move handles are not available yet.

## Reference grid and flight speed

**View → Grid** shows a world-anchored XZ reference plane at Y=0. It appears infinite: there is no rectangular patch edge. Thin, muted lines keep approximately one-pixel coverage as you zoom. Small divisions blend away as they become crowded, with subtle emphasis on larger divisions. The whole grid gradually loses contrast at shallow viewing angles toward the horizon, including the colored world axes. It is visible from above and below, with opaque scene objects covering the grid where appropriate.

Red marks world X at Z=0; blue marks world Z at X=0. Both cross at world `(0,0,0)`. The colored axes and gray lines are part of the same ground rendering, so look, pan, orbit and flight do not move them relative to objects or world coordinates. Their screen positions naturally change with the camera view. Looking exactly along the plane makes it edge-on; looking away from it hides it.

Open **View** to change **Grid spacing** and **Fly speed**. Grid spacing is the smallest displayed division; farther divisions are multiples of ten. This display adjustment does not change snap spacing. The grid is an editor reference, not a floor entity, collision surface or game asset.

Flight speed applies to RMB+WASD, Space and Shift. Tool choice, grid visibility/base spacing, snapping/step and flight speed persist between editor launches. Selection outlines and move handles remain editor overlays, so they can remain visible through other objects.

## Save a camera bookmark

Choose **View → Save view** to store one camera bookmark for the current scene. **View → Restore view** returns to it. The bookmark also restores automatically when that scene opens again, including after an editor restart. A scene without a bookmark opens with the default camera.

Bookmarks are stored separately from scene content under the project's `.forge` folder. They do not enter scene undo history. Each saved scene path has its own bookmark; untitled scenes share one slot. Save As does not copy the old filename's bookmark to the new filename.

## Orientation gizmo

The compact widget in the image's upper-right corner shows world X in red, Y in green, and Z in blue. Positive endpoints are filled; negative endpoints carry a minus sign.

Click an endpoint to look from that side toward the current camera target. **+Y** gives an exact top view and **-Y** an exact bottom view. Click the current viewing-axis endpoint again to flip to the opposite side. Drag the widget to orbit. It works without an object selected and does not create scene edits or undo entries.

The view label identifies axis views and the current **Perspective** projection. An axis view remains perspective; orthographic projection is not available yet. Toggle **View → Orientation gizmo** to hide or show it. The setting persists. The widget hides when the viewport is too small to fit it.

For object rotation and scaling with **R / S**, followed by **X / Y / Z**, see [Transforms](transforms.md).

## Static preview performance

An unchanged EDIT view reuses its rendered scene image. Navigation, object edits, resize and transform previews refresh it immediately; Play renders continuously. Grid, selection overlays and the interface remain responsive every frame. See [Performance](performance.md) for measurement and comparison controls.

## Tool discovery

Scene exposes **Select**, **Move**, **Rotate (R)** and **Scale (S)**. Rotate and Scale start the existing modal gesture: X/Y/Z constrains, Enter or left-click confirms, Escape cancels. **View** describes supported tool spaces: world axes for Move/Rotate, local axes for Scale. View also groups grid, orientation, navigation overlay, framing and camera options. Camera gestures and the existing infinite grid remain unchanged.

Runtime output and HUD now appear in the separate **Game** tab; Scene remains authored content during Play.

## Compact tool row

The **+** is Add Entity. Pointer, arrows, ring and diagonal-box icons select Select/Move or start Rotate/Scale. The selected icon is highlighted; Rotate/Scale returns to the resting tool when its modal gesture ends. The magnet toggles move snapping. The eye opens View settings. Hover for names, shortcuts and purpose. Existing camera and R/S/XYZ shortcuts are unchanged. The Scene tab carries the filename and unsaved `*`.

Transform icons also have named entries under **Entity > Transform tools** and in the Command Palette.

## Imported mesh loading

The Scene view prepares each entity's assigned **Mesh Renderer** with its materials
and textures. An incomplete replacement keeps the previous complete draw; an
initial failure appears in **Problems** with a diagnostic fallback. Moving the
camera is not required to reveal a completed load.

Scene and Game use the same rendering resources for static, skinned and morphed
meshes, PBR and unlit materials, supported optical extensions, lights, shadows and
environments. Game uses enabled authored Camera components. Model placement and
selection use imported mesh geometry; importing a file alone does not place it in
the scene. Drag the Model from Content onto Scene or use **Place in scene** in its
import document. See [Models](models.md) and [Play mode](play-mode.md).

Legacy primitive entities resolve built-in Mesh/Material resources without
rewriting their authored scene data. Their default material stays unlit. Assign a
PBR material when the object should receive scene lighting and shadows.

## Brightness and transparent materials

Open the Scene **View** menu and adjust **Exposure** to brighten or darken the
preview. A value of **+1 EV** doubles the light; **-1 EV** halves it. **0 EV** restores
the default. This personal preference is saved between editor sessions and does not
change authored objects or game cameras. The grid and selection overlays keep their
normal colors.

The viewport keeps bright lighting values in an HDR target, then uses PBR Neutral
tone mapping for the display. Blended imported materials render after solid and
cutout materials, from far to near. Intersecting transparent surfaces can still show
sorting artifacts; this is conventional transparency, not order-independent rendering.
These new display and queue paths are undergoing Windows rendering validation.

Use [Scene lighting](lighting.md) for authored environment maps and Game exposure.

## Place assets from Content

Drag a Model, Mesh or Prefab onto the Scene image to place it at the cursor on a plane through the view target. Each placement is one Undo step. Drag a Scene asset to open it through the normal unsaved-changes workflow. Materials, textures, clips and audio belong on compatible Inspector fields. See [Content browser](content-browser.md) for placement and cancellation details.
