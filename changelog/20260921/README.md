# Changes — 2026-09-21

## Phase7 rendering integration — in progress

### Complete draw-resource preparation

- Prepare mesh, selected material slots and typed texture variants from one copied
  catalog publication. A complete candidate owns exact immutable CPU revision
  leases; GPU adoption remains a separate presentation-owner step.
- Cancel only the requesting consumer, preserving shared/coalesced loads. Reject
  stale catalog epochs and failed dependencies without returning partial draws.
- Keep missing authored material-slot keys as explicit unresolved diagnostics;
  never redirect them by index or label. Source files are parsed by resource
  workers, not by this presentation-side coordinator.
- Validation is in progress. No new numbered package or Phase7 completion claim.

### Physical draw bundles

- Construct a complete native mesh/material/texture bundle before replacing its
  previous owner. Keep GPU leases alive through binding destruction and mark their
  use for fence retirement. CPU resource owners can close after successful upload.
- Add native rendering checks for complete draw equivalence, failed replacement
  preservation and CPU/GPU lifetime separation. Native execution remains pending.
- CPU draw-candidate model recipe passed1/1 in33.72s before switching its preliminary
  override limit to the existing4,096-entry material-slot validator. The corrected
  source requires its rebuilt validation; no new authored limit is introduced.

### Shader fixture linkage correction

- Make raw mesh-fetch vertex and pixel stages use one shared varying structure.
  The previous pixel signature placed color in register0 while the vertex stage
  wrote it in register1. Local compiler disassembly confirms the mismatch and
  corrected layout; Windows FXC/WARP validation remains required.

### Scene mesh resource connection

- Connect the Scene viewport to the project resource host. Adopt complete GPU
  candidates, invalidate retained frames after loading, retain previous draws on
  failure, and send entity-specific resource diagnostics to Problems.
- Apply mesh/light layer masks, full-affine bounds culling and authored LOD
  thresholds. Mesh Renderer takes precedence over legacy primitive preview.
- Release old scene bundles on project changes; forward published texture catalog
  snapshots. Avoid fence/flush work when a residency owner has no new GPU use.
- Rebuilt cold-loading model recipe passes1/1 normally in29.87s and under strict
  ASan/UBSan/LSan in69.89s. Native bundle/viewport validation is pending; Game,
  standalone and production material/pass integration remain in progress.

### Valid back lighting

- Treat a punctual light behind every contributing surface layer as a valid zero
  contribution before evaluating its half-vector. An opposite-view back light
  must not display a numeric-error color. Coincident point lights and overflowing
  contributions retain their explicit numerical rejection.
- Updated the native regression; local14-stage shader compilation passes. Windows
  execution of this correction is pending.

### Windows diagnostic evidence

- The49841b4 audit built and passed36/37 selected tests. Native D3D12 message660
  confirmed the raw-fetch fixture's mismatched COLOR registers. The shared-layout
  correction is in189051f and awaiting its native run.
- All four Windows/Linux core and exact-SDK jobs passed for49841b4. These results
  do not yet validate the later Scene integration or back-light correction.

### Clearcoat material layer

- Connect clearcoat intensity, roughness and independent normal maps to the pinned
  native PBR layer. Preserve the red/green data channels and separate geometric
  normal when a clearcoat normal map is absent.
- Reuse a shared pixel tangent-frame reconstruction for base and coat normal maps;
  normalize mapped directions before combining basis vectors to avoid overflow.
- Add native clearcoat response and reflection regressions. Windows execution is
  pending; this does not complete advanced material or production pass integration.
- Clearcoat checkpoint: local14-stage HLSL compilation and native C++ header/source
  syntax checks passed, including textured coat branches and Scene host wiring.
  Format and manual3/3 passed. Native FXC/WARP execution remains required.

### Iridescence, sheen and directional reflections

- Connect iridescence factor/thickness textures to the pinned spectral evaluator.
  Preserve actual IOR/specular reflectance when combining material extensions.
- Connect sheen color/roughness to native layer shading and its shared preintegrated
  lookup resource. Keep context/resource ownership explicit across complete bundles.
- Connect anisotropy strength, rotation and texture direction to the native BRDF,
  preserving tangent handedness and requiring the specified tangent-space inputs.
