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
