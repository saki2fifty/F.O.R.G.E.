# FORGE User Manual

FORGE is the Flecs-Oriented Runtime & Game Editor. Use this manual to learn the editor's current controls and complete everyday authoring tasks.

Start with [Your first scene](getting-started/first-scene.md), explore the [Feature Gallery](getting-started/feature-gallery.md), or try [Build a blockout scene](getting-started/blockout.md) with the packaged example. The offline edition is included with each Windows ZIP and opens through **Help → User Manual**. Its build identifier matches the packaged editor. Markdown pages in the repository track the current source.

## Projects and authored content

- [Standalone development host](getting-started/standalone-runtime.md): source-build startup and current limits.
- [Cooked content packages](editor/runtime-content.md): prepare and verify source-independent asset data.
- [Loaded resources and cache maintenance](editor/resources.md)

- [Flecs Script](editor/flecs-script.md): edit project scripts and inspect managed preview results.

- [Runtime UI](editor/runtime-ui.md): create a game HUD, use buttons and text fields, and test Pause/Step.

- [Navigation](editor/navigation.md): build static navmeshes and test nonphysics agents.
- [Transform animation timeline](editor/transform-animation.md): keyframe an object, preview it and export its motion.
- [Animation](editor/animation.md): convert glTF skeletons/clips and test Animator playback.
- [Animation root motion](editor/animation-root-motion.md): move an imported model or pass its movement to C++ physics.
- [Preview and blend two animations](editor/animation-blending.md)
- [Make bones reach or aim](editor/animation-rigging.md)
- [Choose named animation states](editor/animation-states.md)
- [Trigger C++ actions during an animation](editor/animation-markers.md)
- [See animation progress and control playback from C++](editor/animation-playback-status.md)
- [Audio](editor/audio.md): WAV clips, sound sources, listeners, pause and prefabs.

- [Projects](editor/projects.md): create, open, and return to projects.
- [Project settings](editor/project-settings.md): shared simulation frequency, startup scene, and controls.
- [Scenes](editor/scenes.md): create and switch scene files.
- [C++ physics velocity example](samples/physics-velocity.md): launch bodies and stop movement or rotation independently.
- [Entities and hierarchy](editor/entities-hierarchy.md): add, rename, parent, duplicate, and delete entities.
- [Model import tools](editor/models.md): prepare static glTF asset families and resolve identity conflicts.
- [Scene lighting](editor/lighting.md): environment maps, sky, exposure and HDR bloom.
- [Textures](editor/textures.md): import images, choose usages and reimport safely.
- [Material Graph](editor/material-graphs.md): connect surface values, reuse functions and make material variations.
- [Materials](editor/materials.md): edit reusable surfaces, preview them and assign mesh slots.
- [Editable Meshes and UVs](editor/editable-meshes.md): shape a reusable cube Mesh and place its texture coordinates.
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

- [Reference game](reference-game.md): standalone controls, menus, rebinding and saves.

- [Physics](editor/physics.md): make bodies fall and collide, with Pause/Step and prefab support.
- [C++ contact example](samples/physics-contacts.md): react to a sensor zone with editable speed and reversal settings.
- [C++ volume example](samples/physics-volumes.md): count current objects inside a query shape, with sensor filtering.
- [Play mode](editor/play-mode.md): run an isolated copy of your scene.
- [Gameplay input](editor/input.md): bind actions and inspect fixed-tick input without writing code.
- [Project Code](editor/native-gameplay.md): create, edit, and compile project C++ components and systems.

## Current boundaries

This manual describes the source or packaged build it accompanies. FORGE includes
imported and skinned mesh rendering, materials, textures, scene lighting, standalone
export, runtime dependency declarations, Collision assets and reusable character
mechanics. Build260923-000066 predates these Phase8 runtime/export/physics additions.
Terrain remains future work. Material graphs are described in the graph guide. The controller supplies physics mechanics;
gameplay code still supplies movement keys and camera behavior.

## Commands and inspection

- [Command palette](editor/commands.md)
- [Scene diagnostics and component schema](editor/diagnostics.md)
- [Headless authoring tools](editor/automation.md)
- [Asset inspection tools](editor/asset-tools.md): scan sources and query registered dependencies without project writes.

- [Live automation](editor/live-automation.md): let a trusted local script inspect or edit the open scene.

## CPU Profiler

Open **Window > CPU Profiler**, or use **Details > Open CPU Profiler** in the status bar.

- The **Editor** tab shows editor work; **Play runtime** shows the separate game worker after Play has an active world.
- Click **Start collection**, use the editor or run the game, then **Stop collection** to keep the recent samples on screen.
- **Clear samples** starts a fresh observation window. The filter searches category and scope names. Closing the panel keeps collection running until stopped.
- Compare **Average ms**, **Max ms** and **Latest ms**, with their sample count and fixed tick. Only the most recent512 completed measurements are retained.
- CPU measurements include elapsed waits, and nested rows can overlap. They are not GPU timings. The displayed FPS measures the editor loop.
- The existing **Performance** panel keeps its redraw comparison controls and frame averages. Exported games do not contain the editor profiler.
