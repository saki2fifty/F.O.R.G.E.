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

### Windows fixture validation

Clean Build29 compiled the Windows editor/runtime/tools and all four shaders; Windows core10/10 and editor/native/input/transform/identity/world/API suites passed. The new WARP fixture failed before drawing its new hierarchy because its `entity.create` setup passed JSON null instead of an empty object. Corrected to explicit `Json::object()` and executed the complete fixture setup, picking and two-normal coverage locally. The existing27 render images were produced; the new GPU lighting assertion still requires the replacement Windows run. Build30 was cancelled before packaging because it contained the same test setup. No failed ZIP was delivered.


### Phase 3 verified delivery — Build 260917-000031

Clean [Windows run 35173713866](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35173713866), packaged source `e2841c61006891b98b830c857ff7407022144ba3`, passed every job: Windows core10/10, editor/native/process/transform/identity/world/API/live/CLI/WARP9/9, Linux core and formatting. All four shaders compiled and all three Windows executables reported the reserved build ID. The new affine/shear WARP fixture passed projected-face picking and inverse-transpose lighting checks. All27 accepted Build25 render fixtures are byte-identical; the new hierarchy image was visually inspected.

ZIP CRC, manifest hashes, reserved source/build identity, three x64 PE executables and23 matching manual source/HTML pages were verified. Package promotion archived Build25 and left only the current ZIP and executable folder; cleanup dry run is a no-op. Interactive desktop acceptance remains the user's check. Phase3 stops here; fixed timestep, structured prefabs and physics/animation remain deferred.


## Phase 4 — runtime clock, presentation and native activation

- Runtime owns monotonic time: 60 Hz default, centralized 1..240 Hz runtime option, 250 ms elapsed clamp, eight-tick catch-up cap, counted dropped whole-tick debt and preserved fractional remainder.
- Explicit Flecs fixed simulation pipeline orders native gameplay and final transforms; presentation interpolates live local translation/quaternion/scale through the shared hierarchy without authoring or simulation writes.
- Protocol2 adds session/request correlation, removes caller dt, and bounds nonblocking inherited-pipe traffic. Play progresses without editor step requests or continuous output consumption.
- Toolbar Pause/Resume and single-tick Step; Console tick/rate/debt diagnostics and contextual help. Authored scene and editor camera/UI behavior remain separate.
- Native ABI1 layout unchanged. Disposable probes run a real fixed tick; live loads remain pending until their first completed fixed tick. Paused reload never advances automatically. First-tick failure restores the previous artifact and boundary checkpoint; Stop cancels, newer candidates supersede, restarted timing/presentation reset.
- Editor and Python native tooling share pending/active semantics. Tests cover clock math, hierarchy interpolation, independent pipeline dt, paused steps, autonomous progression, blocked output, stale sessions, probe/load/first-tick failure, rollback, supersession and pending Stop. Existing identity, migration, prefab channel ownership, history and UI tests retained.
- Updated function-based manual and technical runtime/protocol documentation. No scene migration, dependency change, gameplay input mapping, structured prefabs, physics, animation or general SDK added.
- Local Linux core 11/11 and SDL/ImGui/editor native 2/2 passed during integration; ASan/UBSan/LSan 6/6, Windows-target syntax, manual 3/3, formatting and workflow lint also pass. Actual Windows/render checks and build identity to be recorded after execution. These are automated checks, not Windows desktop acceptance.

- Windows CI follow-up: include the new runtime clock test executable and sample module in the editor job's explicit build target list before selecting runtime tests. Build260917-000032 core Windows11/11 and Linux11/11 passed; its editor run was superseded before packaging to fix that missing-target configuration. No Build32 package delivered.

- Windows polling follow-up: Build33 passed core11/11, shader compilation and WARP rendering, but SDL editor handshake/native probe tests timed out. Bound both controller/runtime writes to 1KiB fragments and retain bounded multi-fragment pumps; add an explicitly nonblocking parent/runtime framing test with a payload larger than pipe capacity and clearer timeout diagnostics. Build33 was not packaged or delivered.

- Run Windows process-controller tests before compiling the renderer so handshake/reload failures are found earlier; avoid rerunning those suites in the later render/core selection.

- Build34 core11/11 passed the new nonblocking pipe regression; editor-test compilation rejected the expanded diagnostic's string argument. Pass the diagnostic through the existing C-string assertion helper. The preceding local editor rerun used an older executable after its rebuild failed, so it is not evidence for that revision; rebuild successfully before rerunning and recording final editor results. No Build34 package delivered.