- Add native effect, zero-film/zero-sheen, directional-rotation and missing-frame
  regressions. Windows execution is pending; local14-stage shader compilation passed.
- All four core/SDK jobs passed for189051f. The corresponding rendering audit is
  still running; these results do not claim Windows validation of later layers.
- Reflection-layer checkpoint: native C++ syntax checks, all14 local shader stages,
  format and manual3/3 passed. FXC/WARP tests remain pending.

### Scene queues and HDR display

- Sort visible mesh parts into opaque, masked and blended queues. Blended ordering
  uses camera-relative depth; stable identities break ties. Apply per-part bounds
  culling and material/mesh/parity locality without changing ECS identity.
- Render editor lighting into RGBA16F, then resolve through pinned PBR Neutral tone
  mapping and exactly one sRGB transfer. Composite the grid after tone mapping.
- Add persistent Scene View Exposure with contextual help. Preserve previous target
  resources if resize allocation fails; reject invalid display inputs explicitly.
- Add native transparency/cutout and HDR/exposure regression captures. Local16 shader
  stages and native C++ syntax pass. CPU queue/bounds tests pass normal1/1 and strict
  ASan/UBSan/LSan1/1; Windows validation of this increment remains pending.
- Earlier189051f source audit passed all37/37 Windows editor/render tests in53.16s,
  including complete GPU bundles, tangent-handedness and resource retirement. It
  predates advanced reflection layers, queue sorting and HDR display.

### Environment resources and combined material bindings

- Reuse the existing GPU residency owner for cached environment convolution. Keep
  CPU revision identity, reserve source plus full output-mip payload bytes, and use
  existing fence retirement and failed-candidate accounting.
- Feed native diffuse/GGX/Charlie environment maps to prepared PBR draws, with
  explicit intensity/rotation and a zero-light default. Scene environment selection
  and sky authoring remain ongoing; this checkpoint does not expose them as complete.
- Share identical material samplers and compatible lighting samplers while preserving
  distinct state. Add combined15-texture native coverage, convolution reuse/budget,
  disabled IBL, constant-environment rotation and retirement regressions.
- The a3cf2ee Windows audit passed37/37 in58.10s, validating clearcoat/back-light,
  iridescence, sheen and anisotropy. Its core/SDK checks also passed. HDR, queues and
  environment consumers require the next Windows source audit.
- IBL checkpoint: all16 local shader stages, native C++ syntax, normal/strict
  material tests1/1 each, manual3/3 and format pass. Native validation remains pending.

### Scene lighting and selected environment assets

- Add scene-owned environment identity, intensity, rotation, sky visibility and
  exposure settings with shared validated authoring, Scene Undo/Redo, unknown-data
  preservation and detached presentation transport.
- Load standalone and model-member texture variants through copied catalog selections
  on workers. Verify complete cooked bundles and publication provenance; failed or
  unavailable variants retain prior usable resources.
- Connect environment GPU adoption to mesh hosts; update all parity/LOD native bindings
  before retiring old maps. Reuse pinned Diligent EnvMapRenderer for sky sampling and
  far-depth drawing, with camera-relative rays safe for infinite-far cameras.
- Add Scene lighting controls and the shared asset picker catalog-snapshot overload.
  Scene and the current Game preview share prepared mesh/environment resources; Game
  uses authored scene exposure. Authored-camera integration remains in progress. A full standalone exporter is future work under the approved scope.
- Local normal checks pass4/4: render bounds/sky rays, texture recipe, model recipe and
  authoring API in35.02s. Initial command-catalog count assertion was updated for the
  new shared command and rerun successfully. Native C++ syntax including unmodified
  upstream sky/PBR code passes. Sanitizer and native Windows execution are pending.
- Strict ASan/UBSan/LSan validation passed4/4 in77.04s with leak detection enabled.
  Editor lighting-header syntax passes against local SDL/ImGui headers. Manual3/3
  and formatting pass after correcting a wrapped list for the offline manual format.
  An additional HDR catalog-selection regression is being validated separately.
