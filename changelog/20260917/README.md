# September 17, 2026

## Persistent identity — foundation

- Added distinct 128-bit UUIDv4 EntityId and AssetId value types with canonical parsing/serialization and checked OS randomness.
- Added typed EntityRef, non-inherited PersistentEntityId, and membership-scoped WorldContext resolution with missing/unloaded/ambiguous outcomes and reverse lookup.
- A scene AssetId is its durable document identity. Editor sessions and loaded membership scopes remain separate. No DocumentId, loaded AssetHandle or dependency upgrade.

## Scenes, migration and authoring

- Scene-v2 embeds AssetId and canonical EntityIds. Known parent/base links migrate without changing world-space transforms or prefab semantics.
- Legacy file opening validates a detached migration and records UUID assignments in a companion identity record; the source stays intact. Save retains the original backup and atomically writes v2. Conflicting records/sources fail with preserved data.
- New objects and subtree copies use fresh UUIDs. Whole-scene Save As allocates fresh scene/object IDs, remaps known internal links, and starts separate history only after saving succeeds. Save As requires a new destination; ordinary Save retains identity.
- Kept opaque/plugin payload internals unchanged and added explicit legacy aliases. Reads and authoring targets now expose canonical identities, while legacy API inputs remain accepted through the compatibility adapter.
- Added user instructions for migration, backups, Save As and identity conflicts. No editor layout, renderer, transform, clock or native ABI changes.

## Asset references and metadata

- Added a UI-independent forge_assets target with typed AssetRef<T>, explicit asset metadata registration/persistence, source relocation, and type-checked resolution.
- Missing, unregistered and incompatible assets have distinct results. Duplicate IDs/locators and escaping paths are rejected; replacing a scene file at the same path with a different AssetId cannot satisfy the old reference.
- Added identity/migration/duplication/scope/asset tests, including actual Windows blocked-file replacement. Windows editor CI explicitly builds/runs the new suite.
- Asset loading handles, importers, cook pipelines and a project-wide conflict browser remain deferred. The current five numeric components do not contain arbitrary typed reference fields; known hierarchy/base links are remapped, and a typed helper serves future known fields without scanning opaque payloads.

## Migration recovery refinement

- Backup creation also publishes through a neighboring temporary file and atomic replacement. An interrupted backup cannot leave a partial final backup that blocks all later migration retries.
- Added failed-backup-publication and exact-original-byte regression checks. Reservation 260917-000022 was superseded before package dispatch to include this correction; its number remains consumed.

## Format validation

- Reject unsupported format numbers without narrowing the JSON integer. Regression cases include oversized values that previously wrapped to 1 or 2.
- Build260917-000023 is a validation candidate only, superseded for delivery by this correction. Its number remains consumed.

## Windows test portability

- Corrected a Save As regression assertion to compare explicit strings on MSVC, avoiding a mixed string/JSON C++20 overload-resolution error. Product behavior is unchanged.
- Build260917-000024 passed Linux/Windows core9/9 and formatting, and compiled editor source files, but the editor test target failed on that assertion. No package24 was delivered.

## Validation

Local Linux core/API/runtime/native/plugin suites: 9/9 passed. Targeted ASan/UBSan/LeakSanitizer identity/lifetime/API/core suites: 4/4 passed. Manual tests: 3/3 passed; format/whitespace and workflow checks passed. Windows-target syntax checks passed; this is not Windows execution. Local editor/process/input/migration/native iteration suites: 2/2 passed. Clean Windows **Build 260917-000025**, source `d98c0a5387506caa5983ad76afb12a52da3b4c9e`, passed all jobs in [run 35167379024](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35167379024): core **9/9** (5.18 seconds), editor/native/process/identity/API/WARP **8/8** (15.73 seconds). Linux CI and formatting also passed. All **27** rendered PPM fixtures match Phase 1 Build 21 byte-for-byte. Shader compilation and all three executable build identifiers passed.

Verified ZIP CRC, manifest/source/build identity, three x64 PE executables and all 23 packaged manual sources/HTML build identifiers. ZIP SHA-256: `3cad6664a82b18e75a8f5f814a02cee62ccdce1429434a698cb28e9b22c89d06`. Build 25 is the delivered package; Build 21 was archived and redundant staging/extractions removed. Interactive acceptance on the user's GPU remains separate.

## Documentation consistency

Updated authoring API discovery, world ownership/resolution and project save/recovery documentation to match scene-v2 and the AssetId-only document model. This delivery-evidence update changes documentation only; packaged product source remains `d98c0a5`. Phase 2 is complete. Phase 3 is not started.

## Phase 3 — transform math and evaluation

