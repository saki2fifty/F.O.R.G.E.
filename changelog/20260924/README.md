# 2026-09-24

## Delivered — Build 260924-000070

Source `b7f08ba5f18d268d03d5eb46a4004a7411cbab35`,
[successful validation and package run](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35952645128).
This is the combined Phase8 standalone/export, Collision asset and reusable character
foundation delivery. Earlier checkpoint notes below preserve the validation history.

- Seven Collision families: Box, Sphere, Capsule, Cylinder, Convex Hull, static
  Triangle Mesh and Compound; explicit Mesh-derived recipes, safe publication,
  last-good retention, runtime loading and source-free export/relocation.
- Reusable Jolt CharacterVirtual mechanics: grounding, slopes, stairs, jump requests,
  translating/rotating platforms, bounded pushing, checked placement, crouch/stand,
  filtering, private recovery and exact-SDK access. No FPS movement keys are added.
- Central Collision editing, independent source history, typed asset assignment,
  named collision layers and optional selected-object collision/character guides.
  User manual and technical contracts describe the supported workflows and limits.
- Linux core76/76 (135.60s), shared SDK88/88 (74.65s); Windows core76/76
  (375.45s), shared SDK88/88 (118.09s). Windows editor controllers2/2 (21.83s),
  broader editor/render/asset suite123/123 (469.94s), shared standalone6/6 (25.90s)
  and fresh-machine relocation passed. The361-step editor and76-step physics
  workflows passed; final assignment and grounded-character captures were reviewed.
- Retained strict sanitizer evidence covers collision admission/publication,
  resource lifetime, character/physics mechanics and the focused preview corrections.
  The separately documented Flecs managed-include exception is unchanged.
- Download verification passed build/source identity, ZIP CRC,777 outer hashes,
  255 SDK hashes, four x64 executable headers and42 matching offline manual pages.
  SHA256: `a6214ee1d24cd32bcc6b7d20d1dcb9f4c53bd1d117a84c6ee0b973dddf100887`.
- Package staging and duplicate extractions were cleaned; the prior delivered ZIP
  was archived. Builds67–69 failed validation and were never delivered.
- Physical GPU/display/audio checks remain separate. Phase8 input/reference-game
  integration remains unfinished; Phase9 has not begun.


## Collision and reusable character foundation — verification in progress

- Continued full core/shared-SDK and Windows input validation of internal checkpoint
  3da9436. Build 260923-000066 remains the delivered editor; no new numbered ZIP.
- Initial core run passed72/76. Two localhost tests were blocked by the Linux
  sandbox and passed2/2 when rerun with socket access. The remaining findings are
  an outdated reflected-component count and a reproduced resource-eviction race;
  corrections and full acceptance are still pending.

- Corrected idle resource eviction to ignore completed terminal jobs without pumping
  or publishing replacements. Its regression now observes failure before an owner
  pump, making the former timing window deterministic.
- Updated builtin schema count for Cylinder Collider, Asset Collider and Character
  Controller. Shared SDK checkpoint passed87/88; only this schema assertion failed.
- Added Collision recipe imports to the existing asset CLI using the same validated
  importer/publication owner as the editor. Added a reusable acceptance-project
  generator importing real glTF geometry and all seven collision families.
- Extended controller tests for moving-platform entry/exit, paused single stepping,
  resume, recovery and unload. Added a 400-static-body,100m-level measurement mode.
  Rebuilt validation of these changes is pending.

- Corrected a Windows/MSVC overload ambiguity comparing JSON with the collision
  Jolt revision string. The revision remains unchanged; the constant uses a character
  array compatible with the existing JSON comparisons.
- Rebuilt focused core/resource/physics/collision pipeline passed4/4(19.57s);
  deterministic failed-load eviction passed30 consecutive runs.
- The generated 20-body acceptance level imported real glTF Mesh geometry, cooked
  all seven collision families, carried/jumped/recovered its character on a moving
  platform, and repeated successfully after source-free package relocation.
- Initial Debug workload:400 static bodies over100×100m,120ticks4.323s,1000rays26.16ms,
  200sphere sweeps7.15ms. This is a concurrent-build Linux Debug measurement, not
  a Release performance claim. Release and Windows visual validation remain open.

