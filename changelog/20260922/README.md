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
