# September 19, 2026

## Editor architecture and UX refinement — implementation

- Compact menu/global/Scene tools with dependency-free vector icons; Preferences under Edit, scene identity/dirty marker in its stable tab and one-row status with overflow Details.
- Shared entity recipes across Scene/Hierarchy/Entity menu/palette, explicit placement and one Undo per create. Empty/nonvisual recipes preserve old implicit cube behavior through explicit None.
- Added 16 bounded procedural shapes using shared geometry for rendering/picking/navigation. Original kinds 0–3 retain their geometry. No imported mesh/material pipeline.
- Left-to-right property layout, grouped schema Add Component, conditional attached filter, Transform utilities overflow, Spatial binding wording and collapsed Copy ID.
- Internal document/asset-open adapters retain independent Save/history/inspection ownership. Prefab source initially docks centrally; existing custom layout persists.
- Added standing editor placement guidelines and synchronized current user manual. No future specialized editor or Phase 7.
- Focused and final clean Windows/Linux/SDK/sanitizer/render/package evidence will be recorded after execution. No new numbered delivery yet.

- Focused validation: fresh Linux core **32/32**, fresh portable editor/input **3/3** (including all four shared creation-menu contexts and both placement modes), shared-main syntax, manual **3/3**, and formatting passed. Added palette keyboard/disabled-action, document capability, recipe/geometry and shared creation-menu regressions. Final clean SDK/sanitizer/Windows evidence remains pending.
