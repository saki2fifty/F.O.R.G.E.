# Changes — September 22, 2026

## Authoring responsiveness

- Removed full-schema copies from per-entity custom-component validation, prefab override projection, and reflected EntityRef remapping. These paths borrow the admitted immutable schema; their validation and transaction semantics are unchanged.
- Same-host, three-run Debug measurements found repeated create commands previously about 2.7× the pre-Phase7 baseline and prefab instantiation about 4×. The correction brings those workloads near baseline. These are CPU authoring measurements, not renderer or hardware FPS claims.

## Runtime content packaging

- Added cooked-content dependency closure packaging and source-independent verification through the existing catalog and selected-resource loaders. Supports imported Model families, Texture, built-in Material, AudioClip, and compiled Shader assets; engine-owned references retain their reserved identities.
- Package metadata records logical identity, type, selected artifacts, typed runtime dependencies, hashes, and explicit target/profile. Authoring sources, sidecars, unrelated cache revisions, and private editor metadata are excluded. Bounded immutable recipe provenance remains for artifact verification.
- Preparation uses a fresh candidate directory, validates it before publication, refuses existing output destinations, and preserves existing packages on failure. Missing/removed/wrong-type/stale dependencies, incompatible targets, corruption, extra files, cancellation, and resource-budget excess are rejected.
- Immutable selected artifact reads no longer create writer lock files, allowing read-only packaged data. Concurrent cache eviction fails the individual load safely.
- This is a content packaging tool; standalone visual executable export and additional legacy-source packaging adapters remain separate work. Validation is in progress; no new numbered build is issued by this checkpoint.

## Official glTF validation coverage

- Added18 unmodified, licensed Khronos fixtures with exact source hashes and attribution, covering GLB, textured PBR, normal maps, alpha modes, multiple primitives, skinning/animation, cameras/lights, sparse accessors, quantization, Draco and advanced material layers.
- Added native import/cook/roundtrip tests and a reproducible external official-validator comparison tool. The official validator reported zero errors for all18 fixtures; warnings and unsupported-extension information are retained. All 18 fixtures pass native import/cook checks both normally and with ASan/UBSan/LSan. Windows validation is pending.
- The validator is an external test utility, not a new engine/runtime dependency. Its upstream prerelease label and exact source/archive provenance are documented.

## Defensive input validation

- Added a bounded, deterministic regression corpus covering GLB input, small image headers/payloads, immutable artifact manifests, shader include paths/input hashes, and custom component schema/value transport. Valid inputs are checked before and after rejected candidates; invalid inputs cannot alter the native schema or known-good artifact bytes.
- All 640 seeded mutations plus shader path/hash rejection checks pass normally and under ASan/UBSan/LSan. Some mutations remain valid content and are correctly accepted. No infinite fuzz job or sanitizer suppression was added.

## Editor validation coverage

- Extended actual Windows editor captures with 150% Texture, Material, Model, Mesh, generated Material, Content thumbnail and file-review views. Added Audio clip and Shader import views at 100%, 150%, and 200%.
- Capture readiness checks require the intended document to be visible and new imports to finish successfully. These new native captures are pending execution; source inspection alone is not acceptance.

## Validation checkpoint

- Full Linux Release build and all 99 tests pass (198.62 seconds), including Vulkan material binding, existing subsystem regressions, asset pipelines, packaging, official samples and the bounded input corpus.
- Strict sanitizer checks pass for the affected package, source-copy, reflected authoring and corpus paths. The previously approved Flecs managed-include defect remains separately classified; no suppression was added.
- Manual links/rendering and formatting checks pass. Native Windows compilation, new captures, final clean profiles and the final numbered delivery remain pending.

## Cache maintenance and resource diagnostics

- Added structured CLI commands for cache statistics, integrity verification, budget pruning, selected-asset clearing, full clearing and interrupted-publication cleanup. Commands use project writer ownership and the existing cache lock; selected revisions are protected during pruning. Clearing one shared model artifact reports every affected logical member.
- Removal preflights recognized flat contents and bounds work before deletion. Unfamiliar nested paths, redirected paths and malformed orphan names are retained. Cache operations preserve authored sources, sidecars, catalog identity and already-owned resource bytes. Integrity verification explicitly distinguishes hash/manifest checks from importer/runtime format admission.
- Added Tools > Loaded resources with CPU/GPU payload counts, loaded/retiring revisions, strong lease counts, failed/pending requests, cache counters and Show in Content. GPU payload is labeled separately from total VRAM. The view covers the main editor scene owner and refreshes only while open.
- Expanded native capture stages for resource diagnostics at 100%, 150% and 200%. Added native draw/culling/batching and shader import/cache timing measurements; Windows execution remains pending.
- Added real 1,000/10,000-file scan/catalog/query workloads, 2048-pixel texture mip/cache/reload checks, and a 65,536-vertex model with 4,096 nodes, 64 materials/images and 64 clips. These are bounded test workloads, not a universal throughput guarantee.
- Corrected three runtime-package JSON comparisons rejected by MSVC. Both Linux CI profiles passed the preceding source; Windows compilation required this correction and must be rerun.
- Reconciled asset publication/discovery/settings/texture/resource documentation with current providers and editor consumers. The new resource/cache manual explains ownership, limitations and recovery.
- Focused Linux checks pass: cache maintenance, resource lifetime, asset tools, runtime content packaging, asset build and scale workloads (6/6, 24.04 seconds). Manual checks (3/3), formatting and adapted Linux editor syntax pass. Strict ASan/UBSan/LSan checks also pass (5/5, 72.12 seconds). Full Linux regression completed 99/101 inside the sandbox (235.75 seconds); both local-loopback tests passed with socket access on rerun, giving passing evidence for all 101 tests. Native Windows results remain pending.

