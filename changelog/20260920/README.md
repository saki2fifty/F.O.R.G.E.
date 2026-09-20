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

### Importer registration and settings — continued Phase 7 infrastructure

- Added immutable native importer declarations, sealed startup registration,
  bounded source probes, explicit platform/profile support and deliberate
  selection when multiple providers match. Shared selections retain provider
  lifetime; registration order does not choose a winner.
- Added typed bounded importer settings with defaults, explicit equal-value
  overrides, reset/repair, preserved unknown envelope data, explicit version
  migration and effective-value digests. Document-envelope and importer-settings
  versions are separate. Failed edits/parses preserve the previous document.
- Portable registry/settings tests passed2/2; shared-SDK2/2 and strict
  ASan/UBSan/LeakSanitizer2/2 also passed. These are infrastructure APIs, not a shipped import dialog or
  completed production importer/worker/publication path.

- Extended the existing asset supervisor with configurable bounded job limits and
  an Import command using flat outputs and cooperative cancellation followed by
  forced termination. Existing converter defaults remain. Output folders/links/
  count/size checks and pre-cancelled/stale-staging rejection are covered.
- Added Windows CPU-time limits and Linux parent-death/process-group cleanup.
  Initial new-worker plus legacy-converter tests passed2/2 in31.96s. After lifecycle
  corrections, affected portable regressions passed9/9 in38.47s, shared-SDK9/9
  in33.53s, and strict sanitizer worker/registry/settings checks4/4 in32.19s.
  The existing known-upstream Flecs include check remains separately labeled;
  the focused sanitizer suite uses no suppression. This is not yet a completed
  production import dispatcher or last-good publisher. Windows execution remains pending.

- Final worker review also rejects a successful exit observed after the wall-time
  deadline. Targeted new/legacy worker reruns passed2/2 portable(32.02s),2/2
  shared-SDK(31.97s), and2/2 strict sanitizers(32.09s).

## Signed and zero visual transforms (Phase7, in progress)

- Expanded the existing independent LocalScale component to finite[-10000,+10000], including zero and float-representable tiny magnitudes. No authored matrix/combined ECS authority, identity or dependency pin change. JSON/API values outside the range or nonzero values underflowing float storage reject before commit.
- Separated valid forward hierarchy evaluation from inverse availability. Inversion now checks a scale-normalized reciprocal condition estimate; world editing resolves the actual spatial parent without unnecessarily inverting the child. Signed/rank-deficient TRS decomposition preserves prior sign and quaternion continuity where available and verifies recomposition. Reparent, channel ownership and independent Revert remain intact.
- Added scene5/prefab2 numerical compatibility using existing version gates only when extended scale values are written. Positive-only documents retain their prior versions; known negative zero is normalized without traversing unknown payloads. Updated prefab catalog publication and exact SDK numerical-contract fingerprint.
- Updated Inspector/Meta ranges and help, small-number display, and S-axis gestures to cross zero into negative multipliers. Added an inverse-unavailable explanation separate from visual validity. Geometry picking uses double arithmetic and safely declines singular/ill-conditioned local inverses; Hierarchy selection remains available. Forward bounds remain ordered.
- Primitive preview now uses reflection-specific culling state, correct outward triangle orientation, and cofactor normal directions without singular inverse/NaN generation. Planar and singular draws explicitly remain two-sided. Added asymmetric/mirrored/zero/nested-parity WARP fixtures; their actual Windows execution/capture review is still pending. This does not claim the later Phase7 imported normal-mapped/skinned renderer is finished.
- Audited pinned Jolt5.6.0 per current collider: centered boxes, spheres and capsules support signed scale by symmetry; sphere/capsule require uniform magnitudes. Actual shape validators reject zero/too-small scale. Magnitudes are baked only into these symmetric collider dimensions; visual signs, Dynamic ancestry policy and last-good candidate behavior remain unchanged. Navigation extraction corrects mirrored winding and skips collapsed triangles.
- Added signed/zero/tiny roundtrip/history/parenting/picking/normal/bounds/prefab intent tests, reflected collider and recovery coverage, presentation interpolation through zero, and real official gltf2ozz→FORGE admission→Ozz sampling/model-evaluation tests. STEP, LINEAR and linear-equivalent CUBICSPLINE fixtures pass locally; arbitrary cubic conversion still uses the existing30Hz approximation and is not claimed exact.
- Validation checkpoint: rebuilt Linux core47/47(55.80s), shared-SDK57/57(30.29s), and local editor/input/native harness3/3(12.91s) passed. Five focused strict sanitizer suites passed; the real-conversion animation suite hit its former60s timeout, then passed independently in60.44s with ASan/UBSan/LeakSanitizer enabled. Its sanitizer-only timeout is now180s. The known Flecs include exception remains separately labeled, not a global sanitizer-clean claim. An actual pre-change SDK tools binary rejects scene5 and retains its previous scene. Added a regression retaining scene3 for the previously valid .001 JSON boundary. Windows execution/capture review remains pending. No numbered package reserved or delivered; complete Phase7 remains in progress.

## Asset publication and official fixture (continued Phase7 work)

