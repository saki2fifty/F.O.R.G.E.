# Transforms

A transform describes an object's position, rotation, and scale. All three are stored as Flecs component data and saved with the scene.

## Position, rotation, and scale

**Position** places the object relative to its spatial parent. With **Spatial binding → World**, it is a world position. One unit represents one meter. **Rotation** uses X, Y, and Z angles in degrees. **Scale** changes its size along its local axes.

FORGE applies local scale, then X/Y/Z rotation, then local position, followed by the spatial parent’s transform. Rotation is saved as a quaternion, so saving does not repeatedly convert through the displayed angles. Equivalent angles may display differently after reopening.

1. Select an object in Hierarchy or Scene.
2. In Inspector, drag a **Position**, **Rotation**, or **Scale** number, or Ctrl-click it to type a value.
3. Watch the preview while editing.
4. Release the drag or finish the text edit to commit one undo step. Press Escape during a drag to cancel.

Scale accepts -10000 to +10000 on each axis. Negative values mirror that axis; zero collapses it. Small values such as 0.0001 are accepted. Rotation accepts values within ±360000 degrees. A Plane has no thickness, so its local Y scale does not give it depth.

## Rotate and scale from the Scene

Select an object, then place the pointer over the Scene image.

- Press **R** to start rotating. Move the mouse horizontally to preview the angle.
- Press **S** to start scaling. Move right to grow the object or left to shrink it.
- Press **X**, **Y**, or **Z** to restrict the operation to that axis. Press the same axis again to remove the restriction.
- Type a number for an exact angle in degrees or a scale multiplier. Backspace edits the number.
- Press **Enter** or click inside the Scene image to apply. **Escape** or **RMB** cancels.

For example, **R → Z → 90 → Enter** rotates 90 degrees around world Z. **S → X → 2 → Enter** doubles the object's local X size. **S → 0.5 → Enter** halves its size on every axis.

The on-screen strip shows the operation, axis and value. Each accepted transform is one Undo step. Cancelling, losing focus, hiding/resizing the Scene panel, or switching documents discards the preview. Invalid scale values cannot be accepted; correct the value or cancel.

Rotation uses **world axes**, or the captured viewing axis when unconstrained. Scale uses the object's **local axes**, or all axes together when unconstrained. The pivot is the selected object's origin. Children set to **Follow parent** follow these transforms. A rotated child under a stretched parent can have a sheared world shape; FORGE preserves that shape. A world rotation that cannot be represented by the child’s local rotation alone is rejected with a message. World-axis scaling of rotated objects, plane constraints and rotation/scale drag handles are not implemented. Negative and zero scale are supported as described below. For multiple selected entities, use the Inspector controls described in [Inspector](inspector.md).

Shortcuts start only over the Scene image, outside text editing and camera gestures. **RMB+S still flies backward**.

## Copy, paste, and reset

**Copy transform** copies effective **local** position, rotation, and scale to FORGE's internal clipboard. Select another object and choose **Paste transform** to apply them as one undoable edit. Paste intentionally overrides all three local channels; quaternion rotation is copied directly without converting through Euler angles. Shape, color, hierarchy, and identity are retained. The internal clipboard lasts for the editor session and is separate from the system clipboard.

**Reset transform** sets position and rotation to zero and scale to one. Choose **Reset position** in the same menu to change only position. Both are undoable.

## Ground placement and snapping

In the command palette, **Transform / Place on ground** moves the lowest point of the transformed mesh to world Y=0, keeping X and Z. It accounts for rotation and scale. It does not query terrain or other objects.

**Snap position** rounds world position using the Scene **View → Snap spacing**. Move-handle snapping also remains position-only; it does not snap rotation or scale.

Stop Play before editing objects. Inspector, menus, shortcuts, the command palette, and external scene edits share this rule. See [Play mode](play-mode.md) and [Undo and redo](undo-redo.md).

## Choose what the object follows

In **Inspector → Transform → Spatial binding**:

- **Follow parent** uses the immediate Hierarchy parent’s transform, if that parent has one. Moving, rotating or scaling that parent moves the child.
- **World** keeps the object spatially independent while retaining its Hierarchy organization. Older scenes open in this mode so their existing behavior is preserved.
- **Explicit attachment** chooses another transformed object in this scene to follow, independently of Hierarchy ownership.

Changing Spatial binding preserves world placement when possible. Inspector’s Position/Rotation/Scale values may change to compensate. An operation that requires unsupported local shear or a singular transform is rejected; the object and history remain unchanged. The automation API also offers explicit **keep_local** behavior, which retains the local numbers and allows the object to move.

A missing attachment hides the object instead of placing it at a guessed location. Repair its target through automation, or use **Detach (keep local)** in Inspector, then set the intended position. This recovery keeps its local values and may change its placement. Cross-scene attachments are not available yet.

## Prefab transform overrides

The three channels inherit independently. Moving an instance overrides its position only; rotation and scale can continue following its prefab. Rotating or scaling similarly overrides just that channel. Right-click the **Scale** field to access **Revert translation**, **Revert rotation**, and **Revert scale** individually. Removing an override restores the inherited value, or the default/absence when no prefab supplies it.

Preserving world placement during reparenting can require new local values. FORGE creates only the needed channel overrides; a rotated/scaled parent may require all three. **Paste transform** and **Reset transform** intentionally override all three. Structured prefab creation, source editing, instantiation and Revert are available; **Apply to Prefab** remains deferred. See [Prefabs](prefabs.md).

## Mirroring and zero scale

Type a negative value in **Scale** to mirror an axis. **S**, then **X/Y/Z**,
changes only that local axis; drag left through a zero multiplier to mirror, or
type a signed multiplier. Axis changes recompute from the original transform.
Confirm creates one Undo step; Escape restores the original. Multiplying an axis
that was already zero keeps it zero: select the object in Hierarchy and type a
nonzero Scale value to restore it.

A collapsed object remains in the scene and Hierarchy. Loaded mesh selection tests forward-transformed geometry, including surviving
flattened surfaces; it does not require inverting the object transform. Completely
collapsed geometry may have no pickable area, so select it from Hierarchy. Parenting with
**Keep local** still works under a zero-scale parent; **Preserve world** or a
world-space gesture can fail with an inverse-unavailable diagnostic. The failed
operation leaves the scene unchanged. Collider restrictions are separate: see
[Physics](physics.md). Saving extended scale values marks scenes/prefabs with a
newer document version; older FORGE builds cannot open them.