## Mesh index representation

- Cooked Mesh version3 selects 16-bit indices when referenced indices fit and 32-bit otherwise. CPU working values stay exact uint32. Versions1/2 remain readable; new version3 data is rejected by older readers. Model recipe fingerprints include the new packing helper.
- Added explicit nonindexed list geometry across Mesh admission, cooking, Diligent draws, picking and offline processing. Preserve-only processing keeps the representation; native processing generates equivalent connectivity within aggregate budgets.
- GPU uploads use Diligent index types and Draw/DrawIndexed without native API assumptions. Compact decoded input still enforces the expanded CPU byte budget before allocation.
- Added legacy-layout, 65535/65536 boundary, malformed type/count/span, byte-budget, processing and picking regressions. Native fixtures compare 16-bit, 32-bit and nonindexed pixels and read back actual buffers. Linux mesh/processing/import/worker/runtime-package/Vulkan integration passes11/11 (84.30 seconds), with preceding focused checks6/6. Strict ASan/UBSan/LSan mesh/bounds/model/corpus checks pass5/5 (19.17 seconds). New native Windows index fixtures remain pending.

## Typed asset selection

- Added shared vector type icons to asset picker results and Content fallbacks/list rows. Icons indicate type; they do not claim a rendered thumbnail or loaded resource. Existing real thumbnails remain in use.
- Picker results sort engine assets first, filter source/member names and types, mark removed members, and clip large lists through the pinned ImGui list clipper. Removed members cannot be newly assigned through picking or drag/drop; existing unresolved references remain intact until edited.
- Added actual UI interaction coverage for a 2,000-row catalog, search/type filtering, selection, Clear and incompatible-reference preservation. The actual Linux ImGui interaction/runtime test passes (14.52 seconds); native captures at 100%, 150% and 200% are added and pending.

## Windows validation corrections

- The latest core runs found a test-only path-separator mismatch and a runtime-package file-write failure in the longer shared-SDK build directory. The artifact/staging path reaches the Win32 260-character limit.
- Asset CLI tests now normalize snapshot dictionary keys consistently. Asset/cache/package I/O uses a private Windows extended-length path adapter at the OS boundary; logical catalog paths and persistent identities are unchanged. Package write errors include the failing path.
- Added a package/load regression with final output paths beyond 290 characters. Linux package/cache/CLI checks pass4/4 (3.01 seconds); strict ASan/UBSan/LSan checks pass3/3 (3.75 seconds). Native confirmation remains pending; the prior Windows failure remains recorded.

### Official fixture validation portability

- Compare corpus byte counts and hashes using explicit JSON value types, avoiding an MSVC C++20 reversed-comparison ambiguity. Fixture content and validation remain unchanged.
- Windows rerun pending; this corrects the compiler diagnostic from the preceding source audit.


### Authored mesh LOD import and inspection

- Admit the documented geometry-only node subset of MSFT_lod through the existing isolated model recipe. Produce a separate combined mesh per owning node; unrelated reuse of the high-detail mesh remains unchanged.
- Preserve level thresholds, geometry, material identities, palettes, morph channels and bounds. Validate aggregate counts and material UV requirements across every level before publication. No automatic simplification or hidden hierarchy switching.
- Add published level counts, thresholds and local bounds to the central Mesh document. The shared Scene/Game renderer and preview select levels by projected size.
- Reject unsupported node/subtree/material switching combinations explicitly and preserve the previous asset family. Record the optional final cull-hint policy in import diagnostics and the manual.
- Model-bundle, official-corpus, direct/isolated recipe, scene, runtime-package and Vulkan checks pass7/7 (96.19 seconds). Strict ASan/UBSan/LSan model/scene/package checks pass4/4 (126.57 seconds). Expanded palette/animation/lower-level UV regressions also pass normal1/1 (1.00 second) and strict1/1 (4.87 seconds). Manual checks3/3, formatting and adapted Linux editor syntax pass. Native Windows validation remains pending.

### Windows package directory paths

- The long-path regression exposed directory creation beyond MAX_PATH after the file-stream fix. Package I/O now carries the extended Windows path spelling through directory operations, staging, validation and loading; stored manifest locators remain relative.
- Keep the failing Windows evidence and the real long-destination regression. Native rerun remains required.

### Backend portability and optional effects

