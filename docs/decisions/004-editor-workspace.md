# ADR 004 — Editor shell and workspace

## 2026-10-03 Content and C++ navigation amendment

Content is the single project browser for AssetId-backed assets and supported C++ files under a friendly Code location. C++ files retain project-relative `Native/` paths and existing source-editor ownership; displaying them in Content does not register them as assets. The former Gameplay Code panel is labeled Build, retaining its `###Native` dock identity and `panels.build` preference. Asset selection and code-path selection use the existing editor selection and active-task boundaries. The original 2026-09-19 default-tab wording below is historical.

## Phase 9 source checkpoint — 2026-09-27 (delivery pending)

Entity multi-selection extends EditorSelection; it does not introduce another
selection authority. Existing single-target writers replace the selection.
Hierarchy ranges follow visible row order. Content/document selection clears entity
selection. Common reflected property writes and transform gestures reuse the existing
candidate-before-commit authoring batch and scene Undo owner (128-command bound).
Spatial roots normalize gesture deltas; structural roots normalize duplicate/delete.
The manual defines individual-root pivot behavior and remaining single-target controls.
The earlier checkpoint incorrectly claimed entity multi-selection: the inspected
Hierarchy and EditorSelection still held one entity. That claim is corrected below.

## Implementation checkpoint — 2026-09-23

Content list/tiles/search, model/material/texture/shader documents, typed pickers, Content multi-selection and hide/lock controls are implemented. See [editor UI guidelines](../editor-ui-guidelines.md) and the [manual](../../manual/README.md). Public editor binary plugins and future specialized graph/modeling tools remain outside Phase7.

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
