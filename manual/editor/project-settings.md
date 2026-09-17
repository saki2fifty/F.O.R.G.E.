# Project settings

Project settings define shared game behavior. Open **Tools → Project Settings**. Your dock layout, interface scale, tooltips and compiler paths remain personal editor preferences.

## Change simulation frequency

1. Stop Play and wait for any native build to finish.
2. Open **Tools → Project Settings**.
3. Change **Simulation Hz** to a value from **1 to 240**.
4. Click **Save settings**.
5. Start Play and check **Console → Fixed**.

The default is 60 Hz. Step still advances exactly one tick; at 120 Hz that tick represents 1/120 second. Settings apply when you next start Play. Saving settings does not alter scene Undo/Redo or the authored scene.

## Choose the startup scene

Save your current scene, then click **Use saved current scene as startup** and **Save settings**. FORGE stores the scene's asset identity. Moving its file inside the project does not change that identity. Copying a scene file by hand can create duplicate identities and makes startup ambiguous; use the editor's Save As workflow for an independent scene.

## Configure controls

The **Input actions** section defines project actions and their bindings. See [Gameplay input](input.md) for an exact walkthrough.

## Save and discard

**Save settings** validates the complete candidate before atomically replacing `forge.project.json`. Invalid frequency, bindings or startup references leave the previous configuration intact. An externally changed manifest is rejected; reopen the project before editing it again.

**Discard edits** restores the currently loaded settings. Closing the window discards edits. Scene Undo does not undo project settings.

Older version-1 manifests are read without replacing them. The first settings save writes version 2 and retains `forge.project.json.v1.backup`. Unknown fields are preserved. Unsupported future versions are rejected.

See also [Projects](projects.md), [Play mode](play-mode.md), and [Settings and appearance](settings.md).

## Gravity

**Physics → Gravity XYZ** controls acceleration for the next Play runtime. The default is (0, -9.81, 0) m/s². Unknown project settings remain preserved. See [Physics](physics.md).