- Both the Windows presentation library and the optional Linux/Vulkan compilation check use one shared renderer source list. The complete shared source set compiles under Linux/Vulkan definitions; the actual Vulkan material binding/readback probe passes. This is not a complete Vulkan frame or editor acceptance claim.
- Audited pinned DiligentFX Bloom, temporal anti-aliasing, ambient occlusion, reflections and depth of field. Bloom itself requires a ready temporal context with motion/current/previous depth/camera data. FORGE's current frame composition does not provide those inputs. The exact source evidence and deliberate advanced-effect deferral are documented in `docs/rendering-postprocessing.md`; existing exposure/tone mapping remains available.

### Custom Shader material surfaces

- Add a versioned, backend-neutral surface declaration for named numeric parameters, texture dimensions and UV semantics. Compile separate color/depth pixel roles in the existing bounded Shader worker. Keep shared vertex fetching, morphing, skinning and winding engine-owned.
- Material source version2 selects a typed Shader asset and preserves independent inheritance, equal-value intent and Revert. Cooked materials embed the exact validated Shader snapshot so incompatible later Shader changes cannot invalidate the previous good material after restart, cache pruning or content packaging.
- Add asynchronous interface preparation to the central Material document, typed Shader selection and declaration-driven parameter/texture fields. Scene history and source/publication ownership remain separate.
- Shared GPU preparation uses named Diligent bindings and copied reflected offsets. DXBC realization is isolated behind the backend adapter; incompatible backend artifacts reject. No D3D12 register-space binding contract is introduced.
- Focused Linux shader/material/package/Vulkan checks pass6/6 (12.80 seconds). Expanded last-good retention/package and generated Vulkan surface compilation checks pass2/2 (13.82 seconds). New native surface draw/alpha/shadow/morph/skin fixtures and updated editor captures require Windows validation before acceptance.
- Windows core validation exposed canonicalization removing the extended-length prefix. Package redirection checks now compare normalized OS spellings while retaining the actual canonical/symlink check. Native confirmation remains required.
- The preceding native audit also found two outdated fixtures: raw vertex-fetch readback still declared uint32 indices after compact upload, and the picker capture created a legacy Primitive entity without a Mesh renderer. Fixtures now use the actual uploaded index type and the explicit mesh creation recipe. Production drawing already uses the uploaded index type. Fresh native execution is required.

- Custom cooked surface programs reuse the existing Diligent presentation shader cache while retaining native reflection verification. Added a native reuse regression; exact pinned cache source returns native shaders when its hot reload is disabled.
- Custom Shader/Material strict ASan, UBSan and LSan regressions passed5/5(15.66s); local actual ImGui Material editor interaction passed1/1(1.30s). Native surface acceptance remains pending.


## Imported material variants

- Added stable AssetId-backed material-variant subassets in model bundle4. Legacy bundles1–3 remain readable; new cooks authenticate inline selectors, mappings and typed dependency edges.
- Connected per-primitive selection to complete draw candidates and authored LODs. Different primitives sharing a base material can choose different variant materials; explicit slot overrides retain precedence and unmapped parts retain their base material.
- Added reflected Mesh Renderer variant selection, a model placement choice, and a root Inspector action backed by one scene command/Undo step. Scene/prefab intent remains independent from import publication. Missing/foreign selections fail preparation while prior usable draws remain available.
- Added source-reorder, mapping-corruption, identity, resource-selection, scene/prefab/Undo and legacy-field regressions. Validation is in progress; no native acceptance or numbered package is claimed for this bundle yet.
- Windows CI identified a further long-path boundary: catalog existence/size queries and file identity must use native path spelling after canonicalization. These calls now use the existing OS-path adapter. Runtime package opening explicitly loads its manifest-required catalog, and the long-path regression checks catalog reopening, identity and material loading. Fresh Windows confirmation remains required.

### Reference editing, diagnostics and validation follow-up

- Entity reference fields now search current-scene hierarchy paths and persistent IDs, clip large lists, distinguish missing targets from references to another scene, and preserve references until explicitly changed. Display names come from authored names rather than Flecs internal symbols.
- Import errors select their logical asset in Problems. Shader compiler errors retain their HLSL/include path and line/column; Open source uses a bounded project-contained read-only viewer without replacing existing drafts. Material errors also link their source document.
- The Material Editor reserves footer space only for visible progress/errors, removing unused space observed in the native 200% capture. Added variant placement/Inspector captures at100/150/200%.
- The previous Windows source audit passed66/68 tests and produced all67 editor captures. Remaining failures were long-path catalog discovery and a GPU residency fixture that expected32-bit index bytes after compact16-bit upload. Catalog OS calls now use extended-length paths, and package opening requires the manifest's catalog to load. The residency assertion now verifies the actual42-byte upload and uint16 type. Fresh Windows verification is still required.
- Local variant/model/package checks passed6/7; the remaining command-discovery assertion was updated for the new command and then passed independently. Actual ImGui editor/model/material interaction checks passed3/3(21.72s), Vulkan binding/readback passed1/1(1.31s), and manual/format checks passed. The expanded strict sanitizer model suite exceeded its old120-second test limit; this is recorded as a timeout, not a sanitizer pass, while a bounded measured run investigates its duration.

