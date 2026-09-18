# 2026-09-18

## Phase 6E — navigation foundation

- Official-source audit selected Recast Navigation1.6.0 at6dc1667f580357e8a2154c28b7867bea7e8ad3a7. Adopt only private Recast authoring and Detour runtime libraries; Crowd/TileCache/sample UI remain deferred.
- Extract bounded asset byte reading and SHA256 from animation for reuse. Preserve existing admission semantics and ABI1.
- Implement the authorized static primitive source bridge, single-tile admission/provenance and navigation service/agents. Integration and full validation completed in Build50 below.

## Navigation implementation

- Added exact pinned private Recast/Detour libraries, source geometry selection and six asset-owned build settings.
- Added a bounded navigation worker, strict FORGENAV/native-tile admission, query proof, immutable revisions and catalog-last publication. Failed/stale candidates preserve previous assets.
- Added reflected NavigationSurface/NavigationAgent components, world-scoped service, fixed-tick nonphysics movement, reconstructed routes, exact SDK callbacks and optional viewport overlays.
- Added Inspector and Content controls and the function-based Navigation user manual.
- Shared bounded bytes and fixed-command worker plumbing with animation, preserving its admission path.
- Build/query/agent/publication tests, full regressions, sanitizers and Windows delivery verification passed; see the Build50 delivery record below.

### Integration corrections and validation

- Preserve inherited NavigationAgent queries through Flecs' normal query path; maintain independent translation/rotation/scale ownership.
- Capture source geometry at writable tick boundaries so gameplay SDK queries never write derived transforms inside read-only systems.
- Recover full semantic agent destinations and local translation when authored prefab property masks hide direct gameplay edits. Restore only into an unpublished candidate world; failed recovery preserves the active runtime.
- Validate detail boundary edges before Detour closest-point calls; use bounded surface projection for traversal-edge rounding.
- Local final normal suites: static **27/27**, shared exact SDK **34/34**, portable editor **2/2**. Shared ASan/UBSan/LeakSanitizer **33/33** passed. Manual **3/3**, cache invalidation, C17 headers, formatting, workflow lint and runtime link separation passed. Static ASan/UBSan/LeakSanitizer **27/27** also passed. Clean Windows delivery follows.

### Windows binary-fixture correction

Build48 is withheld: the corruption/recovery test read a binary NavMesh through a text-mode stream, allowing Windows newline/EOF translation to alter its saved copy. Production admission correctly rejected the changed digest. The fixture now reads binary and asserts exact restored bytes. Linux core27/SDK34 passed; Windows navigation mesh/process and unrelated regressions passed while this recovery fixture failed. Build49 subsequently verified the corrected test.

### Packaged-worker CI path correction

Build49 passed Windows/Linux core27/SDK34, Windows editor controllers2 and navigation/runtime/WARP25, shaders and compiled build identities. Its new navigation relocation step launched from GitHub's default directory instead of the explicit Windows checkout, so the test script was not found. The step now uses the same checkout working directory as the adjacent package/converter checks. Build49 is withheld; no runtime source changes were needed.

## Phase6E verified delivery — Build 260918-000050

Source `2c81647`, verified [run 35302622445](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35302622445). Windows/Linux static **27/27** and shared SDK **34/34**, Windows editor/process **2/2** and remaining navigation/runtime/viewport **25/25**, four shaders, executable build IDs and both packaged worker relocation checks passed. All **29** existing viewport fixtures match Build47 byte-for-byte. Local static27/sharedSDK34/editor2, static sanitizer27/shared sanitizer33, manual3 and supporting checks passed.

Verified ZIP CRC, all manifest hashes, exact source/build identity, five x64 programs including navigation worker and official converter, **30** matching manual source/HTML pages, Recast/Ozz/Jolt/miniaudio notices and both SDK packages. ZIP SHA256: `293539515f4103f01085f48b81fc7c79e6152d4854e6b102c39ee2de75b27e0f`. Previous Build47 archived; package cleanup complete.