- Added an owner-thread asset publisher using the existing project lease, immutable DDC, importer contracts, typed graph and member identity mapping. Complete root/member candidates, source/settings/profile/dependency revisions, format admission and compatibility preflight are checked before selection. Sources and disk baselines are checked again before commit.
- Added versioned adjacent import sidecars with durable member mappings/settings/build inputs and preserved unknown data. Catalog selection records retain actual build/artifact/digest/profile metadata. Removed members remain tombstones.
- Added an asset-specific durable recovery record for sidecar/catalog publication, with the catalog as commit point. Failed/cancelled operations preserve the prior selection; startup recovery refuses external byte conflicts. Version1 catalog backups and platform flush/replacement behavior remain explicit. Production provider/editor/resource adoption is still in progress; these tests do not claim a complete import workflow.
- Publication plus existing catalog/subasset tests passed3/3 locally in8.31s, including fresh-process interruption at three commit stages, first-import cancellation, stale input, failed validation/compatibility and external-edit preservation. Expanded final checks, sanitizers/shared-SDK and Windows replacement validation remain pending.
- Added the unmodified official Khronos NegativeScaleTest files, exact revision/hash provenance and CC-BY-4.0 attribution. A native-source parity/hierarchy/geometry regression is being validated; full imported textured rendering remains separate.
- Signed-scale follow-up: all six focused strict ASan/UBSan/LeakSanitizer suites passed in62.50s with the measured animation timeout. Cross-platform/render CI is still being checked. No numbered delivery has been created.

- Actual Windows editor render fixture passed at source72fcc8e. The new reflected-cube test exposed a test assumption: WARP quantized identical ambient shading to76/77 at the exact .3×255 rounding boundary after mirrored triangle interpolation. Captures have identical coverage and at most one RGB code-value difference. The regression now permits only that measured quantization difference while keeping coverage and alpha exact; complete viewport rerun remains required.

- Publication follow-up adds a monotonic selection generation, separate from artifact identity, so an equal-value settings-intent change has an unambiguous commit point. A cancellation regression covers that exact case. Full metadata/provenance and official-native fixture checks continue.
- Windows Debug core CI passed46/47; the10k asset round-trip subprocess exceeded90s. Removed duplicate canonical filesystem resolution during catalog validation and added per-stage round-trip timing to measure the correction. Release Windows shared-SDK and both Linux profiles passed on the preceding checkpoint. No performance threshold was relaxed.
- Unnumbered Windows audits now build/run publication and official sample tests, and save a compatible cache only after validation succeeds, keyed to the audited immutable source. No build-number allocation or ZIP delivery is involved.

- Publication checkpoint: full portable core48/48 passed51.44s. The final reserved-path/alias correction passed focused publication8.43s; shared-SDK publication/catalog tests passed3/3 in13.89s; strict ASan/UBSan/LeakSanitizer publication/catalog passed2/2 in9.43s. Official NegativeScaleTest native admission passed0.10s under strict sanitizers. Format, manual links and workflow lint passed. Windows open-file recovery and complete viewport rerun remain pending.
- Windows compilation found a C++20/MSVC overload ambiguity in the new recovery test's string-versus-JSON comparison. The expected JSON value is now explicitly extracted as a string; runtime publication code is unchanged. This compile failure prevented that run's Windows tests from executing; corrected-source validation is required.

## Cooked meshes and CPU resource lifetime (Phase7 in progress)

- Added a runtime-only mesh representation and bounded version1 little-endian artifact: named float/integer vertex streams, point/line/triangle indices, material slots, exact local bounds, ordered LOD metadata and morph targets/defaults. Native admitted glTF geometry cooks through this path, including the unmodified official NegativeScaleTest. Node scale remains separate from geometry.
- Added typed owner-scoped resource tickets, immutable strong/weak leases and bounded asynchronous CPU preparation. Concurrent requests coalesce; adoption rechecks cancellation/generation/dependencies and compatibility, failures retain previous-good data, and explicit fallback preserves the requested failure state. Retired tickets stop advertising readiness. CPU destruction and weak promotion respect owner-thread lifetime.
- Added retained-allocation estimates, shared candidate/live/retired budgets, idle eviction, manual unload/reload, late-completion rejection, inspection snapshots and explicit tooling waits. An actual cooked mesh provider checks file digest and format before adoption. GPU resources, production consumers, installed SDK exposure and Content integration remain pending; this is an internal milestone.
- Corrected sanitizer propagation for the new CPU-only targets, which intentionally do not link the ECS core. Fully instrumented mesh/resource tests passed2/2 in0.15s under strict ASan/UBSan/LeakSanitizer; direct ThreadSanitizer concurrency/lifetime tests also passed. Portable tests passed2/2 in0.04s. Fully instrumented official sample cook/reload passed; Windows checks for this new bundle remain pending.
- Preceding checkpoint c75b52d passed all Linux/Windows core and shared-SDK jobs. Actual Windows Debug publication passed0.96s; the previously failing catalog-scale test passed152.84s without relaxed limits; full Windows core48/48 passed283.13s. Complete editor/WARP audit is still being checked. No numbered package has been created.

