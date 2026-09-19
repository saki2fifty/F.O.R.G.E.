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


## Final validation and delivery — Build 260919-000061

- Source `a000ff530d6b77facdec6dbbb2399d1753a52ce8`; clean workflow **35412721915** passed all six jobs. Windows/Linux core **32/32** and exact SDK **41/41**, Windows editor controllers **2/2**, renderer/subsystems **33/33**, shaders, D3D12 WARP and relocated runtime UI/font/navigation/converter checks passed.
- Fresh local core/SDK **32/32 + 41/41**, ASan/UBSan/LeakSanitizer **32/32 + 40/40**, and normal/sanitized editor **3/3 each** passed. Local sanitizer converter tools required the GCC runtime-library search path; no sanitizer checks were disabled. Manual **3/3**, format and shared-main syntax checks passed.
- Reviewed **19 actual-editor captures** and **17 appended geometry captures**. **38/38 previous viewport/runtime UI fixtures remain byte-identical** to Build 60. At 1440×900/100%, menu **52→28 px**, global toolbar **52→32 px**, status **40→24 px**, Scene internal top area **100→64 px**: **96 px** less combined chrome.
- At 960×640/200%, transform tools remain available by wrapping and status stays one row, but viewport space is very limited and some Hierarchy/hint content clips. Use a larger window, resize docks or lower zoom. This stress capture is not a claim of comfortable full-editor usability. Physical GPU/DPI/input/readability acceptance remains for user review.
- ZIP CRC and **180 manifest hashes**, compiled build/source identity and matching manual edition verified; exact SDK archives verified against **237 Linux / 246 Windows** hashes. Delivered `260919-000061-FORGE-Windows-x64.zip`, SHA256 `17dd5f02e1c0223df159fc653e571e17889075c466c8dc439c21de4586a1237f`.
- Archived Build 60; removed verified duplicate download/extraction staging. Packages contains only the latest ZIP and editor folder.
- This documentation-only follow-up corrects the prefab how-to's old **Member space / FollowStructure** labels to **Spatial binding / Follow parent**. The already-built offline page retains that old wording in step 4; it refers to the same control. Package bytes and compiled source remain unchanged.
- **Stopped before Phase 7.** No future specialized editor, Mesh/Material pipeline or public editor-extension ABI was implemented.
