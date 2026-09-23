# FORGE User Manual

FORGE is the Flecs-Oriented Runtime & Game Editor. Use this manual to learn the editor's current controls and complete everyday authoring tasks.

Start with [Your first scene](getting-started/first-scene.md), or try [Build a blockout scene](getting-started/blockout.md) with the packaged example. The offline edition is included with each Windows ZIP and opens through **Help → User Manual**. Its build identifier matches the packaged editor. Markdown pages in the repository track the current source.

## Projects and authored content

- [Standalone development host](getting-started/standalone-runtime.md): source-build startup and current limits.
- [Cooked content packages](editor/runtime-content.md): prepare and verify source-independent asset data.
- [Loaded resources and cache maintenance](editor/resources.md)

- [Flecs Script](editor/flecs-script.md): edit project scripts and inspect managed preview results.

- [Runtime UI](editor/runtime-ui.md): create a game HUD, use buttons and text fields, and test Pause/Step.

- [Navigation](editor/navigation.md): build static navmeshes and test nonphysics agents.
- [Animation](editor/animation.md): convert glTF skeletons/clips and test Animator playback.
- [Audio](editor/audio.md): WAV clips, sound sources, listeners, pause and prefabs.

- [Projects](editor/projects.md): create, open, and return to projects.
- [Project settings](editor/project-settings.md): shared simulation frequency, startup scene, and controls.
- [Scenes](editor/scenes.md): create and switch scene files.
- [Entities and hierarchy](editor/entities-hierarchy.md): add, rename, parent, duplicate, and delete entities.
- [Model import tools](editor/models.md): prepare static glTF asset families and resolve identity conflicts.
- [Scene lighting](editor/lighting.md): environment maps, sky, intensity, rotation and exposure.
- [Textures](editor/textures.md): import images, choose usages and reimport safely.
- [Materials](editor/materials.md): edit reusable surfaces, preview them and assign mesh slots.
- [Shader import](editor/shaders.md): compile project HLSL programs and retain good revisions on errors.
- [Content browser](editor/content-browser.md): find and open project scene files.
- [Prefabs](editor/prefabs.md): create reusable groups, edit sources, override and revert instance values.
- [Primitives and color](editor/primitives.md): create built-in shapes and give them a tint.
- [Transforms](editor/transforms.md): rotate, scale, copy, paste, and reset objects.
- [Inspector](editor/inspector.md): edit the selected entity's properties.
- [Saving and recovery](editor/saving-recovery.md): protect unsaved work and restore snapshots.
- [Undo and redo](editor/undo-redo.md): reverse authored changes.

## Working in the editor

- [ECS inspection](editor/ecs-tools.md): optional queries, native JSON, statistics, alerts and read-only Explorer access.

- [Viewport](editor/viewport.md): orbit, look, fly, pan, and frame blocks.
- [Panels and windows](editor/panels-windows.md): arrange your workspace.
- [Settings and appearance](editor/settings.md): scale the interface and control tooltips.
- [Keyboard shortcuts](editor/shortcuts.md): find the available commands.
- [Performance and diagnostics](editor/performance.md): read counters and report problems.

## Testing gameplay

- [Physics](editor/physics.md): make bodies fall and collide, with Pause/Step and prefab support.
- [Play mode](editor/play-mode.md): run an isolated copy of your scene.
- [Gameplay input](editor/input.md): bind actions and inspect fixed-tick input without writing code.
- [Native gameplay](editor/native-gameplay.md): compile and reload the supported C++ sample.

## Current boundaries

The current Phase7 source adds imported and skinned mesh rendering, material authoring, textures and scene lighting alongside the existing blockout and gameplay tools. The complete Phase7 package is still in validation; repository instructions describe source behavior, not a new numbered delivery. Terrain, material graphs and standalone game export remain outside the current delivered workflows.

## Commands and inspection

- [Command palette](editor/commands.md)
- [Scene diagnostics and component schema](editor/diagnostics.md)
- [Headless authoring tools](editor/automation.md)
- [Asset inspection tools](editor/asset-tools.md): scan sources and query registered dependencies without project writes.

- [Live automation](editor/live-automation.md): let a trusted local script inspect or edit the open scene.
