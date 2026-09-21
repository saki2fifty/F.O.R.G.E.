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

### Animation and physics ownership

- Added a host-only validation boundary before model animation writes any local TRS channel. It reuses the existing Jolt-backed configuration, spatial ancestry and Dynamic ownership checks; no implicit teleport or collider rewrite is introduced.
- The same candidate validation runs during animation recovery before playback state is committed. Invalid animated collider scales and Dynamic movement retain previous transforms and solver bodies; repaired bindings can resume.
- Normal model pipeline, physics and animation regressions pass (3/3, 34.51 seconds), including Kinematic/Dynamic cases before/after body realization, ownership repair, failed recovery and a nonuniform sphere-scale clip. Strict ASan/UBSan/LSan validation also passes (3/3, 86.59 seconds); the additional successful full scene/physics/animation reconstruction case also passes normal and strict validation (final strict model recipe plus bounds: 2/2, 75.95 seconds).
- The preceding model-rendering checkpoint's final malformed-readiness follow-up passes normal and strict authoring regression checks (0.59 and 3.81 seconds). Windows audit for source 1c0ae36 is running independently.

- Model bone overlays now use actual resolved scene-node WorldTransforms from the rendering snapshot, with root-scoped identity lookup. They preserve inherited/spatial behavior, omit ambiguous/missing/unready joints and keep the standalone Ozz preview path.
- Bone preparation runs once before multi-camera projection, preserving double world coordinates until projection. Normal CPU overlay regression and native header/editor syntax checks pass; strict overlay checks also pass; Windows acceptance of this follow-up remains pending.


### Pose memory admission and isolated graphics follow-up

- Add a separate 512 MiB per-scene derived CPU pose payload profile, checked before
  allocation. Count retained plus candidate data and temporary joint scratch;
  share immutable morph intervals with the adopted GPU bundle. Release deleted
  instance payload before retrying refused candidates. Preserve last-good draws.
- Add CPU exact-budget/retention regressions and a Windows aggregate scene budget,
  deletion and retry fixture. No authored scale or persistent-format change.
- Source `1c0ae36` passed 37/41 Windows checks; the isolated camera/frame test passed.
  Morph device removal, FXC skin geometry compilation and transmission crop test
  failures were isolated. Explicit constant-index geometry emission addresses the
  compiler path; the equivalent predicated morph loop awaits controlled native
  execution. Do not interpret supplementary DXC compilation as Windows acceptance.
- Correct the crop test's assumption about native PBR Neutral highlight
  desaturation. Use .5 primary radiance and checked sRGB values instead. Fix display
  resolve dimensions for a selected mip and require a one-pixel last-mip result.
- Validation and the complete Phase7 work package remain in progress. No new
  numbered package is reserved or delivered by this follow-up.


### Model first-pose presentation boundary

- Distinguish prepared animation resources from model channels successfully
  applied at a fixed tick. Paused resource adoption does not advertise unapplied
  model transforms as a ready mesh pose and never writes ECS transforms.
- Snap the first/repaired model transform sample and its morph time together,
  using the existing presentation cache. Later compatible ticks interpolate.
- Reconstructed playback requires its next successful fixed application before
  publishing a model draw; transient interpolation history is not serialized.
- Add regressions for paused adoption, immutable scene state, readiness markers,
  and coherent first-pose samples. Local and native validation are in progress.

- A disabled Animator no longer marks its model meshes unavailable. Current node
  transforms remain unchanged and source-node morph defaults apply; Pause is the
  operation that holds a complete animated pose. Re-enable/recovery and subsequent
  interpolation receive explicit regressions. Reset/recovery clear stale snap IDs.

- Final first-pose validation: normal model/animation/physics tests passed 3/3 in
  39.85s; strict ASan/UBSan/LSan passed 3/3 in 87.31s after moving allocating
  interpolation bookkeeping before ECS writes. Manual checks passed 3/3; format
  passed. Windows execution of this follow-up is still pending.


### Animated model placement admission

- Connect the internal model placement command to optional typed clip selection,
  same-revision skeleton compatibility, required skin joints, and an ordinary
  root Animator. Omitted selection does not invent a default clip.