- Added HDR catalog-selection regression passes normal1/1(.93s) and strict
  sanitizer1/1(1.68s): auto-color preserves RGBA32Float HDR; explicit unavailable LDR
  is rejected. No numbered Windows build was allocated for this checkpoint.

### Windows renderer validation correction

- Native audit35551107369 forbd6df54 stopped during test compilation: Windows headers
  define legacy `near`/`far` macros, which collided with queue fixture variable names.
  Rename those variables; keep the actual ordering assertions unchanged. The audit
  did not run its rendering tests. Core/SDK push validation forbd6df54 passed.
- Supersede the queued8ea0067 source audit containing the same collision. This is
  a test portability correction, not a dependency or renderer contract change.

### Authored cameras and shared engine meshes

- Compose Game from authored camera poses, projection, viewport/aspect, order, clear
  settings and layers; report missing cameras without substituting editor navigation.
- Use HDR frame composition with camera-rectangle clear draws, one final display
  resolve, authored exposure and the shared native sky/mesh resource host.
- Add Camera, Light and Mesh Renderer creation recipes. Project animation/navigation
  debug overlays through the Game camera that owns each rectangle.
- Allocate immutable engine asset UUIDs for all20 existing nonempty primitive kinds
  and default/two-sided/legacy materials. Resolve typed engine references without
  project source files; reject project attempts to replace reserved identities.
- Route legacy Primitive/Tint presentation through the same CPU resource pools,
  complete draw candidates, GPU residency and mesh draw pipeline as imported meshes.
  Preserve authored components and default-cube/None semantics; explicit MeshRenderer
  always takes precedence. Keep compatibility shading and transient tint intent.
- Add camera pixel fixtures for composition, clear flags, aspect, no-camera behavior,
  exposure, retained targets and mirrored/collapsed legacy geometry. Add CPU checks
  for the complete primitive catalog, resource sharing, type rejection and unchanged
  project/scene metadata. Native execution and full regression results are pending.
- Prior camera/recipe local checks passed2/2 normally and2/2 with strict sanitizers
  (3.60s). No numbered Windows package has been created for these source checkpoints.
- Expose the immutable engine asset reference helpers in the exact-version SDK,
  including their primitive-name dependency, and include both headers in its existing
  compatibility fingerprint. Installed-client compilation checks the public include.
  ABI1 and persistent identity formats are unchanged.
- Shared asset pickers include readable engine selections and suppress filesystem
  Reveal for these virtual assets. Model/lighting/Play manual pages describe the
  actual camera and built-in mesh workflows.
- Local normal regressions pass3/3 in30.83s; all16 generated HLSL stages, native C++
  syntax (including Windows near/far macro emulation), and full editor UI syntax pass.
  Strict sanitizer and native Windows results remain pending for this checkpoint.
- Strict ASan/UBSan/LSan regressions pass3/3 in78.05s with leak detection enabled.
  Keep Scene frame/fit actions from operating on runtime snapshots while Game is
  also visible. Manual3/3 and formatting checks pass; native pixel tests remain pending.

### Native resource rebinding correction

- Windows source audit558e5ba passed36/37 tests. The viewport test reached native
  rendering and failed its first environment-lighting pixel assertion; captured
  output was black. Later sky and display assertions were not reached.
- Correct FORGE's environment texture bindings from Diligent assign-once mutable
  variables to dynamic variables. The pinned D3D12 implementation deliberately
  ignores reassignment of an already-bound mutable resource. Dynamic bindings also
  preserve in-flight descriptor lifetime during replacement.
- Apply the same correction to the HDR display input and add a real source-identity
  switch/resize/return regression. Uniform buffers that retain the same identity
  remain mutable. No unsafe overwrite flags or vendor patches are introduced.
- Exact-source review and GPU capture identify this as a FORGE integration error.
  Corrected native execution remains pending; the prior unnumbered camera audit
  will be superseded because it contains the same binding defect.
- Corrected binding code and the resize regression pass the full local native C++
  syntax harness. Formatting and whitespace checks pass. These checks do not replace
  the queued D3D12 pixel and SDK execution checks.


### Shadows — implementation and pending native acceptance