- Mesh/resource shared-SDK profile also passed2/2 in0.03s; the fully instrumented official NegativeScaleTest cook/runtime reload passed0.11s. Format/manual/workflow checks passed. GPU retirement source audit confirms native Diligent queue/fence support; actual provider/render integration remains pending.
- The Windows editor source audit found the same MSVC C++20 string/JSON comparison ambiguity in the official sample's provenance test. Both expected digest and byte count are now extracted to their actual types; the remaining glTF source/tests were inspected for that comparison pattern. This run stopped during compilation and supplied no new rendering result.
- Unnumbered source audits also retain compatible partial compilation after a configured build/test failure, under a distinct partial-cache key. This avoids discarding compiled dependency objects after a small test-source correction. Restores still reconfigure, rebuild every selected target and rerun tests; cached objects never count as validation or a deliverable.

### Texture preparation and explicit resource variants (Phase7 in progress)

- Added bounded cooked CPU texture data for2D/arrays/cubemaps/volumes, mip/block
  layouts, normalized/float/BC formats, color/normal/HDR/alpha metadata and sampler
  validation. Meshes and textures share the checked cooked-file envelope.
- Added actual cooked texture loading with content verification and previous-good
  resource preservation. One AssetId can have simultaneous named semantic/backend
  variants; replacement/unload remains scoped to its variant and lease revision.
- Added private PNG/JPEG/TGA/HDR preparation through pinned codecs and Diligent mip
  processing/BC encoding. Explicit no-mip handling, max-size mip selection, normal
  orientation/normalization, linear-light sRGB filtering and alpha premultiplication.
- Exact-source review found Diligent's PNG read callback ignores supplied size.
  Use its existing libpng dependency through a bounded C adapter with strict CRC,
  complete-input and error-cleanup checks. No vendor patch or dependency upgrade.
  The similarly unchecked native TIFF route is not selected for texture imports.
- Portable and shared-SDK mesh/texture/resource tests3/3 pass; fully instrumented
  ASan/UBSan/LSan3/3 pass0.29s. Native image fixture passes including truncated PNG
  rejection; expanded codec-instrumentation rerun and Windows tests pending.
- GPU textures/viewers/container import/publication integration and full Phase7
  acceptance remain in progress. No new numbered Windows build has been created.

- Expanded codec sanitizers exposed IJG libjpeg integer DCT and Huffman signed
  shifts. The image adapter uses JPEG decoding from Diligent's existing pinned stb
  source; private symbols, bounded input and no vendor patch/suppression.
- Unnumbered Windows audit35496082128 passed viewport1.14s and editor10.25s.
  Its remaining official-model hash failure was reproduced as Git CRLF conversion;
  fixture-specific attributes now preserve the exact upstream bytes. Full rerun
  remains required, with failure diagnostics identifying the affected file.

- Final channel review preserves grayscale alpha for color, R/RG channel intent
  for data, and opaque HDR/RGBE alpha. Uses native pixel swizzles/premultiplication.
  Strict native codec/official-model2/2 passed0.22s, CPU3/3 passed0.29s,
  shared-SDK3/3 passed0.03s; Windows texture validation remains pending.

## Texture containers and Basis compression

- Added bounded private KTX1/KTX2 asset-tool adapters: supplied mip chains,
  arrays/cubes/volumes, supported raw/BC formats, native container inflation,
  ETC1S/UASTC transcoding and prepared RGBA8 Basis encoding.
- Added explicit color/normal/data metadata checks, glTF Basis admission mode,
  packed/unpacked RG preservation and BC4/BC5/BC7 or CPU targets. KTX1 row padding
  and endian conversion use the native converter; KTX2 uses tight checked layout.
- Selected exact official stable KTX4.4.2 with no graphics uploader, no OpenCL/SSE,
  scalar codec configuration and disabled non-open-source ETC unpacker. Packaging
  collects its license directory and notices. Existing dependency pins are retained.
- Recorded observed upstream encoding-allocation, typed-alignment and metadata
  cleanup issues. Selected existing official codec and lifetime APIs that avoid
  those paths; no source patches, defect exceptions or sanitizer suppressions.
- Strict rebuilt artifact/KTX/image tests3/3 passed0.27s, including separately
  allocated truncated inputs, invalid extents/descriptors, channel semantics,
  arrays/cube/volume ordering, odd dimensions, cancellation and byte budgets.
  This is worker adapter infrastructure; Content import/GPU integration remains
  part of the active Phase7 package. Windows KTX validation is pending.
- Previous texture/resource bundle73e2a54 passed all four core CI profiles and the
  unnumbered Windows audit8/8 in11.85s, including viewport/editor render and exact
  official glTF provenance. No numbered delivery was created.
- Optional zlib-supercompressed KTX is explicitly rejected after strict testing
  exposed the pinned miniz typed-alignment path. The selected glTF Basis formats
  remain ETC1S/UASTC with no supercompression or Zstandard where applicable.

## Additional texture formats and source admission (Phase7 in progress)

- Added private native DDS preparation for legacy/DX10 normalized, floating and
  BC formats, supplied mip chains, arrays, cubes/cube arrays and volume slices.
  Pre-load bounds and aligned ownership precede Diligent parsing. BGRA/BGRX and
  color luminance conversion use native utilities. Preserve DDS custom-channel
  metadata; Basis encoding rejects it instead of silently treating it as opacity.
