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