- Revalidate active clip/skeleton members and dependency identity before the scene
  transaction. Preserve one-step undo/redo, duplicate identities and prefab
  inheritance; reject stale, removed, wrongly typed or foreign candidates.
- Normal model-pipeline regression passed 1/1 in 36.96s; strict ASan/UBSan/LSan
  model-recipe regression passed 1/1 in 83.33s. Public placement controls and final
  GPU acceptance remain required ongoing Phase7 work.


### Native renderer validation follow-up

- Windows source 0d722a9 built and passed 37/41 selected tests. Isolated Frame
  rendering passed, including bounded pose admission and capacity recovery.
- The skin geometry shader now passes native compilation, positive/reflected
  coverage and matching shadow depth. Correct its surviving rank-two winding
  sign to match the actual front-face convention, with a flipped-camera fixture.
- Camera-crop/mipmap acceptance passes. Keep Beer attenuation endpoint evidence
  below the tonemapper's deliberate highlight desaturation; retain raw captures.
- Morph rendering still reports WARP device removal at its first zero-weight
  draw. Add an identical-source DXC/FXC diagnostic comparison using the pinned
  Diligent compiler API; production selection stays FXC pending actual evidence.
- These follow-ups need native rerun. No numbered package or complete renderer
  acceptance is claimed.

- Local follow-up checks: all 48 generated HLSL stages compiled with the
  supplementary Linux DXC tool; backend and native-fixture header syntax checks
  passed. Manual checks passed 3/3. These do not substitute for Windows execution.


### Structural visibility and selection policy

- Add independent optional reflected Node Visibility and Node Selectability
  components with ordinary persistence, prefab intent/Revert and scene history.
- Resolve effective policy through structural ancestry, including non-rendering
  intermediaries; World/Explicit spatial bindings do not bypass it. Diagnose
  invalid producer ancestry with bounded iterative traversal.
- Keep camera behavior and selectability independent of visibility. Hidden meshes
  remain available to selection consumers but are excluded from color/shadow
  queues; hidden lights are excluded. Existing blockout picking honors selection.
- Preserve imported false flags during model placement. Correct the shared
  component catalog so Camera, Light and Mesh Renderer are available to the
  generic Inspector/Add Component workflow. Validation is in progress.

- Focused model placement, authoring and core checks pass 3/3 (35.54 seconds).
  Add legacy viewport visibility and selection-policy regression coverage.
- Native source audit now passes 38/41 checks: all isolated skinning cases pass,
  including mirrored/flattened poses and reflected cameras. The identical morph
  fixtures pass with DXC but still lose the device with FXC. Keep production FXC
  and test explicit vector-lane selection without changing the shader profile.
- Transmission checks pass through the complete material-layer fixture. A later
  shadow check still fails; retain per-light captures and projection/depth
  diagnostics for investigation. This is unfinished native acceptance, not a
  numbered release.
- Strict address/undefined/leak sanitizer checks pass 3/3 (91.36 seconds), manual
  checks pass 3/3, formatting passes, and all 48 supplementary generated HLSL
  stages compile. Native fixture/backend syntax checks pass; Windows rerun pending.

### Precise viewport selection

- Select actual retained mesh geometry at the chosen LOD, including morphs and
  skinning. Preserve negative/zero-scale behavior without requiring an inverse.
- Clip triangles, lines and points before perspective division. Use independent
  node selectability, nearest-depth selection and stable identity tie breaking.
- Share one camera construction path between Scene drawing and selection. Missing
  geometry does not create a substitute cube hit; failed/over-budget queries keep
  the existing selection and show an actionable status.
- CPU regressions cover reflection, zero scale, large world origins, reflected
  cameras, negative morphs, skinning, near-plane clipping, points/lines and work
  exhaustion. Normal and strict sanitizer render-bounds checks pass; the native
  retained-mesh selection check still requires Windows execution.
- Final focused selection rerun passes normally (0.05 seconds) and with strict
  sanitizers (0.22 seconds). Native backend, editor-main and fixture-header syntax
  checks pass; manual checks pass 3/3 and formatting passes. Windows execution
  remains a separate pending gate.

### Native shadow orientation correction

- The Windows audit confirms that explicit vector-lane selection did not fix the
  FXC morph failure; DXC continues to pass the same fixtures. Extend the diagnostic
  comparison to FXC with optimization disabled and retain generated source and
  disassembly. Production compiler/optimization defaults remain unchanged.