- Connect a shared shadow pass to Scene and authored Game cameras using pinned
  Diligent cascade allocation/fitting and PCF. Add spot maps and six point faces,
  layer filtering, casting/receiving flags and structured per-light diagnostics.
- Reuse mesh vertex fetch/full-affine/winding for depth draws. Match alpha-mask
  base alpha/cutoff; blended materials do not cast opaque depth shadows.
- Add camera-relative receiver matrices, off-camera directional caster depth
  coverage, absolute texel snapping and zero-near orthographic fitting. Correct
  perspective shadow Z/W in the FORGE adapter without modifying pinned upstream.
- Add Scene lighting shadow quality controls with shared authoring validation,
  Scene Undo/Redo and preserved unknown metadata. Document the bounded resource
  profile, internal cascade overlap/blending and far-distance fade.
- Normal authoring/bounds checks pass2/2. Supplementary DXC compiles28 HLSL stages,
  including masked shadow programs. New native pixel fixtures and strict sanitizer
  execution remain pending; no new numbered Windows package is allocated.
- Strict ASan/UBSan/LSan authoring/bounds checks pass2/2 in3.43s with leak detection
  enabled. The final28-stage HLSL check, native C++ syntax, editor UI syntax,
  manual3/3 and repository formatting checks pass. Windows shadow pixel execution
  remains pending; these local checks are not desktop acceptance.


### Validation follow-ups for camera and engine assets

- Native source audit163ba06 passed36/37 tests in65.11s. The IBL, sky and HDR
  replacement fixtures passed; camera composition reached its first captured frame,
  then stopped with a presentation-envelope diagnostic. Preserve stage names and
  the failing source document in subsequent camera fixtures, and construct entity
  arrays explicitly. The later failure is not yet claimed resolved.
- Distinguish a non-array presentation envelope from the10000-entity resource limit
  in diagnostics. The previous combined message obscured the actual cause.
- Link in-tree SDK probes to their identity support target so the engine-assets
  header receives its transitive JSON include path. All four affected local SDK
  probe modules now build. Installed SDK and Windows revalidation remain required.
- Update the clock/presentation regression for the intentional legacy default-cube
  adapter: the root remains a blockout mesh, and the explicitly assigned child mesh
  is verified by persistent entity identity rather than vector position.
- Local SDK-profile validation now has passing execution for all66 tests, including
  installed SDK packaging (17.70s). The initial full run passed64/66 in84.92s; two
  loopback tests were blocked by sandbox socket creation and passed on a permitted
  rerun. The clock regression passes locally, and the camera fixture syntax passes.


### Transmission and volume rendering integration

- Connect per-camera cropped HDR background capture, native mip generation and the
  opaque-before-transmission queue to both Scene and Game. Reuse the native light
  sampler and release old background bindings across invisible/parity/LOD draws.
- Add transmission/thickness texture channels, rough background filtering, Snell
  displacement, Beer absorption and RGB dispersion with native reflected lighting,
  sheen and clearcoat composition. Keep transmission separate from alpha coverage.
- Preserve signed affine thickness and zero-thickness rank-two surfaces. Diagnose
  derived optical values that cannot fit the GPU representation without editing
  LocalScale. Use parallel view directions for orthographic material lighting.
- Add CPU pass-selection tests and native fixtures for capture, mip refresh,
  optical factors, textures, reflection and collapse. Record unresolved volume
  topology/inside-interface acceptance and bounded screen-space behavior honestly.
- All Linux/Windows core and SDK push jobs for cc5afb6 pass. Local material tests
  pass; generated HLSL and native C++ checks are separate from the pending Windows
  rendering audit. No numbered package has been allocated for this checkpoint.

- Windows source audit cc5afb6 stopped during viewport-test compilation because
  the shadow fixture relied on a transitive `<numbers>` include. Add the explicit
  standard header. No camera or shadow GPU execution occurred in that run.

- Final local optical checks: material regression1/1 normal and1/1 strict
  ASan/UBSan/LSan pass;28 generated HLSL stages compile; native-header C++ syntax,
  combined17-texture fixture syntax, manual3/3 and formatting pass. Native optical
  pixel execution remains pending.

### Morph-target rendering integration

