# ADR 011 — Standalone visual runtime

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

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