- Identify the directional shadow integration defect from pinned Diligent source:
  its selected light basis reverses Y, but FORGE omitted the camera orientation
  flag used by mesh culling. Derive that flag from the basis and projection; add
  cascade/punctual parity checks. Native pixel confirmation is pending.

### Model import and placement document

- Content now opens a Model import document with reflected import settings,
  worker progress/cancellation, source scene and optional animation choices,
  source-hierarchy inspection and a Place model operation.
- Reuse one import-editor controller for texture/model Save focus, dirty-state
  guards, cancellation and publication. Keep asset publication separate from
  scene history; placement creates/selects ordinary entities in one Undo step.
- Present ambiguous subasset correspondence explicitly. Bind chosen mappings to
  the reviewed build-input key; changed input requires a fresh review before
  publication, and failed candidates preserve the prior family.
- Validate model metadata asynchronously with project/generation checks. Release
  cooked blobs after validation and retain only the metadata needed for placement.
- Real-process texture and model ImGui regressions pass 2/2 in 1.80 seconds,
  covering guarded close, Save focus, identity ambiguity/stale decisions,
  signed/zero-scale placement, Undo/Redo and corrupt-source failure retention.
- Include the model editor regression in both native audit and packaging CI target
  lists and test filters. Update the models/Content manual; native rerun pending.
- Refresh Content immediately after texture/model publication. Clear pending
  identity decisions when the project changes. Remove the ineffective morph
  vector-lane experiment after the native comparison disproved it as a fix.

- Bind immutable raw mesh/morph buffers through Diligent's existing bounded
  descriptor-table option. Native FXC comparison remains pending; production
  shader compiler/profile are unchanged.
- The instrumented editor-controller harness passes 2/2 in 3.47 seconds with
  matching normal importer adapters/native workers and sanitized publication,
  core and placement libraries. Initial mixed-profile rejection was a harness
  configuration error; production fingerprint and worker memory checks remain.
  This is not a claim of sanitizer coverage inside the child process.
- Manual checks pass 3/3; formatting and platform-neutral presentation syntax pass.

### Live model animation publication

- Notify separate Play after a Model document publication; read the runtime's own
  catalog asynchronously and coalesce repeated notifications.
- Prepare the new skeleton/clip through existing typed resource pools while keeping
  last-good playback. Adopt at a fixed tick only after sampling and transform/physics
  validation; preserve playback time and snap the first compatible replacement pose.
- Preserve previous resources on missing/corrupt/stale/incompatible candidates, with
  structured diagnostics and retry on a later publication. Retain standalone legacy
  animation restart semantics and the existing recovery envelope.
- Allow asset import settings during Play while preserving the model-placement
  scene-edit guard. Add runtime IPC, paused refresh, failure, generation and locked
  placement regressions; validation is in progress.

### Built-in mesh texture support

- Add texture coordinates and analytic tangent frames to all twenty engine meshes:
  spherical, cylindrical, toroidal and planar mappings with split seams/poles.
- Preserve engine AssetIds, geometry, legacy preview vertex layout and scene
  Primitive/Tint compatibility. Increment only the immutable mesh recipe revision.
- Test every built-in shape through mesh encode/decode, tangent orthogonality,
  noncollapsed UV triangles and UV/winding handedness; validation is in progress.

### Responsive Content discovery

- Move registered-asset and saved-scene discovery off the UI thread with one owned
  cancellable scan. Coalesce repeated requests, retain the prior list on failure,
  and reject completed scans after a project switch. Show Refreshing status.
- Keep newly created prefab records immediately available from the existing owned
  prefab catalog; background discovery never creates IDs or writes an asset index.
- Add actual controller regressions for background adoption, repeated refresh,
  corrupt catalog retention and old-project completion; validation is in progress.

- Validation: live runtime model IPC 1/1 (1.99 s), normal model pipeline 1/1
  (34.54 s), strict model/animation 2/2 (90.94 s), and updated built-in surface
  direct-model sanitizer regression 1/1 (97.12 s) pass.