### Phase 4 verified delivery — Build 260917-000035

Clean [Windows run 35182526241](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35182526241), packaged source `1e76cb8190602c957ba7c488e3124e63b3d6c792`, passed every job: Windows core **11/11** (18.77s), early SDL/editor/native **2/2** (15.44s), and remaining renderer/runtime/transform/identity/world/API suites **9/9** (5.88s). Linux core and formatting also passed. The actual Windows controller tests confirm the nonblocking pipe correction; all requested pending/first-tick reload and recovery scenarios pass. Four preview/grid shaders compile; all three executable build IDs match.

All **28** accepted Build31 viewport images are byte-identical. Final local editor rebuild succeeded before **2/2** tests passed (14.44s); updated Windows-target syntax, Linux core11/11, sanitizer6/6 plus pipe1/1, manual3/3, formatting and workflow lint passed. ZIP CRC, manifest hashes, reserved source/build identity, three x64 PE executables and23 matching manual source/HTML pages were verified. ZIP SHA256: `34cfbf8898186b052b7ece22d2de2e16b636b1592123c9da2c893be597c51c27`.

Build31 was archived. Package cleanup leaves only the current numbered ZIP and executable folder; cleanup dry run is empty. Builds32–34 were superseded/failed and never delivered. Phase4 implementation and automated validation are complete. Interactive Windows desktop acceptance remains for the user. No Phase5, structured prefabs, physics, animation or dependency changes are included.


## Phase 5 — structured prefab implementation

- Added independent AssetId-backed prefab schema 1, UUIDv4 member identities and scene4 stable instance-member mappings. Ordinary scenes/legacy ChildOf prefabs keep their existing behavior.
- Immutable Flecs Prefab/IsA revisions use Parent interiors; dynamic attachments remain ChildOf. Candidate hierarchies are prepared before single-file publication, with explicit retirement of old templates.
- Explicit component/scalar override intent includes equal-value edits; independent local TRS channels and scene-owned Revert/Undo/Redo are preserved. Source publication affecting this scene establishes a clearly documented scene-history boundary.
- UI-independent source create/duplicate/publish, instance creation/duplication, bounded project discovery, stable references, missing/removed-member diagnostics and transient runtime dependency snapshots. Content and Inspector provide the corresponding prefab controls and source window.
- Added dedicated prefab regression coverage and isolated-runtime snapshot/native-tick checks. Expanded function-based manual and technical schema/lifetime/publication docs. Local and actual Windows validation results will be recorded after execution; no delivery claimed yet.
- Apply to Prefab is deliberately deferred, with no UI/API workflow. Nested/structural overrides, Unpack and Phase6 remain out of scope. No dependency pins changed.

- Final local validation: core **12/12** (11.13s), rebuilt editor/UI/recovery/native **2/2** (14.23s), ASan/UBSan/LSan **8/8** (21.76s), Windows-target syntax for editor/core/UI/render tests, manual **3/3**, formatting, workflow lint and diff whitespace checks passed. Source history name Revert, nested/legacy selection rejection, stable duplicate-name UI IDs, and missing-parent agreement between live transforms and detached previews are included. Actual Windows/WARP and ZIP verification remain pending.

- Windows Build36 passed Linux/Windows core and formatting but MSVC rejected three reversed JSON/string comparisons in the prefab source window. Compare explicit string values instead; no Build36 package was delivered. Added regression assertions for restoring removed members and dynamic attachments, equal-value rotation/scale inheritance, name Revert history and invalid dependency graphs. Rebuilt targeted core and ASan/UBSan/LSan tests pass.


### Phase 5 verified delivery — Build 260917-000037

Clean [Windows run 35189296706](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35189296706), packaged source `aebe8fa99ab4139e9733be031a8eca257ca25c14`, passed every job: Windows core **12/12** (25.96 s), early SDL/editor/native **2/2** (15.08 s), and remaining prefab/render/runtime/transform/identity/world/API suites **10/10** (6.33 s). Linux core and formatting passed. All four preview/grid shaders compiled and all three executables reported the reserved build ID.

