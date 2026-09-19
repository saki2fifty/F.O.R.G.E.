# September 19, 2026

## Dependency evidence policy

- Established a permanent policy for every external dependency: exact pinned
  source/configuration and matching tests take precedence over live documentation.
- Require version/commit/options, feature assumptions, documentation drift,
  compatibility implications and verification dates in dependency records.
- Retain Flecs4.1.6 at `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`;
  `on_validate` and native Meta maps are explicitly unavailable at this pin.
  No backport, dependency upgrade, or automatic range-mutation veto is claimed.
- This policy change precedes the ongoing Flecs integration; it is not a new
  packaged build or a claim that the integration is complete.

## Editor architecture and UX refinement — implementation

- Compact menu/global/Scene tools with dependency-free vector icons; Preferences under Edit, scene identity/dirty marker in its stable tab and one-row status with overflow Details.
- Shared entity recipes across Scene/Hierarchy/Entity menu/palette, explicit placement and one Undo per create. Empty/nonvisual recipes preserve old implicit cube behavior through explicit None.
- Added 16 bounded procedural shapes using shared geometry for rendering/picking/navigation. Original kinds 0–3 retain their geometry. No imported mesh/material pipeline.
- Left-to-right property layout, grouped schema Add Component, conditional attached filter, Transform utilities overflow, Spatial binding wording and collapsed Copy ID.
- Internal document/asset-open adapters retain independent Save/history/inspection ownership. Prefab source initially docks centrally; existing custom layout persists.
- Added standing editor placement guidelines and synchronized current user manual. No future specialized editor or Phase 7.
- Focused and final clean Windows/Linux/SDK/sanitizer/render/package evidence will be recorded after execution. No new numbered delivery yet.

- Focused validation: fresh Linux core **32/32**, fresh portable editor/input **3/3** (including all four shared creation-menu contexts and both placement modes), shared-main syntax, manual **3/3**, and formatting passed. Added palette keyboard/disabled-action, document capability, recipe/geometry and shared creation-menu regressions. Final clean SDK/sanitizer/Windows evidence remains pending.


## Final validation and delivery — Build 260919-000061

- Source `a000ff530d6b77facdec6dbbb2399d1753a52ce8`; clean workflow **35412721915** passed all six jobs. Windows/Linux core **32/32** and exact SDK **41/41**, Windows editor controllers **2/2**, renderer/subsystems **33/33**, shaders, D3D12 WARP and relocated runtime UI/font/navigation/converter checks passed.
- Fresh local core/SDK **32/32 + 41/41**, ASan/UBSan/LeakSanitizer **32/32 + 40/40**, and normal/sanitized editor **3/3 each** passed. Local sanitizer converter tools required the GCC runtime-library search path; no sanitizer checks were disabled. Manual **3/3**, format and shared-main syntax checks passed.
- Reviewed **19 actual-editor captures** and **17 appended geometry captures**. **38/38 previous viewport/runtime UI fixtures remain byte-identical** to Build 60. At 1440×900/100%, menu **52→28 px**, global toolbar **52→32 px**, status **40→24 px**, Scene internal top area **100→64 px**: **96 px** less combined chrome.
- At 960×640/200%, transform tools remain available by wrapping and status stays one row, but viewport space is very limited and some Hierarchy/hint content clips. Use a larger window, resize docks or lower zoom. This stress capture is not a claim of comfortable full-editor usability. Physical GPU/DPI/input/readability acceptance remains for user review.
- ZIP CRC and **180 manifest hashes**, compiled build/source identity and matching manual edition verified; exact SDK archives verified against **237 Linux / 246 Windows** hashes. Delivered `260919-000061-FORGE-Windows-x64.zip`, SHA256 `17dd5f02e1c0223df159fc653e571e17889075c466c8dc439c21de4586a1237f`.
- Archived Build 60; removed verified duplicate download/extraction staging. Packages contains only the latest ZIP and editor folder.
- This documentation-only follow-up corrects the prefab how-to's old **Member space / FollowStructure** labels to **Spatial binding / Follow parent**. The already-built offline page retains that old wording in step 4; it refers to the same control. Package bytes and compiled source remain unchanged.
- **Stopped before Phase 7.** No future specialized editor, Mesh/Material pipeline or public editor-extension ABI was implemented.

## Flecs integration work in progress

- All built-in authored types explicitly register Flecs member entities, Doc names/help, Units and numeric ranges. Shared schema/Inspector/Add Component consumers derive their presentation from that metadata.
- Scalar subsystem admission uses the reflected ranges while preserving cross-component validation. Advisory gain guidance does not reject supported amplification.
- Hierarchy ordering, fixed-clock timer integration and persistent transform queries are undergoing regression validation; no new package is delivered by this entry.
- Added dependency source-of-truth and Flecs ownership documentation. Exact 4.1.6 pin retained, with the approved unavailable stable APIs recorded explicitly.

