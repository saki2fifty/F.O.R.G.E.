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

## Phase 8 Windows81 probe fixture construction

### Bug

`native_sdk_workflow_probe` compiled and ran 124/125 editor tests
green on Windows81 (run36204593880). The only failing assertion
was the synthetic ambiguity case (`tests/editor_sdk_workflow_probe_tests.cpp`
case (8), message: "ambiguous documents: ui_model_from_snapshot
must return empty object, not first-match an unrelated model").

The implementation of `ui_model_from_snapshot` was correct: with
two visible documents it returns an empty object via the
`ambiguous` flag. The fixture itself was broken. Case (8) (and the
case (9) hidden-unrelated variant) inserted the unrelated document
with:

```cpp
snap["documents"].insert(snap["documents"].begin(),
                         {{"entity", "..."},
                          {"visible", true},
                          ...});
```

The double-brace initializer `{{"k","v"},{"k","v"},...}` resolves
to `nlohmann::json::array::insert(iterator, std::initializer_list<json>)`,
not `insert(iterator, json_object)`. The compiler treats each
`{"k","v"}` as a single json value; `{"k","v"}` is itself a
brace-init for a 2-element json *array*, so seven 2-element JSON
arrays were appended to `documents` rather than one JSON object.
`ui_model_from_snapshot`'s `if (!doc.is_object()) continue;` filter
silently skipped those arrays, the documents array still held only
the reference document, no ambiguity was exercised, and the
implementation correctly returned the reference document's
non-empty model. The assertion
`model.is_object() && model.empty()` therefore failed on
`model.empty()`. The case was not a real regression test; it was
a fixture-construction mistake that masked itself as an
implementation failure.

Case (9) was symmetric: its hidden-unrelated document was also
inserted as JSON arrays, the test "accidentally" still passed
because the function still saw exactly one visible document.

### Fix

Cases (8) and (9) now build the unrelated document as an explicit
`nlohmann::json` object first, then insert that single value:

```cpp
const nlohmann::json unrelated_visible = {
    {"entity", "00000000-0000-0000-0000-000000000000"},
    {"visible", true},
    {"model", {{"page", "editor-toolbar"}}},
    ...};
snap["documents"].insert(snap["documents"].begin(), unrelated_visible);
```

This routes through `insert(iterator, const json&)` and lands one
JSON object in `documents`.

Case (8) additionally asserts the fixture shape before exercising
`ui_model_from_snapshot`:

* the unrelated document is an object with `visible=true`;
* the documents array holds exactly two entries after insert;
* a visible object carrying the unrelated entity id is present
  in the array.

A future change that re-introduces the brace-init-as-initializer-
list trap fails one of these `require()` calls first, with a
message that points at the fixture, not at the implementation.

### What was not changed

* `tests/editor_sdk_workflow.hpp::ui_model_from_snapshot` — already
  correct.
* `tests/editor_sdk_workflow.hpp::find_live_visible_context` and the
  `presenter_alive_observer_` gate — pre-init safety preserved.
* `tests/editor_sdk_workflow.hpp::ui_model`, `model_page`,
  `model_interactions`, `model_message`, `model_binding`,
  `player_position`, `scene_asset` — remain private; only
  `ui_model_from_snapshot` is exposed as a test seam.
* Authored scene ids, the runtime, the SDK, the renderer, the
  RmlUi host, and the menu/level/editor authored fixtures are
  unchanged. No authored id was rewritten.
