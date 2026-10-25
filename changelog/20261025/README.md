# 2026-10-25 — Illustrated manual and measured editor audit

Local source changes; the existing Windows Build 213 ZIP is unchanged.

## Project Code setup

- SDK compatibility errors show expected and detected toolchain values and the
  selected SDK configuration. Exact compiler, architecture, flags and CRT rules
  are unchanged.
- Windows module-kit setup reports the CMake 4.3 requirement before compilation;
  the install helper also checks the executable used for installation.
- Added configure-only admission checks and compiler troubleshooting instructions.
  The SDK guide describes the combined ZIP, distinguishes source-only references
  from installed guides and removes obsolete roadmap claims. Package validation
  checks links across all installed SDK guides.

## Recovery records

- Editor JSON reads reject trailing non-whitespace content or an extra JSON
  value. Binary file reads require physical EOF and reject raw terminators that
  could hide trailing bytes. Rejected named and untitled recovery keep the
  current scene and snapshot available for repair.
- The document lifecycle suite also runs in the portable editor-owner test
  configuration, with recovery rejection/retry and protection against saving
  over malformed external changes.

## Core file records

- Project, Scene identity, Prefab, catalog and Script worker file readers reuse
  complete-record parsing. Raw terminators cannot hide external edits or cause
  a partial file to be admitted for migration/publication.
- Added actual project Save, legacy Scene identity, Prefab publication and Script
  request regressions that retain source bytes and prior state on rejection.

## Asset JSON admission

- The shared bounded JSON parser rejects raw NUL bytes that could hide part of a
  captured source or metadata record. glTF capture and GLB relocation use this
  same check. Escaped JSON characters and binary resource bytes remain valid.
- Extended the existing robustness and glTF source suites with actual source capture and
  relocation rejection, valid padding and escaped-control cases.

## Editor validation

- Registered existing editor fixtures for mesh refinement, material graph geometry,
  Navigation Agent progress/avoidance, particle appearance and Prefab member
  picking. The portable owner-test configuration and both full Windows acceptance
  routes include these checks.

## Using FORGE

- Shortcut, Save/Undo and viewport guides consistently describe the active
  document and multi-selection controls. The Flecs integration contract labels
  its original delivery evidence separately from current ownership behavior.

- Project Settings keeps automation-created game defaults through the next editor
  frame and saves their application identity only on explicit Save. Native and
  automation review initialization share the existing owner. Discard clears stale
  errors; accepted close also cancels queued Save requests. Cancel retains failed drafts.

- Runtime dependency reloads keep the complete previous review on failure and
  contain read errors in Inspector. Successful Open/Discard clears obsolete save
  errors and operation IDs; a successful Save retains its own operation ID.

- Runtime Dependencies clears clean reviews and completed results when changing
  projects, including copied projects with identical asset IDs and a closed
  Inspector. Old automation reviews stay invalid; saved declarations reload from
  the current catalog. Dirty drafts and running saves retain existing switch guards.
- Reconciled the runtime-content reference's obsolete phase/delivery introduction
  with its implemented content packaging and standalone assembly contracts.

- Export Game clears previous-project results and errors after a project switch,
  including while closed. Same-project outcomes remain available, and a new export
  starts with an empty exported-game run log.
- MCP Play and exported-game log reads handle invalid or incomplete native text
  using the compiler text reader's shared conversion. Returned text stays valid
  JSON while original process bytes and compiler-page offsets remain intact.
- Timestamped compiler, Play and exported-game logs retain bounded recent output
  even when a program writes an oversized line without a newline. Truncated lines
  keep their timestamp and newest text with an explicit marker.
- Automation folder grants, project-parent and export/kit fields preserve complete
  UTF-8 paths from typing and native folder selection. Long choices no longer
  silently become truncated paths; existing filesystem and grant checks remain.
- Organized all 65 manual guides by task, added an editor orientation guide and
  linked the previously omitted physics-loads example.
- Added 37 authentic FORGE screenshots across 33 guides. The repository README
  shares the same canonical image files. Capture provenance remains in technical
  documentation, separate from user instructions.
- Corrected outdated animation, terrain, audio, runtime and export descriptions.
  Removed development and acceptance narratives from the affected user guides.
- Reconciled the Hierarchy guide and editor reference with implemented prefab
  Apply, multiple entity selection and active-document Save/history routing.
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

- Idle selection maintenance reuses the existing revision-aware scene snapshot
  and keeps intact selections in place. Stale-ID removal, primary ordering,
  legacy writes and Shift-click anchors retain their existing behavior.

- Frame selected builds its temporary structural lookup once for the whole
  selection. Overlapping descendants share one traversal; existing bounds,
  helper-position, cycle and admission behavior are preserved.

- Selection overlays reuse a temporary row lookup instead of rescanning the scene
  for every selected object. One-object drawing avoids index allocation. Geometry
  and selection order are preserved; owner tests and attributed CPU measurements
  are documented in the performance reference.

- Hierarchy membership and count queries avoid copying every selected ID per row.
  Inspector/status count checks share the same read-only query. Owning snapshots,
  legacy writes and selection behavior remain unchanged. Actual selection input
  and existing batch-edit/Undo tests cover the change; query-only measurements
  and their limits are recorded in the performance reference.

- Derived Cache clears completed results and errors when opening another project,
  including switches made while its window is closed. Same-project observations
  remain available. The actual UI regression checks the shared work guard,
  cancellation and new-project results without altering cache or authored data.
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
- Material Graph clears old-project Save requests and cancels preview preparation
  on project changes. Queued opens retain their originating project and resume
  after cancelled preview work drains, without reinterpreting a shared source path
  in another project.
- Navigation clears cached overlays and selection when the active scene or project
  changes, including copied projects with identical scene IDs and a closed
  Navigation panel. Reopening Navigation reloads the saved mesh without rebuilding.
- Registered the existing static-Mesh navigation UI regression with CTest. Both
  full Windows acceptance routes also select the new source publication,
  source-file and graph lifecycle checks through their shared test selector.

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