Manual acceptance: [Navigation](../../manual/editor/navigation.md) provides floor/obstacle generation, agent destination, Play/Pause/Step/Resume, save/reopen and prefab override/Revert. No C++ editing required. This is basic nonphysics following, with no dynamic obstacles or crowd avoidance. **Phase6E complete; STOP before Phase6F.**

Build50 found no compatible editor cache and rebuilt from scratch, then saved the verified cache for later compatible iterations.

## Phase 6F — runtime UI

- Added pinned RmlUi 6.3 and FreeType 2.14.3, licensed engine Lato font, reusable ImGui-free presenter and Diligent geometry/texture/scissor/transform/stencil adapter.
- Added reflected UiDocument assets/components, runtime-only UiService, bounded copied models, semantic commands with session/generation/incarnation correlation, acknowledgements and duplicate suppression.
- Preserved runtime gameplay isolation and headless presentation-free linkage. UI presentation remains responsive while paused; gameplay command processing obeys fixed ticks.
- Added project-contained resource admission and candidate reload with initial geometry/texture preparation; failed replacements preserve the prior usable HUD.
- Added Content HUD creation/registration, Inspector asset/visibility/layer editing, Play reload, explicit input ownership and release/focus handling. Added the Runtime UI user manual and technical dependency/ownership docs.
- Added runtime/presenter/process/SDK/prefab/input tests and separate D3D12 UI fixtures. Final local normal suites: core30/30, exact SDK38/38, editor/input3/3. UI/core/presenter checks pass with ASan/UBSan/leak instrumentation; full sanitizer suites and the converter-override retest are recorded in validation evidence. Windows verification is pending; no Windows delivery claim yet. Phase 6G remains outside this change.

### UI integration validation corrections

- Candidate first-render preparation catches lazy image/font/geometry failures before publication; failed structural candidates wait for explicit retry instead of reparsing every frame.
- Keep command sequencing across hidden viewport/empty document intervals; clear queued stale requests on native-reload generation changes. Added a real editor-process reload-order regression.
- Use RmlUi6.3's actual project-root font-source path policy and quoted @font-face syntax; include UiDocument in the installed experimental SDK and build/run its consumer. Relocate the unchanged StableId declaration into the identity header for that consumer.
- Apply the existing unsanitized converter override to animation-admission fixture generation too. Strict UBSan flags exposed Ozz0.17.0's zero-byte file-write call with a null pointer in the converter. FORGE admission and Ozz runtime remain instrumented; no upstream suppression or dependency change is introduced.

### Final local Phase6F validation

Core30/30, shared exact SDK38/38, editor/input3/3 passed. Static sanitizer29 + corrected admission1 and shared sanitizer36 + corrected admission1 passed with ASan/UBSan/leak checks. RmlUi and FreeType are instrumented. Manual3, C17 headers, format/workflow/cache checks and headless/presenter link separation passed. Windows/D3D12 execution remains the next gate.

### Windows renderer naming correction

Build51 is withheld. All four Windows/Linux core and SDK jobs and the editor process-controller tests passed. The editor compilation caught Windows' `interface` macro expanding a new renderer method name. Renamed that private accessor to `render_interface`; Linux syntax checks also define the Windows macro to cover this collision. A new numbered Windows build will verify the correction; no scene/ABI1/rendering behavior change.

Build52 is withheld. Windows/Linux core30/30 and SDK38/38 passed. The independent UI WARP test included Diligent's native queue header before its required Direct3D declarations; moved the Direct3D include into a prerequisite group, matching the existing viewport fixture. No runtime or renderer behavior changed. Windows compilation/render/package validation will rerun with a new reserved build ID.


## Phase6F verified delivery — Build 260918-000053

Source `a4131578468972663346c832532bfb0ca1e6b541`, [run35311619027](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35311619027), passed every job. Windows/Linux core **30/30** and exact SDK **38/38**, Windows editor controllers **2/2** and remaining runtime/input/render checks **30/30** passed. Actual D3D12 WARP exercises premultiplied alpha, scissors, transforms, stencil set/inverse/intersection, textures, fonts and resize. All **29** previous viewport fixtures are byte-identical; nine separate UI fixtures passed, and the two HUD document sizes were visually inspected. All four existing shaders and executable build identities passed.

