# September 28, 2026

## Earlier Phase 9 acceptance hardening checkpoint

- The native editor multi-selection/Delete/Undo workflow passes all 399 steps.
- Final package acceptance exposed an SDK fixture readiness gap before menu
  rebinding. The fixture now waits for acknowledged input routing before
  Options/Rebind/key capture; failures retain routing and epoch diagnostics.
- Acceptance assertions and runtime input-reset semantics remain unchanged.
  A new numbered Windows gate is required; Build 105 remains delivered.

## Validated Windows delivery

- Build **260928-000109** passed all nine required CI jobs at source
  `9db10421e0b998ce45b8362e1d2c4b43660ef670`; three optional audit jobs were
  deliberately skipped. Final assembled editor/SDK acceptance passed, including
  rebinding, Save/Load and persisted binding use after process restart.
- Windows multi-selection input checks passed; Inspector, helper-aware framing,
  duplication and restored-scene captures were visually reviewed on D3D12 WARP.
- The numbered package was hash-verified and locally promoted. Physical GPU
  acceptance and interactive batch-transform feel remain user acceptance items.
  This completes the first Phase 9 bundle, not all proposed Phase 9 work.

## Phase 9 — gameplay onboarding and coordinated prefab Apply

- Added an installed-header C++ gameplay starter with explicitly admitted Gameplay Counter and a Flecs fixed system.
- Gameplay Code creates sources without overwriting files, builds through CMake/Ninja, prepares deployment kits, validates in an isolated worker and publishes only successful module declarations. Rich SDK changes remain restart-bound.
- Fixed SDK export to use the matching shared inspection worker and automatically select managed module kits.
- Added Apply instance overrides review, including dirty-scene save disclosure, cancellation, named-scene requirements and opaque-data preservation.
- Added coordinated prefab+scene publication and durable Scene Undo/Redo with monotonic revisions and external conflict rejection.
- Extended the existing private asset-file journal narrowly for the two authored files; recovery precedes scene activation. Existing catalog recovery remains supported.
- Added actual process interruption, candidate rejection, inheritance/history/conflict and native author-to-export acceptance coverage.
- Fixed prefab instantiation to select the new entity rather than leave asset selection active. Updated technical contracts, ADR017 and user how-to pages.

At this implementation checkpoint, Windows delivery/visual evidence was pending.
The completed gate is recorded below.

### Author-to-export acceptance correction

- Native input proved gameplay creation/build/rebuild/admission/runtime effect and Prefab Apply/Cancel/Undo/Redo.
- Export exposed a fresh-project gap: discovered saved scenes/prefabs were not always in the persistent catalog. Existing export preparation now registers only reachable validated authored identities, preserving AssetIds.
- Added fresh-scene/prefab packaging and duplicate-identity rejection coverage. Corrected fixture navigation to match compact Content menus and the actual Inspector placement button; assertions and timeouts remain unchanged.

## Phase 9 completion — Build 260928-000117

- Compiled source `efa67a2bb20c06e5d21e69cd0c2e4776810cb433` passed all required
  source build/test job roles in [run36440905658](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/36440905658).
- [Final package run36448103341](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/36448103341)
  passed the 24-transition reference-game workflow and 98-step native SDK onboarding,
  including real widget-driven Apply/Cancel/Undo/Redo and relocated export startup.
- Windows captures were inspected; package source/build identity and hashes were
  verified, the numbered ZIP promoted, Build109 archived and redundant staging removed.
- Exact C++ digest extraction fixes an MSVC overload ambiguity without dependency,
  format or compiler-option changes. Fresh authored-document export regression and
  strict ASan/UBSan/LSan checks passed after rebuilding the final source.
- Reference acceptance now precedes compiler-heavy onboarding. Both scenarios remain
  required with unchanged assertions/deadlines. Earlier loaded-run snapshot deadline
  misses remain recorded; their cause is unproven and no performance fix is claimed.
- Phase9 implementation and automated acceptance are complete. Physical hardware
  acceptance remains separate; reference-game FPS/smoothness feedback stays deferred.
  Phase10 has not begun.

## Gameplay authoring direction — planned

- User selected C++ as the sole gameplay programming direction and requested a
  C++ source editor inside FORGE. External IDEs will remain optional.
- Superseded the proposed future gameplay language/visual scripting layer in
  ADR010. Existing Flecs construction recipes and C-compatible ABI remain intact.
- Explicitly exclude FORGE editor/source-authoring tools from exported games.
- Require measured editor/runtime performance and reuse of the existing build
  workflow. This is a documented plan, not a newly implemented editor or delivery.

## Integrated C++ authoring — validated Build 260928-000125

- Added central C++ source tabs with highlighting, line numbers, source Undo/Redo,
  search, file creation/registration and baseline-checked atomic saves. External
  edits receive notices and explicit reload; dirty close prompts preserve drafts.
- Added installed Visual Studio2022 discovery and an isolated matching-SDK compiler
  test that never publishes into the project. Official download help is provided;
  no portable compiler or automatic installation.
- Added contained file/line compiler navigation and optional queued Build on Save,
  using the existing managed compiler/admission/export owners. Gameplay changes
  remain Stop/build/Play; source tools stay out of exported games.
- Pinned an editor-only MIT text widget; existing dependency pins, runtime ABI and
  authored formats remain unchanged. Added focused editing/file/conflict/diagnostic
  tests and real source-editing/compiler-test input acceptance.
- Corrected stale Inspector/prefab manual statements contradicting delivered Phase9.
- Full [Windows/Linux gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/36491352542)
  passed at source `7e7f1300ff7848e79381e8da1d7e2d9d37bca696`, including the
  Windows source-editor test, native onboarding, reference-game flow and relocated
  exported-game startup. Reviewed actual compiler-ready, source-editor, saved-source
  and compiler-error captures. ZIP integrity and manifest hashes were verified.
- Corrected Windows compiler environment name casing using the pinned SDL UTF8
  snapshot, launched the compiler script from its working directory, and placed
  only the isolated readiness build in a short temporary path after reproducing
  MSVC path-length failures. No toolchain compatibility checks were relaxed.
- Native fixtures now use bounded wheel increments, simulate an ordinary launch
  without inherited developer-session markers, retain compiler logs before fixture
  teardown, and execute both mandatory scenarios even if one fails. Existing
  assertions and deadlines remain intact.
- Build on Save can be disabled during compilation; this controls future work.
  Cancel build remains the explicit action for the current task. File actions and
  inputs occupy two aligned rows to preserve editing space.
- Following explicit user approval, promoted Build 125 as the current download,
  verified the release ledger and unchanged ZIP hash, archived Build 117, and
  cleaned redundant staging using the established scripts. Physical hardware
  acceptance and deferred reference-game FPS/smoothness review remain separate.
