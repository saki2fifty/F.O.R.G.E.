# Project settings

Project settings define shared game behavior. Open **Tools → Project Settings**. Your dock layout, interface scale, tooltips and compiler paths remain personal editor preferences.

## Change simulation frequency

1. Stop Play and wait for any native build to finish.
2. Open **Tools → Project Settings**.
3. Change **Simulation Hz** to a value from **1 to 240**.
4. Click **Save Settings**.
5. Start Play and check **Console → Fixed**.

The default is 60 Hz. Step still advances exactly one tick; at 120 Hz that tick represents 1/120 second. Settings apply when you next start Play. Saving settings does not alter scene Undo/Redo or the authored scene.

## Choose the startup scene

Save your current scene, then click **Use saved current scene as startup** and **Save Settings**. FORGE stores the scene's asset identity. Moving its file inside the project does not change that identity. Copying a scene file by hand can create duplicate identities and makes startup ambiguous; use the editor's Save scene As... workflow for an independent scene.

## Configure controls

The **Input actions** section defines project actions and their bindings. See [Gameplay input](input.md) for an exact walkthrough.

## Save and discard

**Save Settings** validates the complete candidate before atomically replacing `forge.project.json`. Invalid frequency, bindings or startup references leave the previous configuration intact. An externally changed manifest is rejected; reopen the project before editing it again.

**Discard edits** restores the currently loaded settings. Closing an unsaved window asks Save, Discard or Cancel. Scene Undo does not undo project settings.

Older version-1 manifests are read without replacing them. The first settings save writes version 2 and retains `forge.project.json.v1.backup`. Unknown fields are preserved. Unsupported future versions are rejected.

See also [Projects](projects.md), [Play mode](play-mode.md), and [Settings and appearance](settings.md).

## Gravity

**Physics → Gravity XYZ** controls acceleration for the next Play runtime. The default is (0, -9.81, 0) m/s². Unknown project settings remain preserved. See [Physics](physics.md).

## Save the active settings draft

The window title gains an asterisk and shows **Unsaved settings** after an edit. Ctrl+S saves these settings while this task is active. Closing or switching project asks **Save Settings**, **Discard**, or **Cancel**. A validation failure keeps the draft open. Scene Save and scene Undo/Redo do not own project settings.

There are at most 64 input actions and 16 bindings per action. Add controls disable and explain the limit when reached. Settings take effect on the next Play.

## Standalone game defaults

**Set up game defaults** creates the configuration for your standalone game.
**Game defaults** then exposes the game title, stable application ID, window mode,
resolution, display index, VSync, master volume and mouse sensitivity. **Save
Settings** publishes the draft; **Close Settings** asks what to do with unsaved
changes. These settings have separate ownership from scene Undo.

Keep the application ID unchanged across builds to retain access to the same player
saves/settings. Choose the saved startup scene here, then use
[Run > Export Game](runtime-content.md) to assemble a standalone folder.