- Content/model controller regressions pass normally (1.72 s) and with the
  instrumented host (1.89 s). Correct capsule pole handedness using its limiting
  surface orientation; roundoff in a near-zero radial dot product is not a valid
  orientation test. Keep failed empty Content scans on the refresh interval.

### Native renderer follow-up

- Windows source audit f72a763 passes 39/42 tests, including the model editor,
  retained mesh selection and skinned surfaces. Optimized FXC morph rendering
  still fails; the identical DXC and unoptimized FXC comparisons pass.
- Rewrite morph delta loading with an initialized result and a single return,
  retaining all bounds and compiler settings; native confirmation is pending.
- Confirm directional shadow depth now renders. Increase only the small-caster
  acceptance fixture resolution so its interior exceeds the native PCF footprint;
  preserve the strict occlusion assertion. Full native shadow validation pending.

### Repeated geometry rendering

- Share complete native draw bundles by exact resource generations, material slots,
  texture semantics and skin policy; weak entries release removed content.
- Use Diligent per-instance inputs and bounded indexed batches for compatible
  opaque/masked static parts. Keep independent entity identities, signed/zero
  transforms, surface bases and legacy tint; split incompatible winding/layers.
- Retain individual transparent, transmissive, skinned and morphing draws and
  preserve shadow-caster submission. Add native pixel-equivalence, batching and
  release regressions plus immutable-key checks; validation pending.

- Previous combined source 384d07c now passes all 42 native Windows audit tests.
  Optimized FXC morph rendering passes after the initialized single-return
  correction, with the production compiler/profile unchanged. Directional, spot
  and point shadows pass strict occlusion, cutout and mirrored-caster checks.
  The new instancing changes still require their own native run.

- Bound native bundle fan-out before allocation to 4096 distinct LOD parts per
  scene; count retained revisions, share repeated entities, and retry refused
  candidates after capacity is released. This counts native draw resources rather
  than claiming a driver VRAM measurement. Add low-capacity native regression.
- If one instance rejects batch validation, retry individually so valid neighbors
  still render with entity-specific diagnostics.

- Local model/render regressions pass 2/2 normally (34.13 s) and with ASan, UBSan
  and leak checks (98.22 s). Generated shader checks pass all 48 stages. Native
  instancing/low-capacity tests remain pending; no numbered package reserved.

### glTF instance import

- Admit `EXT_mesh_gpu_instancing` through the existing bounded native accessor
  path. Derive ordinary mesh child nodes while retaining captured source, shared
  parent transforms, stable member correspondence and independent entity identity.
- Validate count/type/normalization and preserve signed/zero scale. Normalize
  quantized integer quaternions within their quantization error before float TRS
  admission. Expand morph weight targets with shared samplers.
- Preserve custom attributes in captured source with an explicit diagnostic. Reject
  instanced skin bindings until their transform semantics are represented correctly.
- Add malformed-accessor, quantized-rotation and full import/placement/cache tests;
  validation and instanced-skin applicability review are in progress.

- Native source eaeabeb passes all 42 Windows audit tests, including static instance
  pixel equivalence, independent colors, 65-entity batching, native part-budget
  rejection/retry and removed-bundle release. glTF import additions are subsequent.
- Official Khronos PR2404 confirms skinned-instancing composition was left
  ambiguous; it is not forbidden by glTF. Retain an explicit FORGE profile
  rejection rather than silently applying an unagreed post-skin convention.

- Instance import validation passes: normal native/model tests 2/2 (38.59 s),
  normalized rotation/morph routing 1/1 (0.05 s), and ASan/UBSan/leak checks 2/2
  (108.31 s). An additional normal real-worker failure regression passes (35.80 s):
  mismatched instance counts retain the prior model revision, bindings and scene.

### Standalone material authoring foundation

- Add sparse versioned Material source documents with explicit equal-value intent,
  base inheritance, reset-to-default, texture clear and Revert semantics. Preserve
  unknown document fields and reuse existing typed material admission/defaults.
- Resolve against immutable published base materials through the existing dependency
  graph and asset publication service; add root Material resource loading through
  the renderer's shared resource path. Source Save remains separate from publication.
- Material source tests pass locally. Full publication/resource, sanitizer and
  Windows validation for this increment are pending; the central editor is in progress.

### Material and shader authoring integration