### Flecs integration — additional source changes (not yet packaged)

- Added native enum-backed Primitive/PhysicsMotion choices, per-type member metadata, units, hard/advisory ranges and shared validation feedback. Preserved actual float32/float64 precision in detached edits.
- Added ordered sibling authoring with Shift-drag, Undo/Redo and persistence. Prefab source member ordering propagates through validated candidate publication; commit permutations are prepared before the durable write. Root order uses an internal native membership parent without changing authored parent fields.
- Added optional Tools → ECS inspection: native queries, component insertion, result selection/copy, entity/world JSON, statistics, native alerts and a read-only loopback REST/Explorer connection. Diagnostic output is kept separate from headless JSON output.
- Added AssetId-backed Flecs Script source documents, independent Save/close ownership, search, bounded managed preview workers, project-contained includes, Script Math, errors and Problems integration. Preview evaluation does not alter the authored scene or play world.
- Admitted native timers/rate filters beneath fixed ticks and retained owner-thread external subsystem scheduling. Script Math is part of the exact SDK fingerprint.
- Added focused tests for metadata/validation, ordering and publication failures, native toggles/sparse storage/queries/monitors/events, fixed timers, Script grammar/includes/reload and read-only REST lifecycle. Focused core/authoring/prefab, Script, REST and manual tests pass on Linux; final profile/Windows/sanitizer/package verification is still pending.
- Recorded measured storage/threading tradeoffs and stable query limitations. Updated the integration contract, extension documentation, editor placement guidelines and function-based user manual.

- Follow-up validation corrected native JSON member-order preservation and diagnostic addon registration order across worlds. Valid isolated imports and rejected replacement preservation now pass. Native Metrics exposes all four pinned kinds with validated sources and removal; statistics history uses native buffers and reductions. Range/generation, standalone App/Frame, metric values/history and multi-world activation regressions pass. Diagnostic writes no longer invalidate transforms unless they affect spatial inputs. Final package verification remains pending.


### Flecs validation follow-up — not packaged

- Kept the exact stable dependency and permanent pin-first evidence policy. No
  development APIs or upstream patches were imported.
- Stopped manually reducing empty authoring pipeline statistics; the pinned
  implementation triggers UBSan for that case. Native world/system histories
  remain sampled, and diagnostics do not advance gameplay. Focused core/ECS
  sanitizer regressions pass after this correction.
- Script workers now supply the top-level source buffer through the native code
  constructor, own an outer native log capture, and validate all included managed
  scripts. Added malformed-include and unresolved-component include regressions.
  Normal Script regressions pass.
- The included-file constructor still leaks an internal source allocation on
  failure in Flecs 4.1.6; the strict LeakSanitizer regression remains failing
  (18 bytes in the FORGE fixture). The later upstream correction is documented in
  `docs/flecs-integration.md`; no suppression or false clean-validation claim.
  The integration is not complete and no new Windows package has been created.

- Checkpoint verification: current normal Linux static-profile suite **34/34**
  passed, manual checks **3/3** passed, and formatting/whitespace checks passed.
  These do not resolve the explicitly failing native include LSan case or replace
  the pending final Windows/shared/sanitizer/editor delivery gates.


### Approved Flecs validation exception and final integration

- Retained native managed includes and the exact stable Flecs pin. The user-approved
  exception applies only to the known native filename-buffer cleanup path on failed
  managed includes. The minimal fixture leaks18bytes; the buffer can be larger.
- Added a separate strict-signature known-upstream regression, actual sanitizer
  evidence files, small/larger sources, rejection without publication, terminated
  workers, cleaned staging and subsequent successful evaluation. All unrelated
  sanitizer findings and unexpected clean results fail the check. No suppression.
- Shared the successful-preview publication boundary between editor and tests.
  Bounded source reads now enforce the limit during reading, including editor
  source views, and reject NULs in both candidate and prior source.
- Added source navigation from Script diagnostics and Problems, preserving active
  drafts and opening included files read-only. Native warning severity displays
  consistently. Added actual-editor ECS/Script WARP capture cases and tab/source
  navigation fixtures. Final validation/delivery is still in progress.
- ECS statistics now distinguish the inspected world's delta and native stages
  from the separate play clock and operating-system thread count; each status
  has contextual help.
- Local normal static/SDK suites passed **35/35 and 44/44**. Separate clean
  ASan/UBSan/LSan profiles passed **34/34 and 42/42**, plus **3/3** portable
  editor checks. The dedicated pinned-include regression produced the expected
  **18-byte and 4118-byte** LSan findings, matched only the approved allocation
  path, and verified rejection, worker exit, cleanup and recovery. No suppression;
  this is not an all-sanitizers-clean claim. Final focused checks, manual,
  formatting, workflow lint and portable main/fixture syntax checks passed.