- Added BMP preparation through the existing pinned stb decoder, with complete
  row/palette checks, mask validation, top-down orientation and explicit alpha.
  Unsupported native header/profile variants receive conversion diagnostics.
- Added official stable libwebp1.6.0 at4fa2191 for bounded lossy/lossless still
  images. Native demux/decode, RGBA ownership, dimensions, file/chunk lengths and
  whole-file admission precede image processing. Animation/ICC conversion remain
  separate unsupported capabilities. Packaged notices now include PATENTS/AUTHORS.
- Added HDR/RGBE flat/RLE pre-load extent validation after exact-source review
  found unchecked native short reads. No vendor patch or sanitizer suppression.
- Set KTX's official archive version override to4.4.2, independent of Git tags.
- Fully instrumented ASan/UBSan/LeakSanitizer texture tests passed6/6 in0.45s,
  including BMP/DDS/WebP, malformed/truncated inputs and native compression.
  Windows validation of this new bundle is pending. Previous e7da1b8 Windows
  audit passed9/9 in12.07s including KTX and actual editor/viewport rendering;
  its Linux/Windows core and shared-SDK CI profiles also passed.
- These remain private preparation APIs. Publication, Content/editor integration,
  GPU realization and complete Phase7 acceptance continue; no new numbered package.

## Isolated texture imports and simultaneous usages (Phase7 in progress)

- Added the bounded `forge_asset_build` worker and private image/container recipes.
  Owned snapshots, manifest hashes, portable filename checks, process budgets,
  cancellation, completion markers and parent-side validation precede publication.
  Windows packaging/install targets include the worker; editor workflows continue.
- A texture now publishes a bounded index and independent color/data/normal/HDR
  files under one AssetId. Additional usages publish together; color/data runtime
  selections coexist and preserve distinct linear-light mip results.
- Import revision keys include source, codec configuration, compiler and build
  profile. Container settings preserve supplied mips; no ignored resize controls.
- Actual normal-process recipe/cache/publication/resource regressions passed4/4
  in1.60s. Strict ASan/UBSan/LeakSanitizer direct recipe/bundle/mesh checks passed3/3
  in1.14s. Covered malformed manifests, wrong hashes, missing/duplicate files,
  stale/corrupt inputs, cancellation, DDS supplied mips, failed publication and
  prior-good resource retention. Production process limits remain unchanged.
- Mesh morph deltas now require matching base streams and compatible layouts;
  regression rejects an orphan normal delta and accepts the valid counterpart.
- Formatting, manual links and workflow syntax checks passed. New Windows worker
  validation is pending. Previous c2e3fb8 Windows audit passed12/12 in10.72s and
  all four core/SDK push profiles passed. No numbered build was allocated.

## Shared import commands and texture settings document (Phase7 in progress)

- Connected source discovery/hashing, verified cache hits and supervised cooking
  to the existing bounded asset queue. Owner-thread publication retains the project
  writer lease and rejects changed sources, settings or selected asset families.
  Independent queued imports can publish without clobbering unrelated catalog rows.
- Added `forge_tools --assets import` through that shared service, typed override
  validation and structured results. Worker lookup uses the actual executable
  directory, including PATH launches. Relocation can rebuild from sources/sidecars
  without the disposable cache and retains AssetId/build identity.
- Added Content's Texture import document and texture asset open handler. It uses
  importer-owned typed fields, multi-choice usages, defaults, cancellation, progress,
  Save focus and Apply/Discard/Keep Editing guards. Asset import stays outside scene
  Undo. New text-button wrapping and modal bounds preserve narrow/high-DPI access.
- Added explicit StringList choices with duplicate/unsupported-value admission;
  unconstrained string lists retain prior semantics. Initial UI testing exposed
  and corrected the schema mismatch before accepting the workflow.
- Normal actual worker/CLI/service/queue/settings checks passed7/7 in6.41s; strict
  ASan/UBSan/LeakSanitizer checks5/5 in3.78s; shared-SDK service checks4/4 in2.27s.
  Actual ImGui editor adapter imported/reimported through the worker, retained its
  AssetId/history boundary and closed its modal correctly:1/1 in0.80s on Linux.
- Added two Windows editor render captures for texture settings at100% and200%.
  Their execution/review is pending. Manual links, formatting and workflow syntax
  are checked; the offline manual's supported Markdown subset is retained.
- Previous worker checkpoint27e2cfb passed Windows audit15/15 in13.27s. No numbered
  package was created. Model/material/shader/GPU/skinning/custom authoring and full
  Phase7 validation remain open; a registered texture is not a GPU preview.

### Native model compression and quantized geometry (in progress)

