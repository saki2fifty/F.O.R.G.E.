# Transforms

A transform describes an object's position, rotation, and scale. All three are stored as Flecs component data and saved with the scene.

## Position, rotation, and scale

**Position** places the object in world units. **Rotation** uses X, Y, and Z angles in degrees. **Scale** changes its size along its local axes.

FORGE applies local scale first, then X rotation, Y rotation, and Z rotation, then world position. Parenting organizes the hierarchy without inheriting the parent's transform.

1. Select an object in World or Scene.
2. In Inspector, drag a **Rotation** or **Scale** number, or Ctrl-click it to type a value.
3. Watch the preview while editing.
4. Release the drag or finish the text edit to commit one undo step. Press Escape during a drag to cancel.

Scale must be positive, from 0.001 to 10000. Negative/mirrored and zero scales are not supported. Rotation accepts values within ±360000 degrees. A Plane has no thickness, so its local Y scale does not give it depth.

Rotation and scale currently use Inspector fields. Viewport handles still perform world-axis or viewing-plane translation; rotation and scale handles are not available yet.

## Copy, paste, and reset

**Copy transform** copies effective position, rotation, and scale to FORGE's internal clipboard. Select another object and choose **Paste transform** to apply them as one undoable edit. Shape, color, hierarchy, and identity are retained. The internal clipboard lasts for the editor session and is separate from the system clipboard.

**Reset transform** sets position and rotation to zero and scale to one. **Reset position** changes only position. Both are undoable.

## Ground placement and snapping

**Place on ground** moves the lowest point of the transformed mesh to world Y=0, keeping X and Z. It accounts for rotation and scale. It does not query terrain or other objects.

**Snap position** rounds world position using the Scene panel's Step. Move-handle snapping also remains position-only; it does not snap rotation or scale.

During Play, Inspector changes the authored scene. Restart applies the updated transform to a fresh play world. See [Play mode](play-mode.md) and [Undo and redo](undo-redo.md).