Packaged UI font/notices/rendering, navigation worker and official animation converter passed relocation with developer PATH removed. ZIP CRC, all84 manifest hashes, exact reserved source/build, five x64 executables/four DLLs,31 matching manual pages, font/license resources and both experimental SDK archives were verified. License text matches pinned upstream sources after Windows newline normalization. ZIP SHA256: `7f5226f8d8844ce3735011efb9d7b60aa8e2123d5f710ed23ce411f66742ff77`.

The editor had no compatible cache and rebuilt from scratch; this successful run saved a verified cache. Build50 is archived; packages contains only Build53 ZIP and current extracted editor. Builds51/52 remain withheld historical attempts. This delivery-evidence documentation follows the packaged source commit without changing its binaries or manual.

Manual acceptance: [Runtime UI](../../manual/editor/runtime-ui.md): create HUD example, assign UI Document, Play/capture input, Pause/type/Step/Resume, resize, save/reopen and prefab visibility/Revert. No C++ edits required. Physical GPU, mixed-DPI and IME desktop acceptance remains pending. **Phase6F complete; STOP before Phase6G.**


## Phase6G — final SDK consolidation and integration

- Classified core/source, exact SDK, built-in/private, implementation and ABI1 contracts; added extension author guide and explicit process/thread/fixed-clock/recovery ownership maps.
- Added live exact-SDK capability/version/callability queries, retained the startup bitmap for rebuilt clients, checked callback context/owner thread, named navigation statuses, and exposed bounded module CPU samples through the existing profiler. ABI1 and persistent schemas/identities are unchanged.
- Extracted AssetRef value headers from AssetCatalog declarations; shipped the missing UUID value implementation as a small static helper with a unified Client target. Installed headers drive fingerprinting; bridge/value semantics and configuration checks participate in compatibility. Removed navigation sample's reflection-to-JSON UUID workaround.
- Consolidated repeated typed service-slot publication checks; added retained-UI owner-thread checks and UI model/command profiling scopes.
- Added one combined gameplay sample, live optional-provider/lifecycle regression coverage and cross-subsystem pause/step/recovery integration. Added installed-package mismatch rejection and combined relocated consumer coverage.
- Package manifests now cover licenses and generated launch/readme files; the exact SDK archive includes regular-file hashes and contained symlink identities. Link-boundary checks prove the headless runtime excludes presentation/editor libraries.
- Focused SDK/services/UI and combined integration passed. All29 affected regressions passed after fixing installed-test compiler selection, including the relocated SDK consumer. Format, manual3, cache invalidation and workflow lint passed. Final clean Phase6 results are recorded below; no intermediate official package. Stop before Phase7.

### Final Phase6 delivery — Build260918-000054

Packaged source `991aca3447e148cdbb903331eaf820d101674371`, [clean run35348388494](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35348388494), passed all jobs. Clean Windows/Linux static **32/32** and exact SDK **41/41**, installed/relocated consumers, Windows editor controllers **2/2**, remaining runtime/input/render checks **32/32**, four shader builds and actual D3D12 WARP passed. All **29 viewport and 9 UI fixtures match Build53 byte-for-byte**.

Fresh local static32/shared41, ASan+UBSan+LeakSanitizer static32/shared40 and portable editor/input3 passed. The sanitizer build environment needed the matching GCC12 runtime path for an upstream post-build utility; corrected before testing. No product fix or sanitizer suppression. Bounded converter/navigation worker overrides remain as documented; the instrumented host/admission/runtime still validates corrupt assets and recovery. Format, C17 boundary, manual3, workflow lint and cache checks passed.