- Added a dedicated actual-input Windows physics-level capture workflow, covering
  stairs, slope limits, moving-platform geometry, imported/convex/compound collision
  and the character's visible reference envelope and grounded Play state. The
  generator creates ordinary admitted source assets/scenes; screenshots remain
  pending until the Windows run succeeds and the images are opened.
- Release Linux workload:400 static bodies,120ticks647.19ms (5.39ms/tick average),
  1000rays3.88ms,200sphere sweeps0.89ms; realization80.78ms. These are host-specific
  measurements, not a universal performance guarantee.

- Added the reusable physics-level workflow to Windows editor audits and package validation. It captures imported/compound collision, slopes, steps and the character reference shape through real editor input. Execution and image review remain pending.
- Mesh-part selection now clears stale revision-bound ordinals and requires explicit review after source changes. Recovery rejects mismatched character orientation and nonfinite velocity.

- Final checkpoint Linux core76/76, shared SDK88/88 and strict collision/physics/resource sanitizer4/4 passed. The generated acceptance-level orchestration passed, including cooking and source-free relocation. Windows validation remains pending.
- Reconciled earlier physics documentation and manual text with the implemented compound, character, named-layer and selected-collision workflows.
- Added a regression for revision-bound Mesh part selection: a stale source revision must report the mismatch while preserving the published catalog and retained collision shape.
- Updated Content, asset-format, packaging and SDK documentation for Collision and character/query callbacks, including paused replacement boundaries. Added rotated/nonuniform compound scale regressions against the pinned native validator.
- Corrected another MSVC JSON comparison in collision revision polling by comparing explicit string/integer values. Extended the Windows input workflow to Mesh-part choice, convex publication, compound editing and Undo; shape labels now use readable names.
- Kept acceptance-fixture Python bytecode out of the source checkout; test outputs remain in the external build/evidence directory.
- Windows core compiled and passed75/76 tests; relocation exposed a test-owned writer lock still open during project rename. The fixture now releases documents/importer/resources before moving the project, matching the required Windows lifecycle. Product writer-lock semantics are unchanged.
- Windows actual-input authoring passed339 steps, including collision creation, explicit Mesh-part choice, convex publication, compound editing and Undo. The physics-level captures exposed an asset-backed preview worker crossing a resource owner-thread boundary. Preview preparation now receives an immutable native shape snapshot; resource thread checks remain intact. Added all-seven-family worker extraction after resource retirement, before and after package relocation, and actionable preview timeout diagnostics.
- Made the internal physics preview header self-contained for its new direct test consumer. The new Windows follow-up remains pending; no numbered package has been released for this block yet.
- Collision-document publication now refreshes the viewport's catalog immediately, matching other asset documents. Extended actual-input acceptance to assign the newly published Collision through its typed Inspector picker and display its Scene preview without reopening the project.

- Reconciled the status page with the accepted export/relocation block and removed an obsolete Phase7 source-only notice from the Content manual. Collision/character Windows delivery remains under verification.

- Updated the accompanying manual to cover the runtime/export/collision additions together, retaining an explicit boundary for older Build66. Standalone instructions now lead with normal editor export rather than requiring an end user to compile a host.

- Windows audit35944038189 atca4c476 passed339 general editor input steps,76 physics-level input steps and standalone-only startup. Retrieved and opened imported triangle, convex, compound, character-envelope and grounded-Play captures; the preview ownership correction is visibly verified. The hosted runner reports unavailable audio and continues silently; no physical audio claim is made. Final package validation also includes the newer direct-publication assignment regression.
- Build260924-000067 was reserved but not delivered: the full editor process suite found an old palette test that assumed every asset action was available for a Model. Corrected the test to verify disabled Model collision creation, enabled Mesh collision creation, and Play blocking through real palette input. Production action availability is unchanged. The identifier remains consumed.
- Selected collision preview now follows the enabled physics representation when an entity retains both a Body and Character component. A disabled Character no longer masks an active Body; an active Character does not load an unused disabled Body's Collision asset. Both enabled representations still report their configuration conflict. Added native shape-selection regression coverage; simulation authority and persistence are unchanged.

- Clean Windows package builds now build the graphical game target before installing its runtime kit; installation no longer depends on a previously cached executable.

