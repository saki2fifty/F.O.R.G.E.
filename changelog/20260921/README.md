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
