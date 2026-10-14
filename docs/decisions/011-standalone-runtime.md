# ADR 011 — Standalone visual runtime

## Simulation-rate checkpoint — 2026-10-14

The existing runtime/session clock owns a transient finite 0..4 simulation factor.
Editor Play, scoped C++ game requests and host-reserved Runtime UI controls forward
policy to that owner. Fixed physics/Flecs delta remains unchanged; control/UI/device
time stays unscaled. Zero is a motion hold, separate from Pause and its physical
release policy. No editor, alternate clock, timer manager or saved Scene property
is introduced. New processes use normal speed; same-session Scene activation
retains the factor. Current behavior is specified in [runtime timing](../runtime-timing.md).

## Implementation checkpoint — 2026-09-23

Phase7 Game rendering uses authored Camera components rather than copying the editor navigation camera. The shared visual renderer and cooked-resource relocation tests are implemented; the full standalone exporter remains future work. See [camera/light contracts](../cameras-lights.md), [Play mode](../../manual/editor/play-mode.md) and [runtime content](../../manual/editor/runtime-content.md).

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Keep simulation independent of ImGui and editor authoring. A future standalone
visual host composes the same EngineContext/runtime clock, SDL window/input, Diligent
renderer, RmlUi presentation, audio and loaded asset services. Editor Game and standalone
reuse presentation/data contracts; the editor process never becomes gameplay authority.

## Current evidence and implementation boundary

forge_runtime currently runs isolated/headless simulation; editor Game renders
its presentation snapshot using an editor-owned camera copied at Play start. Existing
RmlUi presentation library already excludes editor UI dependencies. A production
visual executable/exporter does not yet exist.

## Consequences

Game Camera is a future authored component, separate from Scene navigation.
Packaging resolves startup scene/dependency closure and chooses a build profile.
A headless dedicated-server host excludes window/renderer/audio presentation without
forking game logic. IPC exists for editor hosting, not as a mandatory shipping loop.

## Deferred work and exact trigger

Implement visual host after the first camera/mesh/material/resource slice can draw
a relocated game. Reuse tested target boundaries; do not link ImGui or forge_authoring
into the shipping host. Shipping limits diagnostics and excludes compiler/import tools
unless a declared runtime feature requires them.