- Retain editor input-workflow state alongside Windows captures and report picker target identity/clipping on assignment timeouts, so a failed interaction can be diagnosed without relying on screenshots alone.

- The queued-input fixture scrolls a selectable row only when its click center is clipped. Exact ImGui source and a native geometry probe showed that first-row hit padding always extends above a child clip edge, despite a visible click target. This corrects the new Collision picker acceptance timeout without changing normal picker behavior.
- An unassigned Asset Collider now shows an actionable preview message asking the user to choose a Collision asset, instead of a JSON conversion error.

## Phase8 input/reference game — source integration in progress

- Extended the existing input authority with version2 context declarations,
  deterministic priority, consumption/pass-through and neutral context changes.
- Added candidate binding replacement, context-aware conflict queries and input
  listening with cancellation, opening-button suppression and noise filtering.
- Added optional radial stick deadzones and Escape as a standard control.
- Added focused context, binding, capture and numeric-edge regressions; validation
  is pending. Standalone menu/SDK/reference-game integration is not complete.
- Build260924-000070 remains the delivered package; no new numbered build.

- Added explicit active-world control frames for paused menus, independently
  consumed control-phase action snapshots and exact-SDK control callbacks.
  Reentrant session changes reject and callback faults stop further execution.
- Focused rebuilt Linux core-services and game-foundation tests passed2/2(4.35s).
  Exact-SDK and graphical-host checks remain pending.
- Shared SDL gamepad ownership now serves editor and standalone, with deliberate
  active-device switching, switch debounce and neutralization on disconnect.
  Virtual-device regressions have been added; execution remains pending.

### Phase 8 input/session integration — source work in progress

- Added world/module-scoped game request services, deferred host execution,
  native save-schema leases and revocation, and copied exact-SDK control queries.
- Connected real session pause/transition, save/load/slot metadata, persistent
  rebinding/settings, cursor requests and live master audio volume.
- Added candidate scene-initialization and explicit game save-restoration callbacks
  before admission; neither advances simulation or receives gameplay input.
- Added explicitly registered bounded UI value actions and controller navigation
  through native RmlUi focus behavior.
- Started a separate reference-game SDK consumer, input map and RmlUi menus.
  Reference-game, standalone visual and relocated acceptance are still pending.
- Focused Linux input/control/session/storage/UI/audio checks passed 6/6 (9.76 s)
  before the subsequent navigation/reference additions. No new numbered package.

### Reference gameplay and native input acceptance

- Added an exact-SDK reference project with game-owned FPS controls, ray interaction,
  main/pause/options/HUD/loading screens, explicit save state, scene requests and
  controls rebinding. Engine code supplies services, not FPS/menu policy.
- Added an editable acceptance level built through ordinary collision imports,
  skinned-model placement, spatial audio and navigation baking. Its combined
  headless check observed navigation movement, admitted animation, nonzero offline
  audio, scene round trips and restored interaction state.
- Added explicit relative-mouse capture to editor Game input. Pointer mode retains
  runtime UI interaction; native Escape/focus loss release relative capture.
- Added native Windows reference-game input/capture, source-free relocation and
  relaunch workflows. Execution/visual acceptance is pending; these fixture additions
  are not evidence that the Windows workflow has passed.
- Updated the input manual and added the reference-game guide. Offline manual checks
  pass. RmlUi first keyboard/controller focus now enters a document deliberately.

### Gameplay verification follow-up

- Rebuilt local core 78/78, shared SDK 91/91 and portable editor/input 3/3 pass.
  Local socket-only regressions were rerun with socket creation permitted after
  sandbox rejections. Strict sanitizers and Windows acceptance remain pending.
- The reference camera eases to the controller's accepted crouch height, including
  blocked standing behavior. The rebuilt headless camera regression passes.
- Extended queued host tests for candidate cancellation, rebinding clear/defaults
  and corrupt-save preservation. Added Windows visible error workflows for corrupt,
  newer, unavailable-scene and disappeared-content loads; execution remains pending.
- Added headless fixed/control/transition timing records and hosted menu frame
  measurements. These are measurements of the stated workloads, not physical
  mouse/controller latency or unrestricted game performance guarantees.
