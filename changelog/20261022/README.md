# 2026-10-22

## Build261022-000208 — Sprite image regions

- Added an optional pixel rectangle on Sprite Renderer. The existing sheet grid
  subdivides that rectangle, including animated frames, Sliced/Tiled fill and flips.
  Zero width/height reaches the image edge; disabled regions retain their values.
- Added authored, Prefab, Undo/Redo, Inspector frame chooser and temporary Live
  gameplay controls through the existing component and property owners. Old source
  files keep their identities and admit default-off values. The public C++ layout
  changes, so native modules require the matching experimental SDK rebuild.
- Added editable Gallery78 using FORGE's existing original images and animation.
  Linked Scene/history, actual Inspector input at three scales, software Vulkan,
  installed C++ both hosts, all four source profiles and source-free/fresh-runner
  exports pass. The focused Windows renderer passes after its point-sampling
  fixture was moved away from permitted texel-addressing ties without dropping
  assertions. The complete numbered gate passes188core/225SDK on each OS,
  326editor checks, shared/source-free/fresh-runner exports and final packaged
  editor C++ acceptance. All1703manifest hashes,78x64PEfiles,18SDKheaders and
  530Galleryfiles/78scenes match source50e311a. The independent source editor also
  passes326checks; its comparison-only retry corrects a baseline-owner reference
  and completes matched startup/idle measurements, without a gameFPS/no-regression
  claim. Four actual native region images and representative final package/example
  images were reviewed. Physical Windows/GPU/FPS/listening tests remain deferred.

## Build261022-000206 — native 2D character movement

- Added feet-origin Grounded/Floating Character Controller 2D under the existing
  physics owner, with bounded move/slide, slope support, downward floor following,
  checked placement and copied support/velocity. Game C++ supplies gravity/jump.
- Extended the existing shared character FIFO and exact-SDK C table; modules must
  rebuild for the matching fingerprint. ABI1 and authored identity remain. Private
  physics checkpoint7 rejects older exact-build payloads.
- Added the editable Character2D C++ project and both-host movement/support/FIFO/
  recovery/permission checks. Local compiled example tests cover painted and box
  floors, wall blocking, held-input behavior and speed/jump tuning. It uses FORGE's
  own existing visual assets; no competitor controller implementation was copied.
- Added read-only Live gameplay movement feedback and selected Scene support/
  velocity guides. All three actual Windows Inspector captures at 100/150/200% and
  three exported Character2D rest/jump/landing images were visually reviewed.
- The [complete delivery gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39822638412)
  passes 188 core and 225 SDK tests on each OS, 326 native editor checks, shared SDK /
  fresh-runner exports and final packaged editor C++ acceptance. The relocated
  Character2D game uses actual SDL input, painted support and a 1.884325m jump;
  holding Jump does not repeat. All 1698 manifest hashes, 78 x64 PE files, 18 matching
  public SDK headers and 529 Gallery files/77 scenes were verified. Physical Windows /
  GPU/FPS/listening acceptance remains deferred. Sprite image regions are separate
  development work and are not in this ZIP.

## Build261022-000204 — swept 2D queries and C++ movement

- Added read-only closest box/circle/capsule sweeps through the existing Physics
  owner and exact C++ SDK, including canonical body exclusion, sensor/layer filters
  and optional initial-overlap skipping. Native ABI1 and authored formats remain;
  the changed exact SDK fingerprint requires matching module rebuilds.
- Added an original editable Movement2D project with A/D/arrows/Space actions,
  Inspector speed/jump settings, painted and box floors, walls and a sensor.
  Physics Body2D owns motion; this example does not add a native controller.
- Focused Linux query/compound checks and installed-SDK movement checks pass in
  ordinary and prepared Play hosts. Launch speeds6/8m/s rise about1.82/3.25m, a held
  jump does not repeat, and initial side-wall contacts do not hide ground support.