- The independent strict model run completed cleanly in144.07s with ASan/UBSan/LSan enabled (normal model-worker suite48.72s). Its sanitizer-only CTest timeout is now240s; the normal120s budget and every sanitizer check remain. The original timeout is retained in the evidence record.

## Missing-resource recovery and engine textures

- Added reserved engine Texture assets for white, black, flat-normal and checker content across 2D, 2D-array, cube, cube-array and volume shapes. Their ordinary typed leases preserve semantic color/data/normal/HDR variants and immutable engine revisions.
- Material authoring, dependency publication and source-free packages now admit built-in texture/material dependencies with exact type/revision validation. Existing engine primitive/material revision digests remain unchanged.
- Initial missing materials use an unlit magenta error surface. Missing textures use semantic-appropriate, dimension-matching fallback assets. Substitutions retain independent engine identities and preserve authored references/catalog data. Initial GPU surface failures have one bounded error-material retry.
- Failed replacements retain a previously working complete draw. Initial fallbacks can recover after valid publication/assignment. Missing meshes remain omitted with a diagnostic.
- Added CPU failure, identity, dependency, package and stale-candidate regressions, plus native pixel/recovery fixtures. Expanded local checks pass (6/6, 110.07 seconds), including model recipe/worker, material publication, fallback failures and Vulkan binding. Strict sanitizers and native Windows execution remain pending. Shared Vulkan compilation and binding/readback pass.
- Windows validation of the preceding variant bundle exposed an MSVC JSON/string comparison error in model bundle validation. The correction uses an explicitly validated string; previous Windows runs did not reach native acceptance and are not counted as passing.
- Expanded strict model recipe checks pass under the measured 240-second sanitizer harness budget (151.16 seconds). Normal timeout and production limits are unchanged.

- Added a permanent source/cooked-format matrix with concrete importers, admitted features, limits, dependencies and executable fixtures.

## glTF animation pointers and current specification audit

- Added whole-node translation/rotation/scale/morph-weight targets for KHR_animation_pointer, using the existing typed animation tracks, canonical official Ozz conversion and runtime owner. Core/pointer equivalents preserve clip/node semantic identity. Instancing weight expansion and LOD restrictions share the same target resolver.
- Added malformed/default/numeric/duplicate target checks and actual converter/runtime/publication regressions. Other property domains and individual vector lanes reject clearly; no generic property-animation evaluator or dependency patch was introduced.
- Unsupported optional extension notices now survive model publication and appear in the central model document's Import notes and asset-addressed Problems. Required unsupported extensions still reject.
- Rechecked the official registry at836573be93954f26827e3dc16476f8620209a1f2. Its ratified list is unchanged; its new core geometry wording requires primary COLOR_0 values in[0,1] and indexed semantic suffixes of at most nine digits. New imports enforce those rules, preserve additional color/custom streams, and leave previously published artifacts unchanged. No dependency pin changed.
- Initial pointer/model tests passed4/5; the official-converter verification fixture omitted the required extension allowlist and was corrected. Expanded validation remains in progress. Fallback strict ASan/UBSan/LSan checks passed3/3 (22.44 seconds), with no suppression.
- Added a renderer feature matrix with concrete consumers, executable coverage and explicit backend/algorithm limits; final native acceptance remains separate.

- Added the complete glTF core/extension matrix: all27 current ratified extensions, required/optional behavior, implemented subsets, technical dispositions, executable coverage, vendor compatibility and every current draft/proposal registry entry.

## SDK and deeply nested package corrections

- Included engine texture identities in the installed SDK and its exact-version fingerprint. The prior CI SDK consumer failed because this transitive public header was missing.
- Preserved the extended Windows path form inside the private derived-cache I/O owner, including directory creation, enumeration, quarantine and maintenance. Logical locators and artifact hashes remain unchanged. Native CI reproduced the failure with a package destination longer than290 characters; a new cache fixture also exercises publication, readback, corruption, quarantine and cleanup there.
- Moved unsupported optional-extension notices into the shared model recipe so direct tooling and worker execution produce identical metadata. The original direct-recipe failure is retained in validation records; a corrected rerun is pending.

- Corrected local regression bundle passes10/10 (132.66 seconds), including actual worker import, direct recipes, the official sample corpus, animation conversion and deeply nested cache/package operations. Actual ImGui model document tests pass, including removal of obsolete warnings after successful reimport. Manual3/3, full formatting and adapted editor syntax pass. Strict sanitizer and native Windows/SDK runs remain in progress; this is an internal checkpoint, not a numbered delivery.
- Added real Windows Import notes captures at100%,150% and200%; native execution/review remains pending.

## Import staging recovery and SDK boundary

