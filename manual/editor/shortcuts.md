# Keyboard shortcuts

Commands that edit scenes are suppressed while typing or interacting with modal dialogs. Mouse navigation must begin over the Scene image.

## Files and editing: defaults

- Ctrl+N: new scene.
- Ctrl+O: open a scene within the project.
- Ctrl+S: save the active task, including the active C++ source file.
- Ctrl+Shift+S: Save scene As... (active Scene task).
- Ctrl+Z: undo.
- Ctrl+Shift+Z or Ctrl+Y: redo.
- Ctrl+D: duplicate the selected structural subtrees.
- Delete: delete the selected structural subtrees when Hierarchy has focus.
- Enter in Inspector Name: commit the name.

## Change editor shortcuts

Open **Edit → Preferences → Editor shortcuts...**. In a narrow window, open
**Menu** first. Search for a command, then choose **Change...** beside one of its
two shortcut slots. Press the new combination. FORGE saves it immediately in
your personal preferences and shows the same shortcut in menus and command search.

Use Ctrl, Alt or Super with a supported key, or use F8–F24 alone. The recorder
rejects a combination already assigned to another command and explains where it
is used. Clear that command's binding first if you want to reuse it. Escape or
**Cancel** keeps the previous binding. The recorded key is consumed; it does not
also run the command.

**Clear** unassigns one slot. **Reset** restores that command's defaults;
**Reset all shortcuts** restores every default. A failed preferences save keeps
the last working bindings. Changes survive restarting FORGE and apply to your
editor, independently of the project or exported game.

The configurable commands are New scene, Open scene, Save scene As, active-task
Save, Undo, Redo, Duplicate subtree, Export Game and Play. Export Game and Play
start unassigned. Stop Play and finish an active operation before changing keys.
Commands still require their normal selection, task and runtime conditions.

Typing, C++ source Undo/Redo and Find, Hierarchy F2/Delete, viewport tools,
interface size, command search, workspace folding and gameplay safety controls
keep their contextual keys. They are listed below and in the shortcuts window.
The active C++ source uses your configured **Save** key; ordinary text editing
continues to own its other keys. Global scene actions do not consume text input,
active dialogs, viewport gestures or captured gameplay input.

## Interface size

- Ctrl+Minus: reduce interface size.
- Ctrl+Plus or Ctrl+Equals: increase interface size.
- Ctrl+0: restore 100%.

Keypad plus, minus, and zero are also supported.

## Viewport authoring

- Left-click: select an object; empty space clears selection.
- Ctrl-click or Shift-click an object: toggle it in the selection.
- Drag an axis handle on the primary object: move the selection along its world axis.
- Drag the center square: move the selection in the viewing plane.
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
- F with the image hovered: frame the selection and its structural descendants.

See [Viewport](viewport.md) for directions and [Scenes](scenes.md) for file behavior.

## Command search

Press **Ctrl+Shift+P** after finishing a drag or text edit to open the [Command palette](commands.md). Search, choose with Up/Down, and press Enter. Escape closes it.

## Scene tool selection

Hover the Scene image and press **W** for Move or **Q** for Select. Move shows the selected object’s X/Y/Z handles. Select hides handles. These keys do not switch tools while navigating with RMB/MMB or typing into a field.


During explicit gameplay input capture, Esc releases controls, F6 pauses/resumes and F7 steps. Other keyboard/mouse input belongs to project actions until capture ends. See [Gameplay input](input.md).

## Active task and entity commands

The bindings above are defaults; your configured Save, Undo and Redo bindings
follow the active document. Save writes or publishes that document: Scene, the
active C++ file, Prefab source, Project Settings or a supported asset draft.
The toolbar's **Save:** label identifies the active task. Focus Scene before saving
the scene. **Save scene As...** is available only when Scene owns the task.

Undo and Redo use the active document's history when it provides one. They do not
fall through to Scene history while another document owns focus. Prefab source
and Project Settings do not provide their own Undo/Redo; C++ text editing keeps
its contextual text shortcuts. See [Undo and redo](undo-redo.md).

In Hierarchy, **F2** focuses Name in Inspector for a single selection. **Ctrl+D**
duplicates selected structural subtrees; **Delete** deletes the Hierarchy
selection. A selected child of a selected parent is not processed twice. Each
accepted batch is one Scene Undo step. The context menu and Entity menu use the
same scene actions. See [Hierarchy selection](entities-hierarchy.md) for prefab
and batch limits. Ctrl+Shift+P searches the shared action list, including Save and
runtime controls.

## Bottom workspace

**Ctrl+Space** folds or expands Content, Build, Problems and Console. It is
inactive while typing or capturing Game input. The status-bar Workspace button
and Window menu provide the same operation.
