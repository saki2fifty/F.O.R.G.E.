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

## Editor validation

- Registered existing editor fixtures for mesh refinement, material graph geometry,
  Navigation Agent progress/avoidance, particle appearance and Prefab member
  picking. The portable owner-test configuration and both full Windows acceptance
  routes include these checks.

## Using FORGE

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