- Shared import and canonical animation jobs now hold an OS ownership marker from preparation through joined worker completion. Only the designated worker inherits it; closing the parent's copy cannot make a live child's files eligible for cleanup.
- Cache cleanup/clear-all can reclaim recognized abandoned worker folders after bounded manifest/layout validation. Active jobs, old unmarked folders, unknown contents, aliases and redirects are retained with per-job diagnostics. Authored sources, catalog selections and scene history remain unchanged.
- Separated value-only texture dimensions from cooked texture APIs so installed engine texture identities have a complete, narrow SDK header closure. CMake now checks transitive public FORGE includes against the installation list; relocated SDK tests also check the intended boundary.
- Initial Linux worker ownership and cleanup regressions pass2/2 (2.86 seconds). Full import, SDK, sanitizer and Windows checks remain in progress.

## Native validation corrections

- The Windows audit passed64/69 tests. Its failed Shader source link exposed contextual text prepended to the compiler's first location; shader compiler diagnostics now start on a separate line so the existing contained-source parser can read them.
- The Model Source variant control was unreachable because provenance is not an optional component. Imported models now have a dedicated read-only Inspector section with placement-level variant controls, preserving the schema and Add/Remove Component semantics. A real ImGui regression exercises the placed-root popup without mutating source data.
- The Game-view fallback test incorrectly expected raw magenta after PBR Neutral tone mapping. The exact pinned Diligent shader intentionally desaturates compressed highlights; the corrected test also compares fallback pixels with the explicitly assigned engine error material through the same full frame pipeline.
- Both CI SDK failures confirmed the transitive texture-header boundary fixed above. Windows core now passes the previously failing deeply nested package/cache fixture. Fresh native acceptance is still required; no new numbered package has been issued.

- Combined local validation passes102/102 Linux tests (244.42 seconds), including actual import workers, model recipes, source-free packaging and Vulkan binding/readback. The shared SDK passes79 tests after two sandbox-blocked loopback tests are rerun with local socket access; its installed/relocated consumer passes. Actual ImGui editor/model checks pass2/2 (18.25 seconds), including the newly reachable variant popup. Manual, formatting and adapted native syntax checks pass.
- Renderer diagnostics now distinguish an active initial fallback from retention of a previous complete authored draw. Shared Vulkan recompilation and its binding/readback probe pass after this wording correction; native Windows assertions also check the distinction. Windows and strict sanitizer follow-ups remain pending.
- The unnumbered Windows audit now explicitly builds and runs worker ownership policy fixtures alongside cache cleanup, rendering and editor tests.

- Individual native capture review found selected-row scrolling clipped the asset picker's search box. Search and Clear now remain above an independently scrolling, clipped result list. New actual ImGui checks cover a selected entry in a2000-row list at100%,150% and200%. Capture-only floating window placement also respects the scaled toolbar/status work area.
- Strict ASan/UBSan/LSan model recipe, worker policy, package and cleanup checks pass4/4 (185.62 seconds), without findings or suppressions. The previous checkpoint's native audit passes66/70; its four failures are the shader location, shared fallback assertion and unreachable model variant controls corrected in this bundle. Its deep Windows content-package regression now passes.

- Corrected picker/header checks pass in the actual ImGui harness at all three scales; combined editor/model tests pass2/2 (18.85 seconds). Manual3/3, formatting and adapted editor/native-test syntax pass. Final native capture review remains pending.

## Geometry support documentation review

Corrected the format/render matrices to match existing point and line import, cooking and Diligent draw paths. Line loops/strips normalize to line lists; triangle strips/fans normalize to triangle lists. Ordinary point primitives do not implement Gaussian splatting. Added native indexed/nonindexed point/line readback comparisons, coverage checks and actual captures; fresh Windows execution is still required. No source-format or backend contract changed.

## Packaged rendering walkthrough

Added an original, self-contained Rendering sample project with a checker texture, cube/floor, copper material variant, camera and directional light. The package includes its source and short import/place/Play instructions; no third-party artwork or preselected cooked cache is bundled. The model manual links the walkthrough. The actual FORGE model importer accepts the complete source family. The official Khronos validator reports zero errors, warnings, hints or informational findings. Native visual/package acceptance remains a delivery gate.

## Custom component workflow capture coverage

Added actual editor capture stages for admitted project-component Inspector values, Gameplay Code schema controls and explicit migration review at100%,150% and200%. Fixture schemas travel through the existing supervised worker/history/publication route; gameplay DLLs still never load in the editor. The migration modal now caps its size to the available work area and keeps overflow scrollable. Added an ImGui regression proving the review fits and does not mutate authored values. Native execution remains pending.

Actual100%/150%/200% material captures showed an oversized preview hiding properties in stacked mode. The preview now respects the remaining view height and leaves room for the fields below. Wide documents retain split properties/preview. Corrected test-only popup closing to honor the pinned ImGui nonempty-stack precondition; custom migration review regression passes at all three scales after settled frames. Native refreshed visuals remain pending.

Review of the actual Audio clip capture exposed missing details when Content was closed: catalog refresh completion depended on drawing Content. The owner loop now polls it independently of panel visibility; the clip document shows publication metadata and an honest empty-settings message. Native captures require the published audio record before taking the image. Corrected the format matrix to match the existing audio contract: playback uses Play; an editor audition output lifecycle is not implemented.