All **28** accepted Build35 viewport images are byte-identical. The new structured Parent/IsA prefab image is byte-identical to the ordinary hierarchy/shear fixture and was visually inspected. Final local correction validation passed editor **2/2** (14.30 s), targeted prefab **1/1** (3.82 s) and targeted ASan/UBSan/LSan **1/1** (15.24 s), following the earlier complete core12/editor2/sanitizer8 suites.

ZIP CRC, manifest hashes, source/build identity, three x64 PE executables and **24** matching manual source/HTML pages were verified. SHA256: `29f17efa8461d01ddb46c5e7b7354ce617092c278fea4c3403adeac8e97e6bcc`. Build35 is archived; packages contains only the latest numbered ZIP and current executable folder. Cleanup dry run is empty.

Phase5 implementation and automated validation are complete. Interactive Windows desktop acceptance remains for the user. Apply to Prefab is deliberately deferred; Revert is implemented. Stop before Phase6: no physics, audio, animation, navigation, broad SDK or editor layout redesign was added.


## Phase 5.5 — core services foundation (validation in progress)

- Extend the existing project manifest to version2 with validated simulation frequency, stable startup scene identity and project-owned input configuration. Preserve version1 reading/backup-on-save and unknown fields; personal preferences and runtime state stay separate.
- Add SDL-neutral digital/1D/2D actions, UUIDv4 action identities, keyboard/mouse/gamepad bindings, immutable fixed-tick snapshots and a built-in runtime monitor. Protocol2 carries bounded input batches; native ABI1 is unchanged. Explicit Scene capture, Esc/F6/F7 routing, focus/disconnect resets and project settings UI preserve editor layout.
- Consolidate project-relative path normalization/root confinement, schema discovery/preparation conventions and built-in bootstrap dependency validation. Proven scene identity journals, migration and prefab revision semantics remain format-owned.
- Add EngineContext-owned, capability-restricted diagnostics/profiling with bounded storage and owner-thread/lifetime checks. Instrument current simulation/transforms/prefab boundaries; retain existing Console messages and frame metrics. Loaded AssetHandle, generic tasks/VFS and major subsystem/SDK work remain deferred.
- Add core/headless protocol, settings persistence/failure/path-case, service lifetime, injected-clock profiling, SDL virtual-gamepad and UI tests. Legacy document fixtures now use independent untitled identities and a true version1 manifest instead of overwriting a version2 startup asset reference.
- Initial Linux core14/14, editor2/2, sanitizer10/10 and Windows-target syntax checks passed; final expanded checks and actual Windows delivery remain pending. No Apply to Prefab or Phase6 work.

- Final local checks passed: Linux core **14/14** (11.84s), rebuilt SDL/ImGui/process/native **2/2** (15.20s), ASan/UBSan/LSan **10/10**, Windows-target syntax, manual **3/3**, formatting and workflow lint. Settings tests cover stale drafts, blocked atomic writes, moved/duplicate startup identities and null startup. A compiled-out profiling probe verifies no clock access or records. Actual clean Windows/MSVC/WARP results follow delivery.


### Phase 5.5 verified delivery — Build 260917-000038

Clean [Windows run 35194395680](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35194395680), packaged source `82b2c9518808e2025ef6e7ea4a96b5667ef58442`, passed every job. Windows core **14/14** (25.84s), early SDL/editor/process/native **2/2** (14.07s), remaining services/input/prefab/render/runtime/transform/identity/world/API suites **12/12** (6.29s), including D3D12 WARP. Linux core and formatting passed. Four preview/grid shaders compiled; all three executables reported the reserved build ID.

All **29** accepted Build37 viewport fixtures are byte-identical. ZIP CRC, manifest hashes, source/build identity, three x64 PE executables and **26** matching manual source/HTML pages verified. SHA256: `8ca8a15daf240acd25697ac2891cf3870372cfb9f3b85e8677374592181558e5`. Build37 archived, disposable staging removed, and package cleanup dry run is empty. Final local sanitizer suite passed10/10 in23.80s.

Phase5.5 implementation and automated validation are complete. User acceptance: change/save/reopen Simulation Hz; add a digital Space action; Play/Capture and inspect Console counts; F6 Pause/Resume, F7 Step, Esc release; then existing prefab/transform smoke checks. See [Project Settings](../../manual/editor/project-settings.md) and [Gameplay input](../../manual/editor/input.md). Interactive desktop acceptance remains for the user. Native ABI1 action access, AssetHandle, VFS/task framework, Apply to Prefab and all Phase6 integrations remain deferred. No dependency upgrade or dock layout redesign.
