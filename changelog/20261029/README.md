# 2026-10-29 — Collision profiles, temporal anti-aliasing and depth of field

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
