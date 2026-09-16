# Transforms

A transform describes an object's position, rotation, and scale. All three are stored as Flecs component data and saved with the scene.

## Position, rotation, and scale

**Position** places the object in world units. **Rotation** uses X, Y, and Z angles in degrees. **Scale** changes its size along its local axes.

FORGE applies local scale first, then X rotation, Y rotation, and Z rotation, then world position. Parenting organizes the hierarchy without inheriting the parent's transform.

1. Select an object in Hierarchy or Scene.
2. In Inspector, drag a **Position**, **Rotation**, or **Scale** number, or Ctrl-click it to type a value.
3. Watch the preview while editing.
4. Release the drag or finish the text edit to commit one undo step. Press Escape during a drag to cancel.

Scale must be positive, from 0.001 to 10000. Negative/mirrored and zero scales are not supported. Rotation accepts values within ±360000 degrees. A Plane has no thickness, so its local Y scale does not give it depth.

## Rotate and scale from the Scene

Select an object, then place the pointer over the Scene image.

- Press **R** to start rotating. Move the mouse horizontally to preview the angle.
- Press **S** to start scaling. Move right to grow the object or left to shrink it.
- Press **X**, **Y**, or **Z** to restrict the operation to that axis. Press the same axis again to remove the restriction.
- Type a number for an exact angle in degrees or a scale multiplier. Backspace edits the number.
- Press **Enter** or click inside the Scene image to apply. **Escape** or **RMB** cancels.

For example, **R → Z → 90 → Enter** rotates 90 degrees around world Z. **S → X → 2 → Enter** doubles the object's local X size. **S → 0.5 → Enter** halves its size on every axis.

The on-screen strip shows the operation, axis and value. Each accepted transform is one Undo step. Cancelling, losing focus, hiding/resizing the Scene panel, or switching documents discards the preview. Invalid scale values cannot be accepted; correct the value or cancel.

Rotation uses **world axes**, or the captured viewing axis when unconstrained. Scale uses the object's **local axes**, or all axes together when unconstrained. The pivot is the selected object's origin. This keeps rotated objects free of unsupported shear. Children do not follow these transforms. World-axis scaling of rotated objects, negative scale, multi-selection, plane constraints and rotation/scale drag handles are not implemented.

Shortcuts start only over the Scene image, outside text editing and camera gestures. **RMB+S still flies backward**.

## Copy, paste, and reset

**Copy transform** copies effective position, rotation, and scale to FORGE's internal clipboard. Select another object and choose **Paste transform** to apply them as one undoable edit. Shape, color, hierarchy, and identity are retained. The internal clipboard lasts for the editor session and is separate from the system clipboard.

**Reset transform** sets position and rotation to zero and scale to one. Right-click **Reset transform** and choose **Reset position** to change only position. Both are undoable.

## Ground placement and snapping

Under **Object actions**, **Place on ground** moves the lowest point of the transformed mesh to world Y=0, keeping X and Z. It accounts for rotation and scale. It does not query terrain or other objects.

**Snap position** rounds world position using the Scene **View → Snap spacing**. Move-handle snapping also remains position-only; it does not snap rotation or scale.

Stop Play before editing objects. Inspector, menus, shortcuts, the command palette, and external scene edits share this rule. See [Play mode](play-mode.md) and [Undo and redo](undo-redo.md).