- Add a central Material source document with independent bounded Undo/Redo,
  external-file conflict checks, explicit source AssetId, guarded close and Save.
  Source Save and cooked publication remain separate; errors preserve the previous
  published asset and do not masquerade as scene Undo transactions.
- Add base-material inheritance through the existing typed dependency graph,
  cycle rejection, sparse equal-value overrides, per-field Revert and explicit
  reset-to-default/texture-clear semantics. Runtime resolves immutable cooked values.
- Add grouped surface/PBR-extension controls, per-slot texture drag/drop, UV and
  sampler editing, source diagnostics, responsive property/preview columns and
  source-document routing for project materials versus imported model members.
- Add an isolated unsaved material preview using the shared frame renderer with
  engine sphere/cube/plane meshes, orbit/zoom, light/environment/background/exposure
  controls and last-good complete-resource adoption. Normal scene resource hosts
  refuse unsaved preview selections; native pixel tests cover that boundary.
- Add asynchronous Inspector material-slot discovery and assignment through the
  existing reflected property operation. Keep unresolved slots visible, preserve
  stable keys and expose restoration of mesh defaults with scene Undo/Redo.
- Validate actual cooked texture dimensions and requested semantic variants before
  publishing materials. Incompatible or missing variants retain good publications.
- Add the Shader import document using the existing isolated compiler, declared
  source identity and last-good publisher. It does not claim arbitrary Shader
  assets can replace built-in mesh material shader contracts.
- Route pending source close/cancellation through document adapters instead of
  growing subsystem-specific switch chains. Preserve each document's Save/history
  ownership and existing scene guards.
- Correct older animation/navigation editor test callers to their current copied
  debug-data APIs. Include the editor process test and all new test targets in
  explicit Windows audit build/test lists; copy required UI test runtime DLLs.
- Add Materials/Shader import manual pages and update Content, texture and technical
  ownership documentation. Offline manual validation and formatting pass locally.
- Current local validation: material/resource/import tests 3/3 pass (6.48 s);
  latest ASan/UBSan/LeakSanitizer material/import tests 3/3 pass (9.43 s).
  Native material preview and Shader UI tests await this source's Windows audit.
  The previous source 4507282 passed all 42 Windows tests and all core profiles.
  No new numbered package has been produced; the complete Phase7 work remains active.
- Final local admission follow-up: validate draft numeric/vector/sampler structure
  through the existing typed value decoder before UI entry, while still allowing
  unfinished model values to fail publication safely. Keep edit failures local to
  their controls so rejection cannot unwind an open ImGui tree.
- Updated local results: material/import 3/3 normal (5.53 s), strict sanitizers
  3/3 (6.23 s), Material UI/assignment 1/1 (1.37 s). Add actual Windows editor
  captures for Material at 100% and 200% UI scale, in addition to native pixel tests.

### Content sources and automatic reimport

- Add asynchronous project-contained source polling, coalesced rescans, incomplete
  scan recovery and exact self-write suppression across overlapping scans. Root
  traversal does not weaken asset locator containment or create identities.
- Show recognized unregistered sources in Content with a Source files filter,
  source Inspector and supported central import/source-document dispatch.
- Automatically reimport registered Texture/Model/Material/Shader assets through
  their existing sealed providers and publication service. Order build dependencies,
  defer dirty/pending documents, cancel superseded candidates and retain last-good
  assets on missing sources, invalid content or failed import.
- Expose Source updates status, queued/active work and manual retry. Refresh clean
  documents, dependent material selections and scene resources after publication.
- Record exact publication write receipts and sidecar digests. Unchanged startup
  scans do not republish assets; changed source, sidecar, importer/profile and
  captured dependency revisions are reconciled. Legacy metadata refreshes once.
- Correct the MSVC C++20 string/JSON comparison that stopped source e2c0775 Windows
  compilation. Both Windows core profiles and the native audit encountered that
  same error; native material validation has not yet run on the corrected source.
- Local validation: source/import/publication/material tests 4/4 pass (18.66 s);
  the added offline dependency-startup regression also passes (9.41 s). Final
  ASan/UBSan/LeakSanitizer tests 4/4 pass (18.89 s); Material/Model/Texture editor
  tests 3/3 pass (4.40 s), editor process/scaling 1/1 pass (11.51 s). Manual tests,
  formatting and adapted native main syntax pass. Add discovery to Windows audit
  target/filter lists. Windows validation of this source remains pending.
