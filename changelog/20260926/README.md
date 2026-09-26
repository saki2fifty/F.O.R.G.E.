# 2026-09-26

## Phase 8 UI acceptance fixture selection

### Bug

The previous `tests/editor_sdk_workflow.hpp` fixture hard-coded
`reference::ui_id` as the UI document selector. The level scene's
Game UI entity carries that id; the menu scene's Main menu UI entity
is authored with a per-regeneration UUID. Filtering snapshots by
`reference::ui_id` therefore read the menu scene's published model
as empty and the SDK acceptance fixture timed out at the first
stage.

### Fixture fix

`ui_model_from_snapshot` now selects the active reference UI
document by the snapshot's own descriptor fields using a "single
visible document" rule:

* 0 visible documents -> empty model (snapshot not ready, e.g. mid
  scene-swap or before the first publish).
* exactly 1 visible document -> that document's model (the contract
  for the authored reference fixture, where each scene carries
  exactly one UiDocument).
* 2+ visible documents -> empty model (ambiguous; a future scene
  that legitimately carries more than one visible reference
  document is surfaced explicitly instead of silently narrowed).

No authored-asset id is rewritten. Runtime snapshot selection, the
authored reference project, and `tests/reference_project_fixture.cpp`
are unchanged.

### Regression coverage

`tests/editor_sdk_workflow_probe_tests.cpp` exercises the same code
path through the static `ui_model_from_snapshot` seam with
synthesized snapshots:

* menu scene snapshot (arbitrary entity id) -> page="main";
* level scene snapshot (canonical `reference::ui_id`) ->
  page="play" with non-zero interactions and a real prompt;
* absent / malformed snapshot -> empty model;
* only-hidden document -> empty model (no silent fallback);
* unrelated visible document alongside the reference one -> empty
  model (ambiguity, no first-match narrowing);
* unrelated hidden document alongside a single visible reference
  document -> the reference document is read.

The binary also covers the existing Windows79 Rml safety probe
(absent/false `alive_probe` returns `nullptr` without touching
RmlUi). CTest target: `native_sdk_workflow_probe`.

### Other

`forge::test::EditorSdkWorkflow::ui_model_from_snapshot` is exposed
as a narrow public seam so the probe binary can call it. The
surrounding helpers (`ui_model`, `model_page`, `model_interactions`,
`model_message`, `model_binding`, `player_position`, `scene_asset`)
remain private.
