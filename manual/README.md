# FORGE User Manual

Learn to build scenes, create game behavior and export a Windows game with FORGE.
Start with [Find your way around](getting-started/editor-tour.md) and
[Your first scene](getting-started/first-scene.md), or open the editable
[Feature Gallery](getting-started/feature-gallery.md) to try a specific tool.
The Gallery includes [local diffuse lighting, light-image and spatial anti-aliasing examples](editor/lighting.md)
and a [64-projector floor](editor/decals.md).

Open **Help → User Manual** for the offline edition included with the editor.
Click a screenshot to view it at full size. Your panel arrangement and example
content may differ; use **Window** to open a panel mentioned in a guide.

## Start here

- [Find your way around FORGE](getting-started/editor-tour.md)
- [Your first scene](getting-started/first-scene.md)
- [Explore the Feature Gallery](getting-started/feature-gallery.md)
- [Build a blockout scene](getting-started/blockout.md)
- [Projects](editor/projects.md)
- [Scenes](editor/scenes.md)
- [Project settings, startup scene and fixed ECS workers](editor/project-settings.md)

## Objects and reusable content

- [Entities and hierarchy](editor/entities-hierarchy.md)
- [Built-in shapes](editor/primitives.md)
- [Transforms](editor/transforms.md)
- [Inspector](editor/inspector.md)
- [Content browser](editor/content-browser.md)
- [Prefabs, source contributions, nested assemblies, variants and member structure](editor/prefabs.md)
- [Saving and recovery](editor/saving-recovery.md)
- [Undo and redo](editor/undo-redo.md)

## Models and appearance

- [Importing and placing models, including FBX](editor/models.md)
- [Editable Meshes, flat 2D outlines, holes, curves and UVs](editor/editable-meshes.md)
- [Shape terrain with height layers](editor/terrain.md)
- [Paint and place foliage with slope filters, alignment, clearance and wind](editor/foliage.md)
- [Importing textures, exporting stored/block samples, signed PNG16, BC6 HDR and image inspection](editor/textures.md)
- [Materials](editor/materials.md)
- [Paint textures on a canvas or Mesh, copy images and publish for Materials](editor/texture-painting.md)
- [Show a live camera image on a Mesh](editor/render-textures.md)
- [Blend overlapping transparent Meshes](editor/transparency.md)
- [Project color, normals and roughness onto a Mesh](editor/decals.md)
- [Material Graph for Meshes and Sprites](editor/material-graphs.md)
- [Use an imported diffuse lightmap](editor/imported-lightmaps.md)
- [Shader import](editor/shaders.md)
- [Occlusion culling: current-view hidden geometry and moving occluders](editor/occlusion-culling.md)
- [Scene lighting, light images, rectangular and spherical emitters, contact-hardening shadows, reflection-map capture and rebaking, runtime probe capture, local reflection probes, roughness-filtered screen-space reflections, ambient occlusion with material normals, motion blur, depth of field, temporal, fast approximate and morphological anti-aliasing, distance fog, local fog volumes, exposure and color](editor/lighting.md)
- [Mesh particle effects](editor/particles.md)

## 2D scenes

- [World sprites, image glow, maintained atlas recipes, frame filtering, packing, trimming and fills](editor/sprites.md)
- [Tile Sets and Tile Maps, camera-driven chunk loading, neighbor rules, animated tiles, collision contours and holes, custom one-way boundaries, Dynamic rigid Maps, moving platforms and triggers](editor/tile-maps.md)
- [2D physics and character movement](editor/physics-2d.md), including moving one-way platforms, slopes, ceilings and stair stepping

## Animation