- Apply prepared mesh default morph weights to position, normal, tangent, color and
  material-selected UV channels before object transforms. Preserve signed weights
  and tangent handedness; clamp vertex color after delta accumulation.
- Bind all256 admitted targets through checked raw offsets and copied weight buffers.
  Reuse the deformation in color and shadow vertex programs.
- Derive conservative default-pose bounds with signed delta intervals, float
  accumulation error and outward rounding. Whole-object and part culling now use
  the morphed bounds; unrepresentable derived positions reject GPU adoption.
- Add native visual fixtures for positive/negative motion, color, UV19, normal and
  tangent changes, plus complete-vector validation. Update upload expectations to
  distinguish immutable source bounds from the default deformed draw bounds.
- Local bounds tests and supplementary HLSL checks for1/256 targets pass; native
  rendering, animated weight extraction and skeletal integration remain pending.

- Final local morph checks: mesh/bounds2/2 normal and2/2 strict
  ASan/UBSan/LSan pass, native C++/fixture syntax passes,36 generated HLSL stages
  compile including1/256-target color/shadow variants, and manual3/3 plus formatting
  pass. Windows morph pixel execution remains pending.

### Windows fixture portability follow-up

- Renamed a transmission resize-fixture local that collided with the Windows RPC `small` macro. The optical source audit stopped at compilation; no optical or shadow GPU pass is claimed from that run.

### Imported animation channel ownership

- Model animation companions now preserve exactly which translation, rotation and scale channels the source clip animates. Converter-only rest channels do not acquire animation ownership; equal-value source tracks retain their intent.
- The admission boundary rejects missing, duplicate, foreign-node and unsupported channel declarations. Legacy companions remain readable without fabricating channel intent. CPU clip resources and detached presentation carry the validated metadata.
- This prepares the animated model bridge; it does not yet claim live model-node application or rendered skinning. The existing source-hashed recipe prevents new cooks from reusing the previous metadata contract.
- Added private skin-pose palette preparation and conservative bounds based on selected joint-world/inverse-bind products. Tests cover palette reordering, unused joints, negative and zero scales, large coordinates, invalid counts/indices and nonfinite matrices. GPU skin submission remains pending.
- Validation: normal converter, worker model pipeline and skin bounds pass. Strict ASan/UBSan/LSan converter and bounds pass (2 tests, 7.70 seconds); the direct model recipe/resource regression passes separately (72.16 seconds). Full source formatting passes. These checks do not constitute Windows skin-render acceptance.

### Focused Windows shader-profile validation

- Added a small dispatch-only Windows SDK compiler check for the 19-sampler material/lighting profile. It compares scalar declarations, explicit register spaces and a bounded sampler array, with and without the native unbounded-table compile option, and retains compiler identity and diagnostics. It does not build or publish an editor ZIP.
- Native source audit 84e713 built successfully and passed 36 of 37 tests. Its viewport test reached the fully layered material fixture and failed FXC compilation at the 16-sampler register limit. The failure occurs in the earlier 15-texture reflection fixture after lighting and shadow samplers are added. Morph, transmission, camera and shadow fixtures later in the test were not reached; the captured files confirm that boundary. This is a pending renderer correction, not a reason to merge different sampler settings.
- The focused compiler check is callable through the existing Build and test workflow using `hlsl_profile=true`; this allows validation from the development branch without publishing a new workflow on the default branch.

### Prepared GPU skin deformation

- Added a copied world-space skin-palette input to the private mesh draw, with camera-relative translation and bounded four-influence linear blending after morph evaluation. The mesh node's own transform is ignored, including a singular matrix.
- Coherent cofactor normals/tangents use the blended linear basis. Triangular skins explicitly handle reflected orientation, including blends whose sign differs from every individual joint, and surviving rank-two surfaces. Color and shadow use the same deformation source.
- Local native-header syntax, 48 DXC shader-stage compiles and strict CPU pose/bounds checks pass. New Windows visual regressions are pending execution; scene/runtime skin binding is still being integrated. No complete skin-render workflow or new numbered ZIP is claimed yet.
- The focused FXC probe completed: 19 scalar sampler declarations failed with both default and unbounded-table flags, including the separate-space variant; the bounded array passed with either flag. An additional mixed array/scalar case now checks the material table alongside lighting bindings before full Diligent/WARP validation.
- The mixed 17-element material array plus two scalar lighting bindings passes FXC with default flags. The renderer now binds a bounded deduplicated material sampler array through Diligent's checked reflected extent and `SetArray`, preserving distinct settings. Material regressions pass normally and under ASan/UBSan/LSan; 48 generated shader stages compile with local DXC. Full native resource binding and pixel validation remain the next check.
- Native audit 345d221 stopped while compiling the new skin fixture because `Viewport` was ambiguous with FORGE's viewport type in the complete Windows test translation unit. The fixture now explicitly names `Diligent::Viewport`. No GPU execution is claimed for that failed audit.

