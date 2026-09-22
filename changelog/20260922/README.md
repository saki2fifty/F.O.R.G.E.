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
