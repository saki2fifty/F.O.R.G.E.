# Keyboard shortcuts

Commands that edit scenes are suppressed while typing or interacting with modal dialogs. Mouse navigation must begin over the Scene image.

## Files and editing

- Ctrl+N: new scene.
- Ctrl+O: open a scene within the project.
- Ctrl+S: save the active scene.
- Ctrl+Shift+S: Save As.
- Ctrl+Z: undo.
- Ctrl+Shift+Z or Ctrl+Y: redo.
- Ctrl+D: duplicate the selected subtree.
- Delete: delete the selected subtree when Hierarchy has focus.
- Enter in Inspector Name: commit the name.

## Interface size

- Ctrl+Minus: reduce interface size.
- Ctrl+Plus or Ctrl+Equals: increase interface size.
- Ctrl+0: restore 100%.

Keypad plus, minus, and zero are also supported.

## Viewport authoring

- Left-click: select the nearest visible block; empty space clears selection.
- Drag an axis handle: translate along its world axis.
- Drag the center square: translate in the viewing plane.
- Ctrl during a move: temporarily enable snapping.
- Escape during a move: cancel without changing the authored scene.

Viewport authoring is disabled during Play. A completed move is one Undo operation.

## Rotate and scale

- R: start rotation over the Scene image.
- S: start scale over the Scene image.
- X / Y / Z during a transform: constrain its axis; repeat to clear the constraint.
- Number keys, decimal point, minus and Backspace: enter/edit degrees or multiplier.
- Enter or left-click inside the image: apply one undoable transform.
- Escape or RMB: cancel.

Rotation constraints use world axes; scale constraints use local axes. Holding RMB keeps S available for flying backward. See [Transforms](transforms.md).

## Viewport navigation

- MMB drag: orbit.
- Shift+MMB drag: pan.
- RMB drag: look around.
- RMB+W/A/S/D: fly forward/left/back/right.
- RMB+Space: ascend.
- RMB+Shift: descend.
- Mouse wheel: change viewing distance.
- F with the image hovered: frame the selected visible block.

See [Viewport](viewport.md) for directions and [Scenes](scenes.md) for file behavior.

## Command search

Press **Ctrl+Shift+P** after finishing a drag or text edit to open the [Command palette](commands.md). Search, choose with Up/Down, and press Enter. Escape closes it.

## Scene tool selection

Hover the Scene image and press **W** for Move or **Q** for Select. Move shows the selected object’s X/Y/Z handles. Select hides handles. These keys do not switch tools while navigating with RMB/MMB or typing into a field.
