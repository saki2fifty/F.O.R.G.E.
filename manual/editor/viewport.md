# Viewport

The Scene panel previews authored blocks or the current play world's blocks. Camera navigation changes your view without editing entity positions.

## Orbit and look

Hold the middle mouse button (MMB) and drag to orbit. Drag left to see more of the right face; drag down to see more of the top.

Hold the right mouse button (RMB) and drag to look around from the current camera position. Start the gesture over the Scene image. You do not need to select the Scene tab first when its image is visible.

## Fly and pan

While holding RMB, use W/S to fly forward/backward and A/D to fly left/right. Space raises altitude; Shift lowers it along world Y. Holding both cancels vertical movement. Release RMB to stop flight.

Hold Shift+MMB to pan. Dragging left moves the camera right; dragging down moves the camera up. The mouse wheel changes viewing distance.

Gestures that begin outside the image do not acquire viewport navigation. Losing application focus stops navigation.

## Frame what you need

Select an entity in World, hover the Scene image, and press F to frame its visible block. **Frame selected** provides the same command. **Fit scene** frames the visible blocks together; **Reset view** restores the default camera.

Left-click a visible block to select the nearest block under the pointer. Clicking empty space clears selection. The selected block has an amber wire outline; it can show through other blocks to help locate the selection. Selection changes Inspector and World. Selection through this view is disabled during Play.

## Move a selected block

Enable **Move handles** in Scene. The selected block shows red X, green Y, and blue Z handles plus a center square.

1. Drag an axis handle to move only along that world axis.
2. Drag the center square to move in the camera's viewing plane.
3. Release the left mouse button to apply the move as one undoable edit.
4. Press Escape before release to cancel.

A drag previews the new position without saving partial changes. Losing application focus, resizing the viewport, or changing scenes cancels it. An axis pointing nearly straight toward the camera is hidden because it has too little screen length to drag; orbit to expose it. Moves affect the selected entity only. Parenting still does not make children follow parent positions.

Turn on **Snap** or hold Ctrl while dragging to snap moved coordinates to multiples of **Step**. Step is measured in world units. Ctrl+Plus/Minus continues to scale the interface independently of the camera. Rotation, scale, multi-selection, and local-axis handles are not available yet.

## Reference grid and flight speed

**Grid** shows an XZ reference grid at world Y=0, centered near the camera target. Red marks X and blue marks Z. This is an editor overlay and can show through blocks; it is not collision geometry or a rendered game surface.

Expand **View settings** to change **Grid spacing** and **Fly speed**. Flight speed applies to RMB+WASD, Space, and Shift. Move-handle visibility, grid visibility/spacing, snapping/step, and flight speed persist between editor launches.

## Save a camera bookmark

Choose **Save view** to store one camera bookmark for the current scene. **Restore view** returns to it. The bookmark also restores automatically when that scene opens again, including after an editor restart. A scene without a bookmark opens with the default camera.

Bookmarks are stored separately from scene content under the project's `.forge` folder. They do not enter scene undo history. Each saved scene path has its own bookmark; untitled scenes share one slot. Save As does not copy the old filename's bookmark to the new filename.
