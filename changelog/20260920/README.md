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
