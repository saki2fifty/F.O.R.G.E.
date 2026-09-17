# FORGE User Manual

FORGE is the Flecs-Oriented Runtime & Game Editor. Use this manual to learn the editor's current controls and complete everyday authoring tasks.

Start with [Your first scene](getting-started/first-scene.md), or try [Build a blockout scene](getting-started/blockout.md) with the packaged example. The offline edition is included with each Windows ZIP and opens through **Help → User Manual**. Its build identifier matches the packaged editor. Markdown pages in the repository track the current source.

## Projects and authored content

- [Projects](editor/projects.md): create, open, and return to projects.
- [Project settings](editor/project-settings.md): shared simulation frequency, startup scene, and controls.
- [Scenes](editor/scenes.md): create and switch scene files.
- [Entities and hierarchy](editor/entities-hierarchy.md): add, rename, parent, duplicate, and delete entities.
- [Content browser](editor/content-browser.md): find and open project scene files.
- [Prefabs](editor/prefabs.md): create reusable groups, edit sources, override and revert instance values.
- [Primitives and color](editor/primitives.md): create built-in shapes and give them a tint.
- [Transforms](editor/transforms.md): rotate, scale, copy, paste, and reset objects.
- [Inspector](editor/inspector.md): edit the selected entity's properties.
- [Saving and recovery](editor/saving-recovery.md): protect unsaved work and restore snapshots.
- [Undo and redo](editor/undo-redo.md): reverse authored changes.

## Working in the editor

- [Viewport](editor/viewport.md): orbit, look, fly, pan, and frame blocks.
- [Panels and windows](editor/panels-windows.md): arrange your workspace.
- [Settings and appearance](editor/settings.md): scale the interface and control tooltips.
- [Keyboard shortcuts](editor/shortcuts.md): find the available commands.
- [Performance and diagnostics](editor/performance.md): read counters and report problems.

## Testing gameplay

- [Play mode](editor/play-mode.md): run an isolated copy of your scene.
- [Gameplay input](editor/input.md): bind actions and inspect fixed-tick input without writing code.
- [Native gameplay](editor/native-gameplay.md): compile and reload the supported C++ sample.

## Current boundaries

The viewport draws transformed built-in blockout meshes. Production materials, imported meshes, physics, audio, animation, terrain, and standalone game export are not available yet. This manual adds their instructions when their editor workflows exist.

## Commands and inspection

- [Command palette](editor/commands.md)
- [Scene diagnostics and component schema](editor/diagnostics.md)
- [Headless authoring tools](editor/automation.md)

- [Live automation](editor/live-automation.md): let a trusted local script inspect or edit the open scene.
