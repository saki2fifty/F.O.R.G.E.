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

The camera resets on scene/project changes and editor restarts. Viewport selection, transform gizmos, and camera persistence are not available yet. Ctrl+Plus/Minus scales the interface, independently of the camera.