- Add exact stable meshoptimizer1.2 privately to offline asset tooling, with license packaging and strict native instrumentation.
- Admit ratified EXT compressed attribute/index buffers with native filters, bounded output, fallback-reference checks and original-source provenance retention.
- Reject undefined filter inputs, malformed/truncated buffers and newer KHR-only bitstreams rather than silently decoding them as EXT. KHR remains an unenabled release candidate in the checked registry.
- Accept required KHR_mesh_quantization attribute/morph formats through native conversion; normalize rounded directions, preserve tangent signs, compare normalized bounds and accept valid tightly packed four-byte vertex elements.
- Local glTF regression suites pass8/8 normal and8/8 strict ASan/UBSan/LSan; added compressed-image/nonfinite cases pass both profiles. Exact upstream meshoptimizer tests also pass with assertions and strict sanitizers. Windows checks for this increment remain pending. Full Phase7 importer/rendering integration and final numbered delivery remain in progress.

### Mesh preparation and morph-safe geometry optimization

- Connect native mesh cooking to reusable private preparation with supplied-direction preservation, missing flat normals, Mikk-compatible tangents, selected normal-map UV sets and explicit recalculation recipes.
- Generate each morph target's normal/tangent deltas, retain glTF base tangent handedness and report changed target signs or collapsed-direction fallbacks.
- Split mirrored UV seams and remap all channels together, including exact integer joints, every skin-weight set, custom channels and morph deltas. Native custom equality avoids the sixteen-stream convenience API limit.
- Keep source primitive order by default; only explicitly order-independent material slots enable triangle-cache reordering. Recompute bounds and preserve material/morph metadata.
- Condition temporary native position/UV data to keep tangent calculation valid at tested1e-25 and1e25 scales without altering cooked source coordinates.
- Four focused cooking/preparation regressions pass normal and strict ASan/UBSan/LSan profiles. Format, workflow and manual checks pass; Windows validation for this increment is pending. Full model-worker publication, UI and production rendering remain in progress.
- Prior texture service/editor checkpoint2f60c02 passed all18 unnumbered Windows audit tests; normal and200% texture document screenshots inspected. No new numbered package is delivered yet.

### Draco admission and exact tangent conditioning

- Add official Draco1.5.7 at immutable8786740 privately to native model tooling,
  with glTF bitstream2.2 options, native object sanitizer coverage and packaged
  licenses. Diligent's optional TinyGLTF Draco bridge stays disabled; FORGE validates
  official decoder output before building ordinary Diligent input buffers.
- Check compressed ranges, declarations, unique attribute mappings, exact layouts,
  normalized flags, counts, index widths, source immutability, output budgets and
  cancellation. Retain ordinary extra attributes, sparse attribute overrides and
  morph data. Shared decoded accessors must agree.
- Preserve decoded triangle connectivity for source triangle lists, strips and
  nonindexed primitives. Diagnose unsupported sparse strip-index remapping rather
  than guessing a discarded source sequence.
- Source/test evidence corrected an overly strict trailing-data rule: the pinned
  Draco writer includes up to three zero alignment bytes in compressed views.
  Accept that padding while rejecting other trailing data. Official upstream Box,
  generated geometry and minimal malformed/truncated fixtures cover this boundary.
- Tangent preparation now uses an exactly reversible power-of-two working scale;
  a coordinate range that would lose small values rejects instead of silently
  collapsing them. Original cooked positions/UVs are unchanged.
- Normal glTF/preparation10/10 passed in0.47s; strict ASan/UBSan/LeakSanitizer10/10
  passed in3.53s. Native standalone roundtrip also passed strict sanitizers. Format,
  manual links and workflow syntax passed. Windows validation for this increment
  remains pending; CI explicitly builds/runs the new fixture.
- Previous0fa1422 Windows source audit passed20/20 in13.61s. No numbered build was
  allocated. Complete model publication, production rendering and the rest of the
  authorized Phase7 package remain in progress.

### glTF surface admission, texture bindings and material variants

- Add source-backed core and extension material validation before native loading,
  with material/sampler/field diagnostics. Reuse Diligent's native material factor
  handling while retaining independent authored alpha, normal scales, occlusion
  strength, emission channels, standalone IOR and dispersion.
- Preserve specification-valid IOR zero, negative normal scales, alpha cutoff and
  specular colors above one, and reversed iridescence thickness ranges. Omitted
  volume attenuation distance keeps its infinite/default meaning.
- Preserve binding-specific sampler choices, all six minification modes, explicit
  non-mip LOD clamp, arbitrary admitted source UV-set addresses and signed/zero
  texture transforms. One image retains distinct color/data/normal usages.
- Validate Basis/WebP alternative source declarations and fallback requirements;
  selected images still require the existing real codec admission. Remove unused
  PBR bindings for unlit/legacy workflows without changing captured provenance.
- Retain candidate-local material-variant mappings, allow duplicate display names,
  enforce reference/aggregate bounds and reject conflicting primitive assignments.
  Missing variant mappings retain ordinary primitive behavior.
- Normal full glTF/preparation11/11 passed in0.46s; strict ASan/UBSan/LeakSanitizer
  11/11 passed in3.49s. Formatting, manual3/3 and workflow syntax passed. Explicit
  Windows target/test selection includes the new regression; Windows execution
  remains pending. No numbered package allocated.
- These are import-side capabilities. Stable family publication, material authoring,
  production GPU effects and the remaining Phase7 integrations are still in progress.

### Cooked material values and resource safety