Relocated packaged UI/font, navigation worker and official gltf2ozz passed with developer PATH removed. Verified ZIP CRC, reserved source/build, **180 manifest hashes**, **95 license/notices**, five x64 executables/four DLLs, **31 unchanged manual pages** with the new build identity, and both SDK archives (**237 Linux / 246 Windows regular-file hashes**, two Linux SONAME symlinks). SHA256: `2b99b4a6e228cb0a811c95917413e41c992c4edd11221c2484f7cffd19a41cb0`.

One final numbered package delivered. Build53 archived; packages contains only Build54 ZIP and current extracted editor. No intermediate Phase6G package. This documentation-only follow-up records validation without changing the packaged source/binaries/manual or rerunning the matrix. No new editor controls; desktop GPU/DPI/IME behavior still requires real-machine acceptance. **Phase6 complete; STOP before Phase7.**

## Editor UX redesign — implementation and validation

### Correctness and shared authoring controls

- Fixed prefab-source UI Document drawing, unsigned integer Layer editing, and Create source in a newly created project's existing empty Native folder. Existing source is never overwritten.
- Added explicit entity/asset/prefab-member selection. Inspector identifies its scope; asset click clears entity selection, while dragging keeps the destination entity Inspector available.
- Replaced subsystem-specific Inspector implementations with a shared registered-schema Add Component search, grouped components, typed fields, asset search/clear/reveal/drop, inline validation errors and contextual Remove/Revert.
- Exposed independent XYZ transform labels, prefab ownership/override intent and adjacent Revert, including equal-value intent. Existing local TRS, derived world transforms and scene Undo semantics remain authoritative.
- Added hierarchy context actions, F2 rename focus and validated world-preserving reparent drag/drop. Rejected operations retain the original scene.

### Workspace and document ownership

- Added independent authored Scene and runtime Game views using the existing single isolated Play process. Scene navigation, modal transforms and the grid retain their existing behavior. Inactive Game presentation cannot cancel a Scene gesture.
- Added familiar application menus and a separate adaptive global action bar, consistent action availability, command palette routes and compact Menu/More overflow.
- Added active-task Save ownership, dirty draft indicators and independent prefab/settings close guards. Failed publication stays open; Scene Save cannot silently publish a different draft. Apply and cross-document Undo remain deferred.
- Made prefab source independent of Content visibility. Preserved custom docking and added Game/Problems tabs; Reset layout remains deliberate.
- Adopted the already packaged OFL-licensed Lato font, retained monospace logs, and added responsive property labels, labeled axes and bounded draft windows. No new dependency or font modification.
- Game clearly displays runtime state and input capture. Outside clicks release capture and reach editor controls; Escape/F6/F7 and global interface zoom remain available.

### Content and feedback

- Content now browses the existing AssetCatalog with search/type/folder filtering, asset inspection and supported create/register/conversion/build workflows. No Phase 7 importer, cooker or second asset database.
- Added bounded session Problems with contextual navigation and chronological Console status transitions. Failed field edits show local errors and retain valid values.
- Updated function-based manual pages for the exact controls, save/history boundaries and remaining limitations. Added technical editor ownership documentation.

### Validation

- Focused portable editor/input/native tests passed **3/3**, including the three original defects, all registered optional component drawers, independent dirty-draft close/save/failure paths and outside-Game capture release.
- Clean Linux static **32/32** and exact SDK **41/41** regressions passed. Final sanitizer and clean Windows results will be recorded after execution.
- Added actual-editor WARP fixture captures for the redesigned panels and responsive layouts. Existing viewport/shader/runtime UI and package-relocation checks remain in the delivery workflow. Automated fixtures are distinct from physical Windows acceptance.

### Final documentation and narrow-panel review

- Corrected older manual routes for scene creation, scene Save As, Content scene opening, entity actions, audio fields and Scene/Game ownership so the bundled how-to pages match the redesigned controls.
- Shared labeled XYZ draft inputs cover prefab Euler rotation and project gravity; narrow Inspector transform rows stack vertically rather than squeeze unreadable columns. Added 100%/200% narrow/wide field-layout assertions.
- Added explicit no-result guidance for attached-component filtering and command search.
- Clean static/shared sanitizer suites passed **32/32** and **40/40**; editor sanitizer **3/3** passed before this final presentation follow-up. Build55 was cancelled before packaging to include these corrections in the final delivery; it was not delivered.

