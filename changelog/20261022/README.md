# 2026-10-22

## Runtime UI preparation — local validation

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
  and prepared Editor Play hosts. Full formatting passes. Cross-platform source
  validation remains pending; this work is separate from Build201's frozen source.

## Painted 2D collision — validated source

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
  A numbered delivery remains pending. Build199 does not include this extension.
- Fixed candidate physics initialization before Map cells were ready and checkpoint
  accounting for independently reconstructed copies of shared edited Maps.
- Added bounded UTC/monotonic startup and first-replacement diagnostics to the
  existing opt-in path. Audio-mixer and Save-image fixtures retain those phases;
  the five-second request limit remains. Earlier delays are not claimed fixed.
- Preserved the complete three-scale sequence-export action budget after initial
  imports, with existing per-phase guards and all output/resize/history assertions.

The earlier connected-body source remains accepted. Its numbered Build200 attempt
failed the audio-mixer startup check and produced no delivery ZIP. Build199 remains
the available Windows package; physical testing stays deferred.

Source acceptance: [matrix](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39712815145),
[full editor and matched measurements](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39712829120),
[shared SDK, export and independent runner](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39705291039).
Final documentation updates leave compiled implementation375b941 unchanged.