- Added dependency-neutral double affine math, normalized float quaternions and positive local scale; preserved the existing Rz * Ry * Rx rotation convention.
- LocalTransform is an assembled value only. Authored storage uses independent LocalTranslation, LocalRotation and LocalScale channels; WorldTransform is transient derived state.
- Added shared effective spatial-parent policy and iterative parent-first evaluation with cycle detection and cached unchanged results. No second authored hierarchy, dependency update, fixed clock or structured-prefab implementation.
- Core math covers compound/pole/180-degree orientations, inverse/decomposition, shear/singularity rejection and a 20,000-node evaluation chain. Full bundle validation is recorded below as integration completes.

## Phase 3 — Flecs ownership, scene-v3 and spatial authoring

- Registered independent inheritable local TRS components, instance-owned non-inherited WorldTransform, and owned SpatialBinding. Transform reads/undo/edit stay inside the existing WorldContext; derived writes do not dirty authored state.
- Added FollowStructure, World and membership-scoped Explicit EntityRef binding. New parenting preserves world placement and follows its parent; keep-local remains explicit. Missing/non-transform/cross-scene attachment targets stay unresolved with diagnostics.
- Scene-v1/v2 migrate to v3 without changing UUIDs, independent channel ownership or old world-space behavior. Owned Euler values become normalized quaternions; inherited rotations remain inherited. Exact original files survive `.v1.backup`/`.v2.backup` publication and blocked-save retries. Unknown data remains opaque; legacy rotation extras have a retained nested field.
- Channel-specific local/world operations, independent revert, exact quaternion copying, preserve-world compensation, cycle/shear rejection, dependent detachment, spatial-reference remapping and history use one shared authoring/evaluation path. Only necessary reparent compensation channels become owned.
- Existing native displacement now handles parent motion once per entity; native ABI and externally stepped timing remain unchanged.
- Local Linux core tests: 10/10 passed. Targeted ASan/UBSan/LSan tests: 5/5 passed. Coverage includes generated prefab-child derived ownership, inherited-channel gestures, v2 filesystem retry/IDs, same-asset membership scope, deletion rejection and opaque/internal/external reference duplication. Windows execution is pending at this commit.

## Phase 3 — editor, affine rendering and user guide

- Inspector exposes local Position/Euler Rotation/Scale, a compact Space selector and per-channel revert; unresolved attachment recovery is explicit. Global help/scale/layout remain intact.
- Move/R/S gestures preview through shared authoring operations and commit one undo step. Translation owns only translation; R owns only quaternion rotation; S owns only scale. Copy/paste retains quaternion values and deliberately writes all local channels.
- Rendering, bounds and picking consume evaluated affine transforms, including hierarchy-induced shear. GPU normals use explicit inverse-transpose columns. Added a D3D12 WARP fixture comparing projected sheared cube faces, picking and expected lighting; existing 27 accepted images remain comparison inputs.
- Updated function-based manual pages and technical ownership, API, migration, units, tolerances and deferred-scope documentation. No editor layout redesign or new dependencies.
- Local editor/process/input/native suites: 2/2 passed (11.81s), including camera/drag/R/S/scale/document/history regressions. Windows-target core/editor/render-test syntax, formatting, actionlint and manual tests passed. Clean Windows build/render/package validation follows; interactive desktop acceptance is separate.

### Native generated-prefab regression caught before delivery

Final review found that the first native movement adapter considered authored rows only. Flecs-generated prefab interiors also need movement. Runtime translation now prepares local translation writes directly from membership-scoped live Flecs transforms, including generated children, using the same effective-parent/evaluation policy. It validates all results before applying, preserves generated handles, and compensates moving parents so children move once. New regression covers both legacy World-bound and FollowStructure generated children. Core 10/10 passed after correction. Candidate Build 260917-000026/run35172331058 was cancelled and superseded before delivery; its number remains consumed.

Final portability check renamed the transform test's `near` helper to avoid Windows' legacy `near` macro. Reservation 260917-000027 was superseded before dispatch; no package was produced. The generated-child correction passed local core10/10, editor2/2 and targeted sanitizer5/5 before this test-only rename.

The new affine WARP test camera was preflighted using the production transform math. Its original angle covered only one face above the test's visibility threshold; the corrected angle covers two, including a shear-affected normal. This is a test-fixture correction; product code is unchanged. Build260917-000028 passed Windows/Linux core and formatting, then was superseded before editor/package delivery for this correction. The replacement remains a clean build.

### Guide consistency

Synchronized the Scenes, first-scene tutorial, Viewport, Diagnostics and native gameplay pages with format3, local coordinates, inherited parent motion, quaternion schema and unresolved attachments. Technical project/native/dependency pages now describe the same active contracts. Manual generation/link/build-identity tests pass3/3. This follow-up changes documentation only; clean candidate29 continues verifying the identical product implementation before the final matching-manual package.