## Shader settings and native acceptance corrections

Shader Import now displays declared permutation axes as supported-value drop-downs. Every axis requires an explicit selection; obsolete axes remain visible with a removal action. The bounded source declaration is loaded once per reviewed source generation, not every UI frame. Settings still use the importer schema and publish only through the existing candidate workflow. The generic typed drawer also supports text settings. The shader manual explains missing selections, source refresh, defaults and failed-import preservation. Actual ImGui selection tests pass at100%,150% and200%; the native shader fixture now imports a declared permutation.

The native WARP audit caught GPU budget accounting still using32-bit CPU indices after uploads switched to16-bit compact indices. Both now share the exact index-width calculation; no backend layout assumptions were introduced. The existing native regression detected this defect. Corrected screenshot-fixture scrolling to reveal the actual asset combo below a scaled Inspector, with a focused ImGui regression; this does not change normal user scrolling.

The full sanitizer build also exposed missing runtime linkage in optional upstream shared-library targets consuming instrumented FORGE dependencies. Instrumented target usage requirements and Diligent's common build-settings interface now carry the required sanitizer runtimes. This changes validation profiles only; release builds remain unaffected. Full strict execution and refreshed Windows acceptance are in progress; no new numbered package has been issued.

Validation update: full strict ASan/UBSan/LSan ordinary suite91/91PASS (278.12s); actual editor interaction/process suitePASS (16.32s), including the off-screen picker and permutation selectors at100/150/200. Manual3/3 and source formatting pass. Native Windows source audits remain pending for the corrected code.

The separate Flecs exception check correctly rejected an optimized build without the required allocation frames/source locations. Strict Flecs builds now preserve debug information and disable inlining/tail-call removal for that exact-signature evidence; the test and exception remain unchanged. Linux shared-renderer compilation and the real Vulkan binding/draw/readback probe pass after the GPU accounting correction.

## Final rendering workflow coverage

Extended actual-editor captures to include Camera, Light and Mesh Renderer Inspector sections, Scene lighting and external-source import selection/review at100%,150% and200%. Targeted Inspector captures scroll to the component being reviewed, so enlarged transform controls cannot hide the relevant fields. These additions bring the integrated sequence to103stages. Scene lighting now uses responsive label/value rows and a bounded resizable window; long shadow-setting labels no longer run beyond the panel edge. Updated viewport/lighting manuals to remove stale claims that skinning, transmission, authored Game cameras and engine-mesh migration were unconnected. Fresh native execution remains pending.

Combined Windows package relocation now exercises the shipped Rendering walkthrough through the packaged model/texture importer workers with developer paths removed: import, cache hit, corrupt-source rejection, last-good catalog retention, retry and staging cleanup. This complements shared-runtime Hello and every-file manifest/hash validation; actual final ZIP execution remains a release gate.

Final symbol-preserving ordinary sanitizer suite91/91PASS (271.67s); separate unchanged Flecs expected-upstream signaturePASS (2.69s), with no suppression. Expanded editor/controller checksPASS (16.51s), all103capture-stage code parses in the explicitly adapted Linux syntax check, and manual3/3/format pass. These local checks do not replace the pending native screenshots or final ZIP relocation.

The corrected native audit passes70/71 tests, including all rendered-resource checks. The remaining capture failure revealed that work-area fitting read ImGui's previous-frame `Size` and undid a pending window resize (`SizeFull`). Test-only capture fitting now preserves the requested size, and picker captures keep the target component in view through the scale transition. Added a real ImGui regression for the resize ordering and retained detailed timeout state. No shipped UI or renderer semantics change.

The resize regression also reproduced a popup disappearing after an initial successful open when the next frame moved its parent field outside the clip rectangle. Capture requests now remain active until the image is taken, instead of treating the first `BeginCombo` as settled visibility. The source-backed fixture continues using the real picker and scrolling; it does not bypass the widget or relax the visible-popup acceptance check.

Validation: strengthened real ImGui editor suite passes (16.73s), including the previously failing resize/popup sequence; adapted full fixture syntax and formatting pass. Clean source ff91423 Linux/Windows static68/68 and shared SDK79/79 per host also pass. Fresh corrected native captures and final package are still pending.

## Content tiles at high zoom

Actual200% captures of a short Content panel showed tile names/types below the visible results area. Grid previews now reduce their height to retain a complete readable first row while preserving the preferred tile width. Type icons fit the same preview region; image aspect remains intact. Added an actual ImGui caption-visibility regression and corrected stale thumbnail wording in the Content manual. Native refreshed evidence remains pending.

Content file/selection/thumbnail interaction suite passes (1.56s), including caption visibility at200% in a960×640 window; manual3/3, formatting and adapted editor fixture syntax pass.

## Documentation and native capture reconciliation

Reconciled rendering, asset identity, model/mesh/material/shader, animation and automation documentation with the implemented shared render and authoring paths. Removed stale integration-pending statements for authored cameras, lighting, deformation, material surfaces, placement, reimport and subasset mapping. Retained actual feature limits and the separate final-package gate. The native source audit passes all five renderer tests; its remaining editor capture failure is recorded rather than reported as complete acceptance.