- Whole Phase7 remains in progress. No numbered package, file-management completion
  or production-scale watcher performance claim is made by this checkpoint.

### Asset file-operation foundation — in progress

- Reuse the publication writer's durable filesystem IO for a private source/sidecar/
  catalog transaction. Add exact revision checks, staged hash-verified backups,
  precommit rollback, postcommit retention, cancellation and external-conflict refusal.
- Recover interrupted file operations before importing; reject conflicting recovery
  authorities. Retain delete backups and report cleanup separately from a committed
  operation. This does not participate in scene Undo.
- Add typed Move/Duplicate/Delete preparation, fresh owner/member identity families,
  catalog dependency remapping and source-owned Scene/Prefab/Material/Shader adapters.
  Do not clone a selected cooked binding for a new logical asset. Add a scene duplicate
  overload that remaps known references to a caller-reserved new AssetId.
- Initial normal transaction/publication/material tests pass 3/3 (23.16 s), strict
  ASan/UBSan/LeakSanitizer 3/3 (24.98 s). Domain operation/identity tests pass 2/2
  normally (3.41 s) and under strict sanitizers (3.88 s).
- Add actual crash-boundary, stale deletion, corrupt-backup and Windows deletion-lock
  regressions. The latter and newer glTF relocation/shared-admission work await their
  next validation; current source is not delivered as a numbered build.
- Content command/job integration, complete reference-impact scans, remaining source
  adapters and final package validation are still in progress. No new menu availability
  is claimed by this internal implementation checkpoint.
- Share existing glTF URI decoding/container admission with the relocation adapter.
  Rebase known external buffers/images; preserve data URIs, opaque JSON, BIN and
  unknown GLB chunks. Normal glTF/fileops/recovery 3/3 pass (14.19 s); strict
  glTF/fileops/recovery/publication 4/4 pass (20.20 s). Full source-format/UI work
  remains ongoing; later adapter additions require their own validation.
- Expanded source adapter tests now publish and load a duplicated Material under
  its new identity, and validate Shader/Prefab source copies. These pass normally
  (1/1, 6.85 s) and under ASan/UBSan/LeakSanitizer (1/1, 7.70 s).
- Source-watch checkpoint5827579 passes all Linux/Windows core and exact-SDK CI
  profiles. Its native editor audit is still running; file-operation UI integration
  and Windows execution of this later source remain pending.


### Content source-file review and authoring guards — ongoing Phase7

- Wire Rename / Move, Duplicate source and Delete source into Content's owner-asset
  context menu, with asynchronous preparation, reference review, explicit delete
  acknowledgement, cancellation, retained backups and separate commit/refresh errors.
- Inspect known Meta AssetRef/EntityRef fields, nested collections, prefab instances,
  Material references and project startup identity across saved sources and the current
  scene draft. Report opaque coverage honestly; refuse a stale reference review.
- Index discovered scene/prefab UUIDs without allocating replacements. Pause and drain
  automatic imports while the file dialog owns sources. Block competing Save, recovery
  autosave, project-switch and authoring writes. Preserve scene identity/draft/history
  when adopting an identity-preserving file move. Prune removed queued import records.
- Initial service/watch tests pass 2/2 (18.15 s); refined reference/index/delete tests
  pass 1/1 (10.45 s). Current changes still require strict, editor and Windows checks.
- Source checkpoint5827579's native Windows audit has now passed 47/47 (179.74 s),
  including material preview/render/editor capture and discovery. This evidence does
  not cover the newer file-operation source changes.

### Material preview polish

- Fit preview geometry using the actual primitive vertices and the camera's limiting
  field of view. Preserve deliberate manual zoom and provide Frame view to restore fit.
- Remove unused disabled surface-state Revert rows. Keep explicit/inherited ownership,
  actionable Revert and per-field context actions.
- Clear incidental runner mouse input after the SDL backend update for static asset
  screenshots, using the exact pinned ImGui/Diligent APIs. New native captures pending.
- The source-level Material editor test passes (1/1, 1.08 s). No new numbered package.

