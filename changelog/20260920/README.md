# 2026-09-20

## Phase 7 asset foundation — ongoing internal validation

- Continued the large Phase7 work package begun on September19. The user-approved
  Build260919-000063 remains the latest numbered delivery. No intermediate ZIP
  or Phase7 completion is claimed.
- Asset graph, deterministic cache keys, validated immutable cache, catalog-v2
  dependency migration and bounded asset jobs are implemented as internal services.
- Completed the portable37-test regression pass. Final source refresh, shared SDK
  and focused strict sanitizer checks are being verified; results below will
  distinguish completed checks from pending work.
- Importers, runtime resource adoption, production rendering, expanded Content
  workflows and custom-component authoring remain outstanding Phase7 work.

### Verified internal checkpoint

- Rebuilt portable37/37 and shared-SDK47/47 suites passed. Final focused
  ASan/UBSan/LeakSanitizer cache/catalog/job tests2/2 passed without suppressions.
  Manual3/3, formatting, workflow lint and diff checks passed.
- Local builds reused external build caches. This is not the full clean Phase7
  release gate; Windows/editor/WARP validation for this change remains pending.
- Updated identity documentation to distinguish implemented typed graph/cache/job
  APIs from unfinished importer/resource/editor integration.

- Added regressions for catalog-save interruption before backup and before final
  replacement, preserving both original bytes and unrelated pending contents.
- Dependent jobs reject completion if a prerequisite is superseded during their
  execution, including previously drained prerequisite receipts. Final focused
  regressions2/2 pass in portable, shared SDK and strict sanitizer profiles.

### Asset discovery and command-line inspection

- Added bounded deterministic source scans, content hashes, case-insensitive type
  recognition, hidden/temporary filtering, contained alias handling and structured
  errors. Recognition is separate from successful import/format admission.
- Added debounced change observation with immediate generations, duplicate-event
  coalescing, conservative move evidence, self-write acknowledgements and protection
  against inferring deletions from incomplete scans.
- Added read-only `forge_tools --assets scan/query/dependents` commands and their
  user manual. No editor watcher or automatic catalog move is claimed yet.
- Portable discovery/CLI/existing authoring CLI regressions3/3 pass. Strict sanitizer
  and shared SDK validation of this increment are pending below.

- Raw source-file dependencies now share the catalog's logical-asset dependency
  graph. Primary sources are indexed automatically; additional include/buffer
  edges persist with roles/revisions, reverse lookup and transitive invalidation.
  Added `--assets source-dependents` inspection and source-edge rollback/roundtrip
  regressions. No private importer graph or synthetic source AssetIds were added.
- Catalog loading now validates a detached batch with indexed locator/file aliases,
  cross-record types and one graph-cycle pass, avoiding pairwise path checks and
  repeated growing-graph copies. Added rollback tests and a 1k/10k headless catalog
  characterization fixture; this is not a renderer or whole-editor scale claim.
- Raised the shared catalog byte cap to64MiB after the10k fixture exposed compact
  input versus readable-output size differences. Updated animation/navigation/UI
  snapshot readers consistently; retained100k-record, nesting and parser-event
  limits. Added actual save/reopen comparison to the scale fixture.

### Source foundation validation

- Rebuilt portable Debug40/40 and shared-SDK Release50/50 regressions passed,
  including actual1k/10k catalog save/reopen and dependency equality. The10k
  readable catalog occupies5,128,678bytes. These are local characterization
  workloads, not full-editor performance claims.
- Final focused catalog/discovery/cache/jobs/CLI checks4/4 passed in portable,
  shared SDK and strict ASan/UBSan/LeakSanitizer profiles after final boundary
  review. No sanitizer suppression was added. Manual3/3, formatting, workflow
  lint and diff checks pass. Windows validation remains required before delivery.
- Native Diligent document-loader research/probe confirmed headless metadata
  parsing with deferred image decode at the retained exact pin. No new dependency
  or product renderer/importer integration is claimed from that probe.

### Imported source identity — continued Phase 7 implementation

- Added explicit catalog subasset ownership, durable mapping keys and removed
  member tombstones. Declared members can share their container source without
  allowing unrelated roots to alias a source file.
- The common graph includes active container/member edges. Candidate updates
  reject missing/nested owners, duplicate/tombstoned keys, source mismatches,
  identity/type reassignment and build cycles. Container relocation preserves
  member IDs and updates the family's source locators together.