The next native capture reached the custom-component tools stage after successfully capturing the corrected picker and project-component Inspector at all three scales. Its setup had retained the deliberately folded bottom workspace from an earlier stress layout, hiding Gameplay Code. The capture setup now explicitly unfolds that workspace before requesting the tools; visibility checks remain unchanged.

Actual200% picker review found a material-slot caption trailing beyond the Inspector edge. Material slots now use the shared responsive label/value layout, including unresolved assignments, with per-slot hidden widget IDs. Material identity, explicit assignments, Revert and scene history remain unchanged. Added actual ImGui width/nonmutation coverage at100%,150% and200%.

Material editor interaction and scaled-slot regression pass1/1 (1.19s); manual3/3 and adapted editor fixture syntax pass. Refreshed native captures remain required.

Individual capture review also found the LOD Model import and custom surface Shader import documents active but obscured by earlier floating fixture windows. Their capture setup now presents each document in its own foreground window. This changes test placement only; normal saved layouts remain authoritative.

## Final native gate and documentation

The corrected source fa43239 passes the native Windows editor/render test step, including the complete103-stage capture sequence. Individual final image review and the clean combined package remain separate gates. Reconciled remaining stale asset/file-operation/codec/resource and animated-physics documentation with the implemented and executed behavior. The primary validation profile remains D3D12/WARP; full Vulkan/Metal/WebGPU shipping is not claimed.

The complete fa43239 native gate passes71/71 (336.78s), including103editor capture stages. Clean static and shared-SDK profiles pass on both hosts: Linux68/68 (122.15s),79/79 (65.43s); Windows68/68 (331.88s),79/79 (114.18s). Individual review confirms corrected material-slot labels and foreground Model/Shader documents, plus Gameplay Code, rendering component Inspectors, Scene lighting and source-import review at100/150/200.

The final review found the migration Rules label trailing beyond its full-width text field. It now appears above the field; actual ImGui width/nonmutation regression passes (0.35s) at all three scales. No schema, migration semantics, scene history, dependency pin or renderer behavior changed. The clean numbered package will rerun the native capture suite and validate relocation/licenses/manifest integrity.

## Editor interaction and visual-review automation

- Added an input-driven Windows/WARP editor workflow: open creation menus, create and rename a cube, type transform values, Undo/Redo, Save, Delete/Undo, Hierarchy selection, reload with unsaved-change Cancel/Discard, create Camera and Light, zoom to 150%, and exercise Play/Pause/Step/Stop.
- Actions use real submitted widget rectangles and queued mouse/keyboard/text events; the driver does not mutate authored state directly. SDL handles the ordinary interface-zoom event path.
- Added state assertions, actual backbuffer captures, source/build/backend metadata, an action trace, and bounded failure capture. Step must advance exactly one runtime tick while staying paused.
- Included the workflow in Windows source-audit and package validation. Added an optional `audit_workflow_only` source-audit input for focused fixture iterations; the full audit remains the default and output records its scope. Broader staged captures remain separate; visual quality requires inspecting the resulting images and is never inferred solely from passing assertions.
- This automation does not complete the outstanding Phase 7 camera/light Scene guides or certify engine-wide UX parity. Windows execution and image review are being recorded against the committed source.

- Review of the first captures exposed a diagnostic-evidence gap: the workflow now clicks the real Problems tab after Play, includes structured problem details in its trace, and rejects unexpected error/fatal domain diagnostics. Warnings remain visible for review; renderer console diagnostics are still examined separately.
- Fixed a stale-status defect found in the real reload-dialog capture: the unsaved-scene guard no longer shows an earlier “Scene saved” success message. It displays feedback from the current guarded operation, preserving save/discard/cancel behavior.


## Phase 7 authoring follow-up (validation in progress)

- Added Scene-only camera/light icons with selection, effective-transform guides,
  camera projection/basis handling, directional arrows and radial point/spot extents.
  Disabled components remain discoverable; hidden/locked hierarchy policy is respected.
  Personal helper visibility/size/distance controls explain capped or unlimited guides.
- Added an explicit persisted Scene Preview light option for unlit authoring. It borrows
  presentation lighting only; Game, scene entities, asset data and Undo remain unchanged.
  Camera/light-only Inspectors omit irrelevant legacy Blockout Geometry controls.
- Moved Content filters and folder navigation into compact popups to reserve vertical
  space for results at enlarged UI scales.
- Added shared asset actions for menu, context, Inspector, double-click and palette
  routes, with copied targets and busy/type checks. Configured Model placement retains
  its scene/clip/variant choices through its document action. Added an on-demand derived
  cache tool using the existing maintenance service, import draining and clear review.
- Corrected stale animation, prefab, transform, glTF and dependency documentation;
  expanded viewport, lighting and Content how-to pages.
- Extended native input captures for helper picking and lighting modes; added shader
  compile/cooked-realization timings, timestamped Diligent diagnostics, and a same-runner
  editor startup/idle comparison against the immutable accepted Build 260919-000063.