- [Transform animation timeline and nested and collection C++ component fields](editor/transform-animation.md)
- [Arrange up to eight skeletal or object clips per track, layer overlaps, crossfade properties/Material/component fields, blend camera shots, repeat nested controller placements with rates and loops, and export PNG/WebP/AVI previews](editor/animation-sequences.md)
- [Transfer animation between imported skeletons](editor/animation-retargeting.md)
- [Compose clips, blends and additive layers with named graph inputs](editor/animation-graphs.md)
- [Animation](editor/animation.md)
- [Choose named animation states](editor/animation-states.md)
- [Preview and blend two animations](editor/animation-blending.md)
- [Let an animation move its object](editor/animation-root-motion.md)
- [Make bones reach or aim](editor/animation-rigging.md)
- [Trigger C++ actions during an animation](editor/animation-markers.md)
- [See and control an animation's progress](editor/animation-playback-status.md)

## Game behavior

- [Choose game modes and condition object activity](editor/application-states.md)
- [Project Code (C++ components, project-wide source search, worker systems and unscaled timers)](editor/native-gameplay.md)
- [Gameplay input, sixteen local controllers, raw motion, analog response and vibration](editor/input.md)
- [Record and replay fixed gameplay input](editor/input-recording.md)
- [Send component data on press, release or change](editor/input-messages.md)
- [Touch contacts, tap, hold, swipe, pan, pinch and rotation](editor/touch-input.md)
- [Physics](editor/physics.md)
- [Navigation profiles, authored links, polygon regions, static collision collection, moving obstacles and destinations](editor/navigation.md)
- [Create patrol, wait and chase graphs and behavior trees](editor/navigation-behavior.md)
- [Audio: import, stream, create ordered playlists, event sound cues, offsets, repeating and finite runs, filters, source/shared bus effects, impulse responses, buses/sends, live mixing and music/UI pause](editor/audio.md)
- [Runtime UI: HUDs, interactive quad/animated-mesh surfaces, camera-image panels, menus, animation, filters, shadows, backdrops, gradients, alpha/luminance masks and static/animated panel shadows](editor/runtime-ui.md)
- [Animate named UI values with a Scene sequence](editor/sequence-ui.md)
- [Create UI controls, zoom and scroll Preview, select with a rectangle or keyboard, duplicate and delete, choose or drop project images, link and order stylesheets, declare project fonts and collection faces with weight, style and missing-glyph fallback, edit properties and styles, align or size groups, snap and constrain drags, retain visible aspect and move with arrow keys](editor/ui-source.md)
- [Shape game UI text: ligatures, script runs, collections, weights and layered color glyphs](editor/ui-text-shaping.md)
- [Edit and select whole UI characters: arrows, words, lines/pages, deletion, agent testing and native input limits](editor/ui-text-input.md)
- [Form controls: buttons, sliders, checkboxes, radio groups, dropdowns, progress, keyboard input and agent testing](editor/ui-form-controls.md)
- [Translate a game's menus and HUD](editor/ui-localization.md)
- [Flecs Script](editor/flecs-script.md)

## Play and export

- [Play mode](editor/play-mode.md)
- [Record a fresh game movie with sound](editor/game-recording.md)
- [Export a game or package content](editor/runtime-content.md)
- [Run a standalone game](getting-started/standalone-runtime.md)
- [Reference game](reference-game.md)
- [C++ physics contact example](samples/physics-contacts.md)
- [C++ physics volumes, rays and exclusions](samples/physics-volumes.md)
- [C++ physics velocity example](samples/physics-velocity.md)
- [C++ physics loads example](samples/physics-loads.md)

## Workspace and troubleshooting

- [Viewport and PNG/WebP/JPEG image export](editor/viewport.md)
- [Panels and windows](editor/panels-windows.md)
- [Settings and appearance](editor/settings.md)
- [Keyboard shortcuts](editor/shortcuts.md)
- [Command palette](editor/commands.md)
- [Problems and diagnostics](editor/diagnostics.md)
- [Performance, CPU timelines, worker tracks, shared writes and ordering targets](editor/performance.md)
- [Loaded resources and import cache](editor/resources.md)
- [ECS inspection](editor/ecs-tools.md)

## Agents and command-line tools

- [Connect an AI agent through MCP](editor/mcp.md)
- [Live automation](editor/live-automation.md)
- [Headless authoring tools](editor/automation.md)
- [Inspect project assets from the command line](editor/asset-tools.md)
