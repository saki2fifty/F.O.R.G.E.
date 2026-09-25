# Reference game

**FORGE / FIELD TEST** is a small project that exercises the standalone runtime.
Its gameplay module uses the same exact SDK as project code. The source is in
`samples/reference_game`; the acceptance content builder creates its level with
normal imports, collision assets and a navigation bake.

## Launch the included game

Extract the entire numbered Windows ZIP, then open **ReferenceGame/forge_game.exe**.
Keep its accompanying files together. This game runs independently of the editor;
you do not need a compiler or SDK installed to play it.

The reference game lives in the shipped `ReferenceGame/` folder; it is not
included as an editable project. The editor's **File → Open project** opens an
editable project of your own. **Gameplay Code → Native SDK folder** points at
the matching installed NativeSdk that hosts your gameplay module.

## Start and explore

Choose **New Game** from the main menu. The game loads the level and captures the
mouse. Use **WASD** to move, the mouse to look, **Space** to jump, **C** to crouch,
and **Left Shift** to sprint. Aim at the beacon and press **E** to interact.

With a controller, use the left stick to move, right stick to look, south button
to jump, east button to crouch, left-stick press to sprint, and west button to
interact. These names describe physical button positions rather than controller
brand labels. The camera eases down when the character successfully crouches and
returns to standing height when there is room to stand.

This is a functional blockout course with simple geometry, not finished game art.
Its directional sun is intended to illuminate the floor from above.

The level includes steps, slopes, a moving platform, a dynamic object, imported
collision, a navigation guide, spatial audio and a skinned animated flag. The exit
door returns to the main menu.

## Pause and menus

Press **Escape** or the controller's **Start** button to pause and release the
mouse. **Resume** captures it again. Losing window focus releases capture;
resuming is deliberate.

Use the pointer, **Tab / Shift+Tab / Enter**, or controller **up / down / south**
to operate menu controls. The controller's east button goes back.

The pause menu contains **Resume**, **Options**, **Save Game**, **Load Game**,
**Main Menu**, and **Quit**. Menu controls continue to work while simulation is
paused.

## Options and rebinding

Options offers windowed/borderless display, VSync, master volume, mouse/gamepad
sensitivity and Y inversion. Changes apply to this user and save automatically.

To change Jump:

1. Choose **Rebind Jump**.
2. Press a key, mouse button or gamepad control. **Escape** cancels listening.
3. Check the captured control and any conflict message.
4. Choose **Apply binding**. To deliberately share a conflicting control, use **Apply and share control**.

**Cancel binding** discards the candidate. **Clear Jump primary binding** removes
that slot; other bindings can still trigger Jump. **Restore default bindings**
restores the project's choices. Mouse motion is ignored while listening so moving
the pointer does not accidentally become the new binding.

## Save and return

Pause and choose **Save Game**, then **Quit**. Relaunch the same game installation
and choose **Continue** or **Load Game**. This example uses one named slot and saves
the level, player position, camera direction and beacon activation count. The
engine storage service also supports separate named slots.

Saves and preferences live in the operating system's user-data directory, outside
the game folder. Moving the installation keeps them available for the same
application identity. A rejected or corrupt save shows a message and is preserved;
it is not silently deleted. If a scene load fails while releasing the mouse, the
current scene stays open and the saved game is preserved.

See [Gameplay input](editor/input.md), [Runtime UI](editor/runtime-ui.md), and
[Play mode](editor/play-mode.md) for the editor's separate testing controls.