- Focused Linux editor regressions passed (16.46 s), covering guide geometry, negative-Z
  camera basis, infinite far, valid 90-degree spot cones, hidden/disabled selection and
  action guards. Native visual review, renderer diagnosis and package validation are
  still in progress; no new numbered build has been delivered by this entry.

- Follow-up local editor validation passed in16.76s, including compact Content geometry at100%,150%,200%. Benchmark tooling passes PowerShell parsing/actionlint; full native execution and image review remain pending. The source audit now builds matched WARP adapters for both revisions and records process startup/idle samples without assigning a release number.

- Added executable regression coverage for inherited camera/light guide transforms, ancestor visibility/selection locks, orthographic guides, combined camera/light entities, real Command Palette input for asset actions, and native preview-light versus authored/Game rendering isolation. Native results remain pending.

- The rebuilt local editor behavior suite passed17.37s with real palette input for all eight shared asset actions and disabled-state rejection. Manual checks passed3/3. Main/fixture/viewport source syntax, formatting and workflow lint checks pass; Windows rendering and visual review remain separate pending gates.

- The first native source-audit run exposed a baseline-driver compatibility issue: the pre-Phase-7 checkout has no source-stamp helper. Baseline setup now uses it only where available. Performance-only retries can reuse an unchanged current benchmark artifact, with exact source checks, so a baseline-driver failure does not require rebuilding the current editor.

- Follow-up review moved the Content selection context popup into the actual results child window and routed its Reimport item through the shared command adapter. Mixed registered/unregistered selections cannot silently reimport only a subset. Added real right-click coverage and documented configured Model placement through the palette.

- Follow-up editor tests passed16.75s, including actual right-click access to result-selection actions. The rebuilt ASan/UBSan/LSan editor suite passed50.70s with strict halt-on-error and leak checks; no suppression was added. This covers the changed editor code with existing sanitized engine libraries, separately from native GPU execution.

- Cache maintenance clears the previous operation result when starting new work, so a failed operation cannot continue displaying an earlier success. Matched benchmark setup now rejects failure to set the requested window dimensions. Rebuilt Model editor and Content file-operation tests passed2/2 in10.08s.

- Camera/light guide labels reserve space for active transform instructions; combined Camera/Light entities show distinct stacked notes and one entity caption instead of overlapping text.

- Source checkpoint6b9285b passed all Linux/Windows static-core and exact-SDK push jobs plus formatting (run35768691345). Native editor/render/image checks remain in progress; no new numbered package has been issued.

- Windows compile validation found legacy platform macros colliding with local `near`/`far` names in the new camera-guide code. Variables now use explicit corner/depth names. The local syntax check additionally defines those Windows macros. Native run35768723332 stopped before execution; its compiled dependency cache was preserved, and no images or native pass are claimed for that run.

- Source-audit recovery can also reuse the immutable accepted-baseline benchmark, avoiding another baseline build when only current editor compilation changes. Benchmark measurements still require both binaries on the same runner and exact source checks.

- Camera/light guides refresh after move input so they follow the rendered pose within the same frame. Auto-aspect camera guides use the last Game output dimensions, with an explicitly documented Scene-size fallback before the first Game frame. Snapshot generation reads are sequenced after snapshot refresh.

- Corrected the native preview-light test to explicitly name `forge::Viewport`, avoiding Diligent’s identically named viewport descriptor. The accepted pre-Phase-7 benchmark executable built successfully; current-source native execution remains pending this test compile correction.

- Source audits upload native captures and test logs before building the separate benchmark executable and saving caches. This enables earlier review of the same evidence without repeating tests; it does not change acceptance gates or claim measured time savings.

- Native source audit11932c9 compiled and passed all checks except the input workflow, which tried to click an offscreen camera marker. The fixture now uses the Inspector to place that camera at Scene eye height before picking. This preserves real input coverage and viewport clipping; no production picking bypass was added. Frame-wait timeouts remain under investigation.

- The native fixture now records Diligent fence completion after a frame-wait error, plus recovery and Present duration. Signals use the pinned non-flushing Diligent API; no forced wait, backend-specific production synchronization, or diagnostic suppression is introduced. This is diagnostic evidence, not a root-cause claim.

- Reviewing the actual960×640/200% capture exposed overlapping Scene text in a shallow viewport. The gesture footer and helper captions now yield space when too short; navigation and contextual help remain available. Normal-size Content and native authored/preview-light captures were opened and reviewed.

- Native96b24db acceptance passed72/72 checks and all115 real-input steps. Opened camera/light picking, Assets, cache Statistics/Verify, preview-light toggles and default Content150 captures. Shortened the Assets menu labels while keeping palette categories; raised helper names above icons and added a readable backing to guide captions after the captures exposed line/label overlap.

- GPU fence traces show incomplete queued work during the hosted WARP timeout bursts and subsequent completion/recovery. They do not by themselves identify why that work is slow. The editor idle benchmark now settles for ten seconds before sampling, because the measured first-use backlog exceeded three seconds; prior short-settling samples remain explicitly warm-up-sensitive.
