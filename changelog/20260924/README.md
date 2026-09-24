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