- Added metadata roundtrip/reorder/rename/duplicate/removed-member/cycle fixtures
  and CLI query details. Semantic glTF correspondence, source sidecars and actual
  importer publication remain in progress; these catalog changes do not complete them.

- Added a bounded, versioned subasset mapping document and candidate reconciler.
  Unique exporter/content/semantic evidence preserves IDs across reorder/rename;
  conflicting evidence produces an explicit ambiguity without changing old state.
  Explicit same-type remap/create-new choices, removed-member restoration and
  fresh-ID container duplication preserve opaque plugin metadata.
- Portable and shared-SDK identity tests passed, including10k reordered members.
  Strict ASan/UBSan/LeakSanitizer identity checks also passed without suppressions.
  Filesystem publication and model-import UI remain outstanding integration work.

### glTF input validation — continued Phase 7 implementation

- Added glTF2.0/GLB2 bounded source capture, contained buffer/image dependencies,
  encoded data URIs, owned source snapshots and required-extension admission.
  Unknown optional GLB chunks are diagnosed/ignored; unsupported required glTF
  extensions are rejected. No network fetching or image pixel decode occurs.
- Added duplicate-key JSON rejection and accessor admission for binary bounds,
  normalized component legality, matrix padding, interleaving, sparse indices,
  numeric metadata and non-finite float rejection. Minimal malformed/truncated
  fixtures exercise rejection before native loading.
- This is internal admission infrastructure. Full glTF semantic processing,
  importer publication, runtime resources and editor rendering remain in progress.
- Added the native Diligent Document adapter using captured-only file callbacks
  and deferred image decoding. Native VertexDataConverter handles admitted float
  attributes/exact integer IDs; FORGE applies sparse patches and matrix padding.
  Source deletion before native load, normalized bytes, uint32 precision, padded
  matrices and sparse zero-backed conversion pass in the headless tooling profile.
- Added an optional native asset-tools build target without a window/device
  requirement. Windows editor builds reuse their existing Diligent targets;
  standalone Linux tools configure required backend archives without loading a
  driver. No dependency pin changed. Complete model/worker/UI integration remains open.
- Broader catalog/identity/admission regressions passed44/44 portable and54/54
  shared SDK. Native tooling's new conversion test also passed without a display
  or usable Vulkan driver. Windows execution and final release validation remain pending.
- Instrumented the native CPU loader/conversion boundary and FORGE adapter for
  strict ASan/UBSan/LeakSanitizer validation; the native fixture passed without
  suppressions. Added a build-boundary check preventing editor/ImGui/SDL linkage
  into the native model tooling test.

### CPU mesh processing — continued Phase 7 implementation

- Added native CPU primitive extraction for core attributes, multiple UV/color
  sets, exact joint streams and additional influence inputs, with format/count/
  direction/material/index validation and computed position bounds.
- Preserved point/line/triangle distinctions; normalized line loops/strips and
  triangle strips/fans deterministically. All source index widths and nonindexed
  geometry have fixtures. Cooked index-width selection remains separate work.
- Added morph stream/base checks, per-document target-count validation and custom
  attribute preservation. Native mesh and loader tests passed under strict
  sanitizers. Renderer, generated normals/tangents, model publication and editor
  import UI remain in progress; subsequent skin preparation is detailed below.


### Model hierarchy, skin and animation admission — continued Phase 7

- Added bounded iterative hierarchy validation,100k-deep-tree regression, scene
  root/membership checks, source TRS/matrix preservation, camera-parameter checks,
  and node/mesh morph defaults. Current authored positive-scale policy is unchanged;
  admitted mirrored/zero source transforms are not yet publishable ECS support.
- Validated ordered skin joint roots/scene membership, inverse-bind layout and
  affine values, default identity binds, all joint indices and weight semantics.
  Added explicit reject/top-four-reduction policy, deterministic tie handling,
  normalization diagnostics, and a256-entry draw palette with explicit overflow.
- Added core TRS/morph animation admission for LINEAR/STEP/CUBICSPLINE, including
  shared payload ownership, time/bounds/count validation, cubic tangent preservation,
  duplicate-target rejection and matrix-authored TRS protection.
- Four focused native model suites passed strict ASan/UBSan/LeakSanitizer checks
  in1.99s without display/device. After shared validation scans and bounded
  influence work were added, the final focused rerun passed4/4 in2.23s.
  No completed renderer/Ozz publication claim or intermediate numbered package.