- Project Settings now authors input contexts, priority, consumption, initial state
  and fixed/control phase; actions can select a context. New actions in a version2
  project receive a valid initial context. Analog digital thresholds and radial
  stick deadzones are exposed. Added native context-authoring captures; pending.
- Radial bindings now account for the perpendicular stick sample in routing,
  conflict discovery and digital threshold edges, including one-axis actions.
  Added regressions for this integration boundary.
- Checkpoint93b8efb passed core78/78 and exact SDK91/91 on both Linux and Windows.
  Strict Linux core suite78/78 passed with the existing Flecs upstream-include
  exception separately classified. Shared SDK sanitizer subset4/4 passed; the
  subsequent radial regression also passes strict sanitizers. Graphical acceptance
  and the final numbered delivery are still pending.
- Added a visible corrupt-preferences recovery check: project defaults are used,
  the menu explains the problem, and the original preferences are preserved.
- Settings apply only to changed subsystems. Volume/sensitivity updates retain held
  input; VSync changes no longer reapply SDL window placement. Added a regression.
- Updated the older standalone package test to the authorized corrupt-preferences
  recovery contract: defaults, logged explanation and preserved source bytes.
  The first Windows audit had correctly recovered and failed the obsolete
  abort-startup expectation; no reference-game visual acceptance was claimed.
- Reference acceptance now retains a second production export from the normal
  runtime kit. It is relocated and startup-verified without source content and
  contains no instrumented fixture executable; the input-test package stays separate.

- Windows reference startup exposed missing authored translations on the generated
  menu camera and sun. Corrected both and added headless render/camera admission
  assertions before the longer gameplay acceptance sequence.
- Standalone graphics now drain pending Diligent commands on exception cleanup,
  including rejected startup candidates.
- Restored the editor benchmark target after the standalone-only build check;
  that check intentionally disables editor targets and clears benchmark injection.
- The combined Windows package includes the independent production reference game
  under ReferenceGame. Assembly verifies exact runtime/build provenance and every
  file hash; final relocation also checks its graphical startup.
- Relaunch acceptance waits for queued save-slot discovery before operating
  Continue; the first rendered startup frame legitimately precedes that receipt.
- Extended native RmlUi navigation checks across every pause-menu operation and
  the Options controls, including scrolled rebinding actions.
- Actual Windows menu capture exposed missing RmlUi block styles: native RmlUi
  defaults elements to inline. Added explicit block layout and responsive bounded
  menu cards, with geometry regressions at 1280×720 and 640×480.
- Restricted the old cube-only capture contrast assertion to its original fixture;
  the reference game uses its own menu/input/geometry and visual acceptance.
- Editor acceptance now scrolls to and captures the active/default and new inactive
  context policies, rather than only capturing the section header.
- Documented RmlUi's inline layout defaults and explicit container styling in both
  runtime UI technical documentation and the user manual.
- Added queued gameplay-service regression coverage for successful migration,
  migration-callback failure preserving file/world, multiple slots and isolated deletion.
- Continue appears only after a valid save is found. Generic RmlUi button elements
  do not implement HTML disabled attributes; native binding/focus tests now check
  that Continue is absent without a save and reachable when one exists.
- Corrected Options scrollbar styling: the default auto-width scrollbar consumed
  the content width. A regression now rejects narrow button content at both tested
  resolutions, in addition to checking the outer card and keyboard navigation.
- Long runtime diagnostic paths now wrap within menu notices using native RmlUi
  word breaking; missing-content errors remain readable without horizontal clipping.
- Corrected a native regression found during final acceptance: automatic Collision
  source refresh must not force parent-window focus and close an active Shape menu.
  The input workflow now changes the source with that menu open, waits for actual
  reimport, then selects Compound and checks the resulting child and Undo.
- The cold Windows editor build plus full validation now has a 90-minute budget.
  Successfully compiled outputs remain reusable after a later test failure; this
  does not bypass any build, test, provenance or package gate.
- Fixed a worker-output inspection race exposed by Linux UI packaging validation:
  an atomic rename could remove an enumerated temporary file before its metadata
  was read, incorrectly rejecting a valid import. Ignore only that missing entry;
  preserve all output and execution limits and the completed-output scan.
  A minimal repeated-rename regression failed before the correction. Both affected
  worker and UI-package tests passed ten repetitions afterward.