- The Windows delivery gate builds the example against its installed SDK, imports
  its assets through the normal owner and includes it under Examples/Movement2D.
  Its new source-free relocated-game workflow checks native keyboard input and
  records rest/jump/landing images. All three actual WARP captures were reviewed.
- The [complete delivery gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39761476803)
  passes core188 and SDK224 on each OS, editor326, shared/fresh-runner exports,
  combined-package relocation and the final editor C++ workflow. All 1668 manifest
  digests, 75 x64 PE files, 529 Gallery files/77 scenes and Movement2D source/asset
  identities were verified. Physical Windows/GPU testing remains deferred.

## Runtime UI preparation — included in Build204

- Skip UI catalog admission when a Scene has no enabled, assigned UI document.
  Active and hidden documents retain fresh availability checks; repaired catalogs
  and restored files recover without changing a surviving document's incarnation.
- The same Release Linux CPU probe with the249-asset Gallery reduced an empty UI
  snapshot from33–34ms to about0.76ms. This measures one copied-model operation;
  it does not establish Windows latency, game FPS or physical GPU performance.
- UI updates now request only copied entity IDs, Prefab flags and UI components
  through the existing Scene projection. Native component values, missing member
  intent and sibling ordering retain the same rules; authored saves remain unchanged.
  A separate1000-entity scene probe reduced the copied UI snapshot median from
  16.8ms to8.9ms across9 samples per version. These are local CPU observations.
- Twenty-one focused checks pass, including Scene/history/identity/Prefab parity,
  RmlUi geometry/resource admission and the matching C++ UI module in both ordinary
  and prepared Editor Play hosts. Full formatting and the
  [Linux/Windows source gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39734839251)
  pass188 core/224 SDK tests on each OS. Build204 includes this work; Build201
  predates it. The local CPU measurements above remain separate from Windows FPS.

## Build261022-000201 — painted 2D collision and connected bodies

- Added an exact named Tile Map layer selector on Static nonsensor 2D bodies,
  with one native compound body, checked chunk rectangles and no per-cell entities.
- Integrated fixed cell edits, last-good resource admission, empty/refilled layers,
  compound queries, shape lifetime and edited-instance recovery before Physics.
- Added readonly published-layer outlines and normal Inspector/Prefab property
  history. Added original editable Gallery77 and matching C++ edit/query helpers.
- Linux/Windows core188 and SDK224 each, full Windows editor326, standalone-only2,
  shared SDK/export/fresh-runner checks and scoped sanitizers pass. Nine native
  Game/Inspector images were visually reviewed; two genuine captures are in README.
  Twelve matched process measurements complete separately from physical GPU testing.
  Build201 delivers these changes with all77 editable Gallery scenes and the
  matching C++ SDK in one ZIP. Complete delivery/source/export/package gates pass.
- Fixed candidate physics initialization before Map cells were ready and checkpoint
  accounting for independently reconstructed copies of shared edited Maps.
- Added bounded UTC/monotonic startup and first-replacement diagnostics to the
  existing opt-in path. Audio-mixer and Save-image fixtures retain those phases;
  the five-second request limit remains. Earlier delays are not claimed fixed.
- Preserved the complete three-scale sequence-export action budget after initial
  imports, with existing per-phase guards and all output/resize/history assertions.

The earlier numbered Build200 attempt failed the audio-mixer startup check and
produced no delivery ZIP. Build201 now includes connected bodies and painted Map
collision; physical testing stays deferred. The runtime/UI work above is separate
development source and is not in this package.

[Build201 acceptance](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39726820668)
checks the final combined ZIP, installed C++ workflows, source-free relocated games
and fresh editor/SDK usage. All1619 manifest files and529 Gallery files match.

Source acceptance: [matrix](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39712815145),
[full editor and matched measurements](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39712829120),
[shared SDK, export and independent runner](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39705291039).
Final documentation updates leave compiled implementation375b941 unchanged.