### Combined Content and Material validation

- Combined source-operation/recovery/publication/watch tests pass normally 4/4
  (32.77 s) and under strict ASan/UBSan/LeakSanitizer 4/4 (37.54 s).
- Final opaque JSON-kind handling passes normal/strict file-operation checks
  (8.31 s / 8.41 s). Dirty-scene move now saves recovery under the new locator before
  removing the old snapshot. Updated editor-process/Content-dialog tests pass 2/2
  (10.99 s), including 100%/200% layout, guarded Delete and Cancel.
- Manual3/3, formatting and runtime/presentation target boundaries pass. Extend both
  native audit and final packaging test lists for file transactions, file operations
  and Content dialogs. Add actual native Content-review capture stages; Windows
  execution of this bundle remains pending. No build number has been reserved.

- Final source review also fixed removal invalidation: retain the old dependency
  graph's affected consumers when an asset disappears from the new catalog. Missing
  base materials now produce an admission diagnostic without replacing a dependent's
  last published selection; removed queued assets are pruned. Normal regression
  passes (9.68 s), as does the strict sanitizer follow-up (8.95 s).


## Content navigation and selection (continuing Phase 7)

- Added a disposable background Content index with imported member display names, folder ancestry, cached search text and source-derived states. Browsing still allocates no persistent identities.
- Added folder tree/menu, breadcrumbs, bounded back/forward history, list/grid views, adjustable tile size, type/state filters and combined case-insensitive search words. Real preview thumbnails remain pending; current tiles label their fallback honestly.
- Used pinned Dear ImGui native multi-selection and clipping. Full AssetIds survive refresh/filtering; source-only rows retain transient path keys. Added clipboard actions, saved personal view settings, and catalog dependency/reference inspection.
- Routed selected reimport through the existing application service with whole-request validation and shared-owner deduplication. Existing dirty-draft, dependency, worker, publication and last-good rules remain in force.
- Local browser/editor regressions pass (2/2, 11.93 s), including the default 200-pixel bottom-panel density requirement. Selected-import/activity tests pass normally (10.21 s) and under strict ASan/UBSan/LSan (11.94 s). Manual and formatting checks pass. Windows validation of this browser bundle remains pending; no numbered release.

- Windows core/SDK validation of the preceding source-file bundle exposed overly long temporary filenames: a valid 239-character stored blob became a 284-character staging path. Atomic storage now uses a UUID-named sibling without repeating the destination basename, retaining exclusive creation, file flush and atomic replacement. Added replacement regression near the traditional Windows path bound. This fixes staging-name growth; it does not claim unrestricted long-path support across every dependency.

- Staging fix: file-operation/recovery tests pass normally (2/2 within the 23.99 s selected run) and strictly instrumented (2/2, 17.88 s). Freshly relinked shared publication regression passes normally (4.58 s) and with sanitizers (6.01 s). The Windows correction still requires a new native/core run.


### Content discovery and capture follow-up

- Separated optional scene discovery failure from catalog adoption; retain the last complete discovered-scene list while current registered assets remain available. Recognize uppercase JSON, exclude known non-scene sources/catalog/sidecars, and align traversal with existing source-scan entry/depth bounds.
- Source lookup and publisher self-write acknowledgements now use the established Windows/POSIX locator comparison policy. Added Windows case-equivalent lookup/acknowledgement coverage and a physical project with over10,000 unrelated sources.
- Reimport selected rejects a selection containing unimported sources without silently submitting a subset.
- Static UI captures clear the ImGui navigation cursor as well as the mouse pointer, following pinned source hover behavior. Production keyboard navigation and help remain unchanged.
- Follow-up validation passes locally: editor/browser 2/2 (12.47 s), asset discovery/file operations/material pipeline 3/3 (30.12 s), and strict ASan/UBSan/LSan 3/3 (31.32 s). Windows follow-up remains pending; no final-package claim.
- The preceding Windows audit passed 50/51 tests and all core/SDK profiles passed. The remaining browser failure exposed raw slash versus backslash comparison in Windows locators, creating a duplicate source row beside its registered asset. Ordinal case comparison now uses generic separators; a Windows regression checks both spellings. The near-limit atomic staging regressions now pass on Windows.