- Problem navigation reveals Inspector and selects the scene task so an unrelated open prefab/settings draft does not retain the active Save route after navigating to a scene issue.

- Made the Windows-only screenshot fixture self-contained and preserved required native Direct3D include ordering. Build56 was cancelled before packaging after this source-review finding; no ZIP was delivered. Final portable editor and editor sanitizer runs both passed **3/3**.

- Windows validation exposed an MSVC C++20 overload ambiguity in the Inspector first-property Revert comparison. Compare the explicitly typed schema string; preserve identical override behavior. Build57 failed before packaging and was not delivered.

### Actual-render acceptance corrections

- Build58 passed all six clean Windows/Linux jobs, editor controller2/2 and renderer/subsystem33/33, shaders, relocation and build identifiers. All38 previous viewport/runtime UI images were byte-identical to Build54. It was held before delivery after visual review found Content rows below the fold and missing prefab catalog entries.
- Compact Content search/filters preserve visible asset rows in the default bottom area. Content merges existing prefab-library AssetRecords without creating a second database; newly selected prefab assets resolve with Content hidden. Reveal brings the Content tab forward; fresh/reset layouts focus Content.
- Closing a prefab draft returns member selection to asset scope. Added regression coverage for catalog resolution, closed-draft selection and visible asset-table space.
- Actual-editor fixtures now wait for a confirmed running state before capturing Game and also capture 1920×1080 at200%, alongside the960×640 stress layout. The manual states practical window/scale ranges.
- Default bottom-dock height accounts for scaled control rows within a bounded 22–40% share; existing custom dock sizes remain untouched.

- Build59 passed all clean CI checks and verified12 actual-editor captures; all38 existing render fixtures remain byte-identical to Build54. A first-launch tab-selection mismatch remained: newly appearing dock tabs could steal Content focus. Apply default focus after all initial dock tabs have been created and assert the initial Content tab in the actual-editor WARP fixture. Build59 is retained as an undelivered review artifact.
- Added a portable real-ImGui first-use docking regression, in addition to the actual Windows editor assertion, for Content focus after all primary tabs appear.

### Redesign final validation and delivery — Build 260918-000060

- Final clean workflow 35381763046 passed all six jobs: Windows and Linux core **32/32** and exact SDK **41/41**, Windows editor controllers **2/2** and renderer/subsystems **33/33**, shader compilation, D3D12 WARP and relocated UI/font/navigation/converter checks.
- Fresh local core/SDK **32/32 + 41/41**, ASan/UBSan/LeakSanitizer **32/32 + 40/40**, final portable editor **3/3** and sanitized editor **3/3** passed. Manual, format and workflow checks passed.
- Inspected all **12** actual-editor captures. First-use Content selection passes its Windows assertion; **38/38** previous viewport/runtime UI images remain byte-identical to Build 54. Automated rendering remains distinct from physical Windows usability acceptance.
- Verified final ZIP CRC, **180** manifest hashes, compiled source/build identity, manual edition, x64 executables and bundled font/license. Verified exact SDK archives against **237 Linux / 246 Windows** file hashes. Compiled source: `f9f4b4ef7f00b15407c071269dffdd748f8a8f6a`.
- Delivered one redesign ZIP: `260918-000060-FORGE-Windows-x64.zip`; SHA256 `3b5403243baefd265190dff48c9c08c658e4e945dccf2504b1d556aebf87425f`. Archived the prior delivery and removed verified duplicate staging. Builds 55–59 were not delivered.
- This follow-up records validation only; it does not change the packaged source, binaries or manual. Existing custom layouts are preserved; **Window > Reset layout** shows the new defaults. Small windows at 200% need lower zoom or panel adjustment. Physical GPU/DPI, input feel and end-to-end desktop acceptance remain for user review. **Stopped before Phase 7.**
