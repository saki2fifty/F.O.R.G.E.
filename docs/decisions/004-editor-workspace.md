# ADR 004 — Editor shell and workspace

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

## Decision

Keep Hierarchy left, Scene/Game or asset documents centrally, Inspector right,
Content/Problems/Console/Gameplay Code as a foldable secondary workspace. Persistent
status access restores the secondary workspace; preserve personal visibility/docking.
Only the active task owns Save/history. Commands are shared across menus, shortcuts,
palette/context actions; domain operations own validation and undo.

## Current evidence and implementation boundary

src/editor/document_workspace.hpp, workspace.hpp, interaction/tool controllers,
property_drawer.hpp and fresh Build62 captures. At960x600/200% the old bottom workspace
left a tiny Scene image; folding is a targeted correction, not a new document framework.

## Consequences

Inspector is selection-based, Content is asset discovery, asset editors are central
documents. Future graph/terrain/animation tools register here, not in permanent global
subsystem panels. Viewport gestures have one owner and explicit confirm/cancel.
Machine layout is not an authored DocumentId.

## Deferred work and exact trigger

Multi-selection, hierarchy hide/lock/type filters, Asset previews and document
maximization are deferred until their first domain consumer. Reserve selection sets,
per-viewport visibility/pickability and borrowed document adapters now; no external
binary editor-extension ABI is frozen. Maximization must restore the previous layout
and cannot rewrite project state. Bottom dock height remains ImGui-owned.