### Runtime model-node animation

- Cooked skeleton resources retain durable model-node AssetIds in native joint order. Runtime animation binds through each ordinary model root, including native Flecs Parent storage and inherited ModelSource values; nested instances remain separate.
- The fixed pipeline samples and applies animation after Gameplay and before Navigation/pre-physics synchronization. Only explicit source channels write LocalTranslation, LocalRotation or LocalScale. Translation-only clips preserve inherited rotation/scale and subsequent prefab changes.
- Prepared values and spatial transforms are checked before a player's channel writes. Ambiguous node provenance rejects the update; detached nodes are diagnosed without retargeting. Recovery validates candidate sample times/scopes before replacing playback state, and valid recovery clears prior binding errors.
- Normal model/animation regressions pass; strict ASan/UBSan/LSan model and animation checks pass, including a final repaired-binding recovery run (74.59 seconds). Manual checks pass. Rendered model skin/morph binding and public animated-model placement remain ongoing work in this same Phase7 package.
- Added camera-dependent conservative skin bounds for palette conversion/blend rounding, with 2,000 float-arithmetic cases spanning reflected/collapsed matrices and large world coordinates. Normal and strict bounds tests pass. Scene-renderer consumption and actual GPU culling validation remain pending.

### Windows audit iteration

- Source1359ebc passes all four Windows/Linux core and SDK jobs plus formatting. Its native renderer audit builds and passes36/37 tests, but fails readback during the first morph checks; later optics, camera, skin and shadow cases are not claimed as executed. The earlier sampler-limit fixture completes. Readback errors now include the texture name and native device-removal status, and morph checks identify their individual stage. Capture writes are checked for failure.
- Restored build caches previously still recompiled unchanged project inputs because Git checkout gave them new timestamps. Windows audit/package workflows now record content hashes and input times after a build, restoring times only for identical tracked regular files. Changed or new inputs are made newer than the cached completed build, including changed bytes with preserved timestamps. Compatibility keys and all required builds/tests remain in place. A real Ninja regression verifies reuse and invalidation; measured hosted-run improvement awaits a cache containing the new manifest.


### Model instance presentation and deformation

- Immutable mesh resources retain source-node and skin bindings, inverse binds and default morph weights. Runtime snapshots expose bounded durable-node morph weights and an explicit readiness marker.
- Added model-root-scoped pose preparation and shared color/shadow submission. Required joints use actual extracted WorldTransforms; compatible instances keep separate morph weights and poses. An unskinned node can reuse a mesh containing joint attributes without accidentally enabling skinning.
- Invalid bindings, incompatible revisions and invalid morph candidates retain the previous complete pose/resources. Temporary binding failures remain retryable; GPU allocation failures do not trigger shader rebuilds every frame.
- Prepared morph intervals eliminate vertex rescans when weights change. Deformed bounds drive culling, LOD and transparent sorting, including per-camera skin rounding bounds in shadow views.
- Normal CPU bounds, model pipeline and authoring regression tests pass (3/3, 33.88 seconds). Native-header syntax passes. Strict ASan/UBSan/LSan checks also pass (3/3, 80.08 seconds). A follow-up malformed-readiness regression and Windows validation remain pending.
- Windows source audit abc5956 built successfully and passed 36/37 tests. The first zero-weight morph draw fails with device removal 0x887a0005; no morph/skin/optics acceptance is claimed. Added separate native morph, skin, frame and optics cases alongside the full viewport test to isolate subsequent failures.