- Added bounded identity-neutral cooked Material values: typed scalar/vector/linear
  colors, alpha/depth/culling intent, texture semantic/dimension, binding-specific
  sampler and UV transforms. Existing Texture sampler serialization is reused.
- Native admitted glTF material factors now cook into the same material format;
  texture identities remain publication-owned rather than source-index identities.
- CPU material loaders validate digest, exact typed binding selections and layout
  compatibility. Existing resource generations support binding changes with unchanged
  cooked bytes, preserve held old leases and retain last good values on failure.
- This is material data/resource infrastructure; model publication, shader reflection,
  GPU effects and end-user material editing remain in the active Phase7 work package.
- Validation: normal material/texture integration6/6 passed, strict sanitizer5/5
  passed, including every truncated material envelope, invalid typed fields,
  binding remap with unchanged bytes, held old leases and failed layout/digest
  replacement. Manual3/3, formatting and workflow checks passed. Windows validation
  of this material consumer remains pending.

### Static model cooking and immutable source transport

- Added a private bounded glTF snapshot transport preserving shared GLB/image
  storage, compressed placeholders, original source descriptions and dependency
  provenance. No project file access occurs during snapshot decoding.
- Added native static model candidates containing separate Mesh/Material/Texture
  members, semantic variants, reference bindings and immutable affine hierarchy.
  Complete candidate validation rejects broken references, material slots, image
  usage mismatches, cycles, corrupt outputs and unexpected files.
- Stable subasset evidence is independent of array positions and display labels.
  Official NegativeScaleTest exercises source reorder/rename and shared image
  color/data/normal variants without adding duplicate texture identities.
- Texture bundles can use bounded generated filename prefixes within the existing
  flat artifact directory. Existing unprefixed bundles remain compatible.
- Model skin/animation, camera/light, material-variant stages and editor/CLI model
  publication remain part of ongoing Phase7 integration; this is an internal CPU
  checkpoint, not a completed importer or renderer.
- Validation: combined native model/snapshot/texture tests5/5 passed normally
  (2.51s), strict ASan/UBSan/LSan4/4 passed (4.82s); actual texture worker regression
  remains green. MIME/extension byte mismatches and missing material UV streams
  reject. Formatting, manual3/3, workflow and whitespace checks passed. Windows
  validation of the new model stage remains pending.


### Model worker, whole-family publication and tools

- Added a fixed glTF model recipe to the isolated asset worker, with immutable
  source/dependency capture, exact recipe/toolchain fingerprints and bounded
  memory, time, staging and output limits. The current recipe supports static
  Mesh/Material/Texture families; required remaining model stages are documented.
- Connected model candidates to the shared import service and atomic catalog/
  sidecar publisher. Typed graph bindings resolve candidate-local slots to durable
  AssetIds; removed members become tombstones. Ambiguous correspondence returns
  structured alternatives and requires an explicit same-type/new-asset decision.
- Extended `forge_tools --assets import` with model selection and optional identity
  decisions, retaining the same project writer ownership and last-good behavior.
  Added an end-user model tools page and exact current limitations.
- Aligned service and publisher cache admission with bounded importer output limits,
  allowing complete model families beyond the previous256-file default. A260-mesh
  fixture verifies initial publication and cache reimport.
- Corrected member dependency compatibility projections after the first end-to-end
  test rejected an incomplete record before publication. Normal8/8 checks passed
  in44.59s; strict ASan/UBSan/LSan4/4 passed in34.04s. Coverage includes real worker/
  CLI, source reorder/rename, ambiguity/explicit choices, removal, relocation,
  corrupt/stale/cancelled candidates, compatibility rejection and texture regression.
- Recorded an exact Ozz0.17.0 converter finding: matrix-authored static parent rest
  channels disappear in an animated clip unless represented as explicitTRS.
  The official-converter reproduction measured0/0.499878 versus5/5.49988 for the
  equivalentTRS fixture. The model-animation correction remains pending.
- Windows evidence: prior material/resource source172bc02 passed24/24 audit tests
  in11.35s (run35509366990). Current model-worker Windows validation is pending.
  No numbered package was issued; full Phase7 remains in progress.


### Model cameras, lights and material variants

- Model cooking now preserves perspective/orthographic cameras, directional/point/
  spot lights, node camera/light references, independent visibility/selectability
  intent, and complete material-variant primitive mappings. These are validated
  immutable asset values; corresponding scene controls/rendering remain in progress.
- Shared source/cooked camera admission preserves omitted infinite far/automatic
  aspect and valid negative orthographic magnification. Light admission enforces
  linear color, nonnegative intensity, type-appropriate ranges and spot cones.
- Variant materials enter the mesh dependency graph and retain duplicate display
  labels. Parent validation checks primitive references and available texture UVs
  for both base and variant materials. Different normal-map UV sets remain allowed;
  the exact native PBR gradient path is recorded for later GPU integration.
- Normal model/hierarchy/worker5/5 passed in38.21s; strictASan/UBSan/LSan4/4 passed
  in36.80s; rebuilt model/textureCLI and worker regression4/4 passed in7.20s.
  Manual3/3 and formatting passed. Windows validation for this increment is pending.
  No intermediate numbered package; complete Phase7 implementation continues.


