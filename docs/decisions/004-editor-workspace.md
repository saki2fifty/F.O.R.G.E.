# ADR 004 — Editor shell and workspace

## Implementation checkpoint — 2026-09-23

Content list/tiles/search, model/material/texture/shader documents, typed pickers, multiple entity selection and hide/lock controls are implemented. See [editor UI guidelines](../editor-ui-guidelines.md) and the [manual](../../manual/README.md). Public editor binary plugins and future specialized graph/modeling tools remain outside Phase7.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

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
