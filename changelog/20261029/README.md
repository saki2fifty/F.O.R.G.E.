# 2026-10-29 — Collision profiles and Scene surface effects

## Roughness-filtered current-depth reflections

- Added an optional **Roughness filtering** Scene control through the shared
  authoring, history, Save and MCP operation. It defaults off for existing scenes.
- Added bounded visible-source radiance averaging with geometry coverage and
  depth-range fallback. Six source scales share existing view ownership and exact
  Game/Scene payload admission, with no new material roles or SDK interface.
- Added finite HDR headroom handling in the opted-in path, actual production
  reduction/tracing and real-content checks, original editable Gallery109 and
  usage guidance with authentic captures.
- Windows/cooked D3D12 and physical acceptance remain separate; no numbered ZIP.

## Local reflection probes

- Added imported panorama/cube reflection maps in transformed box volumes with
  world-distance edge fade, stable priority blending and optional box projection.
  Base reflections change locally; global diffuse, sky and separate material
  layers retain their existing lighting.
- Reused texture/environment residency and surface-view ownership, including
  per-camera representability checks, last-good maps and an explicit eight-probe
  capacity fallback. Probe lighting composes before screen reflections and AO.
- Added the creation recipe, Inspector fields and volume guides, shared
  automation/MCP discovery, runtime scalar tuning and Gallery107.
- Added the installed `ReflectionProbe` value header and matching SDK example.
  The exact SDK fingerprint changes; existing modules need a matching rebuild.
- These are local source changes. Windows/cooked D3D12 and physical acceptance
  remain pending; no new numbered distribution is asserted here.

## Screen-space reflections

- Added default-off current-depth base-specular reflections using actual mapped
  material normals, roughness, unit response and replaceable environment light.
  Misses retain the environment; separate coat/sheen and anisotropic paths retain
  their environment response. No hidden/off-screen or probe reflection is implied.
- Reused mesh queue, per-view targets, camera budgets, geometry and authoring
  owners. Updated indirect light reaches occlusion before AA, motion and focus;
  spatial-only rendering has no jitter, motion/pose/color history or extra warmup.
- Extended surface compiler source to capability5/seven roles, retaining readable
  capabilities1–4 and stable logical IDs. Added actual Scene controls/API/MCP,
  original Gallery106, authentic captures and usage guidance.
- Local software rendering, authoring and source-free relocation are separate
  from Windows/MSVC/FXC/cooked D3D12 and physical acceptance. No numbered ZIP.

## Ambient occlusion

- Added a default-off spatial Scene effect that reduces separately resolved
  environment radiance near visible Mesh depth contacts, preserving punctual
  lighting, emission, additive fog color, alpha and current-frame layers.
- Reused existing view, posed geometry, draw variants, queue, shader/material and
  authoring owners. Spatial-only operation skips motion, pose retention, jitter
  and accumulation. Combined views preserve AA, motion blur, focus and capture.
- Added capability4 surface cooks with five roles; older1/2/3 archives remain
  readable with stable asset IDs. Older surfaces need recooking to participate.
- Added Scene Lighting controls, shared API/MCP fields, editable Gallery105 and
  task-focused manual guidance. Local validation remains separate from queued
  Windows/FXC/cooked D3D12 and physical acceptance. No numbered ZIP is issued.

## Surface motion blur

- Added default-off Scene motion blur from completed Mesh/camera movement, shared
  with optional edge accumulation, bounded per-view scratch and frozen sequence
  capture motion. Current-frame layers and depth silhouettes remain sharp.
- Added Scene lighting controls and shared authoring/automation/MCP validation,
  history, discovery and saving. Added editable Gallery104 and source-free export.
- Routed manual Timeline discontinuities through shared frame-history invalidation;
  preserved runtime camera-shot patches on cameras without animation tracks.
- Enabled the existing Timeline UI regression without requiring the Windows editor
  host. Local software/native checks remain separate from queued Windows and
  physical acceptance; native shutdown LSan remains explicitly open.


## Per-tile collision profiles

Tile Set definitions support Full cell, None and convex polygons with 3–8
normalized XY vertices. Full cells retain merged rectangles; None tiles remain
visible without contributing collision. Custom shapes follow cell flips,
transpose, signed scale and rotation through the existing Static/Kinematic Tile
Map body. Sensors, queries, outlines and cold recovery use the same geometry.
Concave decomposition, Dynamic Maps and custom-polygon one-way collision remain
unsupported.

The Tile Set Inspector provides a numbered diagram, editable points and explicit
Apply/Discard controls. Unapplied edits gate source operations. The typed
`asset.document.tile_collision` operation and its discovered MCP binding share
source validation, revisions, permissions, history and Undo. Legacy source1
fields remain opaque; explicit guarded promotion to source2 preserves identities.
Cooked1 remains readable; new cooks use version2. Selected Map/Set revisions stay
paired across publication, previews, copied Play definitions and runtime packages.

Original Gallery101 demonstrates full-cell floors, mirrored ramps and decorative
tiles. The manual includes authentic Inspector and software-Vulkan captures with
recorded provenance. The private physics snapshot version is10; rebuild project
modules with the matching SDK. Native C ABI table signatures and asset IDs are
unchanged. Local validation remains separate from Windows/full editor/MSVC,
physical GPU and graphical exported-game acceptance. No numbered delivery is
issued by this change.

## Temporal anti-aliasing

Scene lighting includes default-off temporal anti-aliasing with adjustable
stability, shared by Scene and Game views. Opaque and masked Mesh geometry supplies
previous object, camera, morph and skin positions. Per-camera depth rejection,
bounded histories and camera-cut resets preserve the existing render owners.
Current-frame layers keep their authored order; unsupported combinations use
ordinary rendering with a warning. New custom-surface cooks include a temporal
entry; existing surface capabilities remain readable.

The same Scene setting is available through automation and MCP with existing
validation, revisions and Undo. Explicit image captures wait for eight completed
samples and reset history between timeline samples. Gallery102 and the manual
demonstrate the controls and original animated thin geometry.

Local rendering, authoring and packaging checks pass. Three instrumented GPU
consumers finish their functional checks but report an unsuppressed 184-byte
shutdown allocation independently reproduced without FORGE linked; see
[dependency diagnostics](../../docs/dependency-known-issues.md#native-software-vulkan-shutdown-allocation-report).
Windows/Main/MSVC, FXC cooked surfaces, physical GPU and graphical exported-game
acceptance remain pending. No dependency upgrade, sanitizer suppression or
numbered delivery is included.

## Depth of field

Scene lighting includes default-off focus distance, falloff and maximum blur
controls, with Scene history, saving and the same automation/MCP operation.
The original spatial pass uses current HDR/depth independently for each camera,
preserves alpha and depth silhouettes, and runs after temporal Mesh resolve and
before current-frame layers and bloom. Unsupported depth, ordering, camera clears
or capacity retains ordinary rendering with a warning. Resized scratch owners
stage through preparation; inactive views release unused pairs.

Gallery103 contains original checker panels at near, focus and far depths. Actual
software Vulkan pixel references and Scene/Game views cover projection, camera
rectangles, disabled/zero paths, layer fallback, temporal capture readiness and
retained Scene picking. Source-free relocation and authentic controls/Game captures
are documented separately from Windows/full editor and physical GPU acceptance.
The native shutdown allocation report remains open and unsuppressed. No dependency
upgrade, installed SDK boundary change or numbered delivery is included.
