# 2026-09-24

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
