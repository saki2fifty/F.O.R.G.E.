# 2026-10-25 — Illustrated manual and measured editor audit

Local source changes; the existing Windows Build 213 ZIP is unchanged.

## Using FORGE

- Organized all 65 manual guides by task, added an editor orientation guide and
  linked the previously omitted physics-loads example.
- Added 37 authentic FORGE screenshots across 33 guides. The repository README
  shares the same canonical image files. Capture provenance remains in technical
  documentation, separate from user instructions.
- Corrected outdated animation, terrain, audio, runtime and export descriptions.
  Removed development and acceptance narratives from the affected user guides.
- Added the Camera and Light setup needed to test the first scene during Play,
  and linked that prerequisite from the C++ cube walkthrough.

## Offline manual

- Added local PNG support to the existing standard-library renderer. Screenshot
  bytes are preserved and included in the existing package manifest and ZIP.
- Navigation follows the manual contents, highlights the current page and stays
  bounded on narrow screens. Screenshots fit the article and open at full size.
- Added regression checks for local images, escaping, containment, relocation,
  task ordering and ZIP image/manifest inclusion.
- Documented the ongoing manual and screenshot maintenance workflow in
  [Maintaining the user manual](../../docs/manual.md).

## Scene history and Content discovery

- Source publication uses the existing document registry to refresh every bound
  editor, including animation states, blend spaces, masks, timelines and editable
  meshes. Clean external reloads invalidate old MCP sessions; unsaved drafts and
  active gestures remain protected. Existing material preview and graph Save
  ownership are retained, and one reload failure cannot interrupt catalog updates.
- Shared import editors retain their previous settings/selection after a source
  refresh error and protect pending Save requests from background refresh. The
  publishing importer skips a redundant reload of its own accepted result.
- Reviewed source Move/Delete notifications use the registered document owners,
  closing the editable-mesh editor after Delete and retaining existing relocation
  behavior for supported formats. Pending source gestures and Save/close requests
  block file review. Mesh Move/Duplicate retain their existing unsupported-format
  rejection; file publication, identity and backup rules are unchanged.

- Scene history shares immutable snapshots when preparing Undo/Redo, prefab
  replay and schema publication. The existing 100-entry cap and transactional
  replay remain; rejected edits and external conflicts retain the pending history.
- Content reads scene JSON once, shares schema admission with explicit scene
  registration, and validates/merges discovered records through the catalog's
  existing bulk API. Registered metadata and dependency edges take precedence.
- Moved Content discovery into a portable unit and migrated its native-only
  checks. Added coverage for history limits, conflict repair/retry, parsed source
  snapshots, last-good discovery, registered metadata and cancellation.
- C++ discovery shares traversal bounds between Content and MCP. Background
  cancellation reaches the C++ phase and empty scans; failed C++ discovery retains
  the previous complete list while independent scene discovery can proceed.
- Ten focused portable owner/consumer test targets pass. The matched Linux VM
  comparison measured less time and transient memory for deep history and less
  scan time for 100 scenes; the one-entry history case was slower. Full workloads,
  values and limitations are in [performance evidence](../../docs/editor-performance.md).

Dependency pins, native SDK ABI, persistence formats and MCP/automation operation
schemas are unchanged. Native captures come from accepted prior Windows/Linux
validation. The changed C++ implementation has portable validation; native Windows
editor execution and physical acceptance remain pending. No Windows binary or ZIP
was relabelled or rebuilt for this local audit.