### Model skeletal conversion admission

- Added bounded canonical animation input for the official pinned gltf2ozz tool,
  preserving required ancestors, unique transient joint lookup names, expected
  native joint order and rest matrices. Source files and authored ECS identities
  remain unchanged.
- Corrected matrix-authored fallback rest channels in FORGE's private input without
  patching Ozz. Kept signed/zeroTRS explicit and distinguished asset rig numeric
  admission from ECS LocalScale write limits.
- Preserved morph curves separately and retained their clip duration even when all
  rig transform channels are already animated. Constant time-zero clips use an
  explicit1second duration; cubic tail extension preserves preceding derivatives.
- Added converter-output rejection for mismatched joint order/parents/rest values,
  clip count/duration, malformed archives and unexpected files. Exact converter
  execution and actual admitted sampling cover matrix rest fallback, signed/zero
  scale, morph timing, constant clips and the separate Ozz rest numeric range.
- Whole-model worker composition, resource binding and GPU skinning remain in
  progress; this internal stage is not a completed animated-model workflow.
- Windows model-worker source6f81e11 audit35512121158 passed29/29 in20.93s.
  The skeletal converter/admission tests passed2/2 normally(0.27s) and2/2
  under strictASan/UBSan/LSan(3.59s). The first sanitizer attempt found a
  missing converter executable; configuring the existing exact-pin converter
  resolved that test setup issue. Manual3/3, formatting and workflow checks passed.
  Windows validation of this new stage remains pending.

### Supervised model conversion and morph curves

- Reused the asset process supervisor and disposable job directory for a sequential
  official gltf2ozz stage. Generated input/output names, URIs and configuration are
  restricted; source/accessor admission precedes launch. Failed/cancelled conversion,
  missing tools, changed inputs and unexpected files cannot publish a candidate.
- Added immutable glTF morph STEP/LINEAR/CUBICSPLINE evaluation with exact endpoint
  semantics and tangent interval scaling. Negative and greater-than-one weights
  remain valid; malformed curves, invalid rig targets and arithmetic overflow reject.
- Exercised the real converter through its bounded process runner, verified staging
  cleanup and retained texture worker regressions. Whole-model importer composition
  and runtime/rendered skinning/morph application remain in progress.
- Validation: combined normal model/texture/CLI/converter/morph8/8 passed in46.93s;
  strictASan/UBSan/LSan4/4 passed in32.77s. Manual3/3, formatting, workflow syntax
  and whitespace checks passed. The earlier canonical-stage Windows audit is
  still running; no numbered package has been produced.

### Prepared mesh skin palettes

- Added explicit per-draw palettes mapping up to256 draw joints to the source
  skin's ordered joints. Bound meshes now prepare four normalized influences using
  the existing explicit Reject/ReduceToFour policy, with reduction/normalization
  diagnostics and validation against every skin using that mesh.
- Prepared palettes use cooked mesh version2; ordinary/unprepared meshes retain
  version1. Older readers reject the new feature rather than misreading its joint
  indices. Palette bounds, duplicate joints, normalized weights and feature/version
  agreement are checked on encoding and decoding. CPU resident-size accounting
  includes palette storage; remapping preserves all prepared streams.
- Corrected an overly strict source-weight check using the exact glTF specification:
  finite float weights must be nonnegative, but summing to1 is a recommendation.
  Renormalizable weights above1 are now accepted. Prepared draw weights still must
  be normalized; quantized source sums retain their exact requirement. Unused skin
  attributes on an unbound mesh remain raw data, without forced reduction/clamping.
- Normal mesh/material/texture/model checks7/7 passed in42.85s; strictASan/UBSan/LSan
  checks6/6 passed in35.98s. Tests include source2+2 normalization, raw unused weights,
  shared-skin joint bounds, palette round trips and invalid prepared data. Final
  review removed per-vertex heap allocation from duplicate-joint checks and added
  palette byte-count overflow checking. Manual3/3 and formatting passed.
- The user-facing skin import option is introduced with the complete animated-model
  recipe, not exposed as an unused setting in the current static recipe. Whole-model
  skin/skeleton/clip publication and GPU application remain ongoing Phase7 work.

### Complete model skeleton and clip families

- Joined native geometry preparation and the supervised official Ozz converter in
  the shared model import service. The converter runs after the geometry worker
  exits. An incomplete animation stage cannot enter the selected asset catalog.
- Publish Model, Mesh, Material, Texture, Skeleton and AnimationClip members as one
  validated family. Clip-to-skeleton references use typed catalog dependencies;
  original model source paths and the existing durable identity service remain.
- Preserve ordered skin-to-Ozz joint mappings and inverse-bind matrices. Validate
  every skin-bound draw palette, model/rig ancestry and rest agreement, morph target
  counts, archive files and source/converter provenance before publication.
- Added source-array-independent rig/clip matching evidence and tests for node/clip
  reordering and clip renaming. Removed clips retain tombstone records. Invalid skin
  candidates leave the previous complete catalog family selected.
- Added effective import settings for excess skin influences, animation sampling
  rate and optimization. Updated the model tools manual and technical contract.
- Added bounded named external-tool revisions to common build inputs. Inputs with
  tools use key version 2; tool-free inputs retain their exact version 1 shape.
  The converter executable digest changes the cache key without changing the
  registered importer revision or inventing a source-file/AssetId dependency.
- Validation caught the initial importer-revision mismatch before publication; the
  existing guard was preserved. Corrected normal publication, model, texture, CLI
  and build-key regressions passed 9/9 in 53.05 seconds. Strict ASan/UBSan/LSan
  checks passed 6/6 in 57.86 seconds; the additional animated CLI and missing-tool
  regression passed in 2.88 seconds. Manual, formatting and workflow lint pass.
  Windows validation for this composition is pending. The preceding palette and
  supervised-converter source audit (8cf4f51) passed 32/32 in 17.73 seconds.
- Runtime model resource adoption, scene instantiation, skin rendering and editor
  model controls remain required Phase 7 work. No intermediate numbered ZIP issued.

### Shared animation rest transforms and selected model reads

- Extracted shared CPU glTF transform admission and Ozz rest-input conversion.
  The animation-only importer now uses the same correction as model conversion:
  a matrix-authored parent retains its transform while a child animates. Original
  source bytes are unchanged; unsupported matrices reject before conversion.
- Added actual legacy-runtime matrix-parent sampling and malformed-matrix retention
  regressions. Normal animation/model checks passed 5/5 in 41.46 seconds; strict
  ASan/UBSan/LSan converter, hierarchy and animation checks passed 3/3 in 11.36 seconds.
- Added selected-revision loading to the common derived-cache reader. Runtime/tool
  callers can validate cooked bytes without source discovery or conversion. A failed
  read reports the problem without moving cache entries or changing catalog selection.
- Added a CPU model-family reader that validates root/member ownership, exact
  publication generation, recipe and converter provenance, hashes and typed bindings.
  Mixed catalog revisions and cancelled loads reject. Model/animation admission now
  builds independently of source codecs and the editor.
- Normal selected-model and cache regressions passed 3/3 in 37.33 seconds. Strict
  ASan/UBSan/LSan selected-reader checks passed 2/2 in 34.34 seconds. Core-only
  animation and target-boundary checks passed 2/2 in 30.76 seconds with the
  source codec tools disabled. These readers do not yet claim live resource
  adoption or rendered model support.

- Windows source audit of936512c found MSVC C++20 rejecting a reversed
  string/JSON comparison in model clip admission. Made the string extraction
  explicit and aligned selected-reader comparisons with the supported ordering.
  No validation predicate was removed. The rebuilt Linux model recipe passed
  in16.91seconds; corrected Windows validation is pending.

### Cooked-model animation playback and resource ownership

- Connected published model Skeleton/AnimationClip members to typed asynchronous
  CPU resource pools and the existing animation runtime. Clip readiness depends on
  the selected skeleton, and players retain both matching immutable leases.
- Use the full model key and publication generation for compatibility. Preserve
  held revisions through replacement, cancellation, unload and failed admission;
  clear samplers before resource-owner shutdown. Reuse a selected catalog snapshot
  across model member requests; no source conversion occurs in Play.
- Copy sampled local TRS directly from native Ozz SoA values, preserving signed and
  zero scale without matrix decomposition. Evaluate companion morph curves using
  the same interpolated runtime time. Native memory accounting includes allocations
  omitted by archive-length estimates and Ozz's name-excluding `Animation::size()`.
- Allow background asset adoption while paused. Pending or invalid bindings cannot
  advertise a complete recovery checkpoint. Gameplay replacement waits for a real
  checkpoint; crashes before it exists require fresh Play. Candidate recovery uses
  a bounded quiescent wait and validates every playback entry before changing any
  player's time/sampling context.
- Added real imported-model resource/runtime tests for coalescing, typed mismatches,
  generation replacement, corrupt replacement retention, cancellation, shutdown,
  absent original source, morph timing, paused loading and exact recovery. Added a
  separate-process CLI-import/play/crash/reimport recovery regression.
- Initial normal runtime checks3/3 passed in20.65seconds; strictASan/UBSan/LSan
  checks3/3 passed in37.98seconds. Expanded normal checks7/7 passed in50.95seconds; strict checks4/4
  passed in40.66seconds. The separate instrumented model-runtime process test and
  actual editor native build/reload/watch regression also passed. Core-only
  animation, process and target-boundary checks passed3/3 in36.34seconds. Windows
  validation of this runtime composition is pending. Corrected test assumptions about catalog dependency projection and
  process-specific recovery session identity; initial failures are retained in logs.
- Updated animation/model manuals and resource/timing contracts. Model scene
  instantiation and GPU deformation remain required Phase7 work; no numbered ZIP.

- Follow-up Windows compilation reached model publication and found the same
  reversed string/JSON pattern in converter-provenance comparison. Corrected it
  and reviewed every added comparison since the preceding passing Windows source
  audit. Exact nlohmann3.12 C++20 operators accept nonscalar conversion on the
  JSON-left side; FORGE now uses that supported ordering. Rebuilt model recipe
  passed in20.55seconds. Windows rerun pending.
