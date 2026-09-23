# 2026-09-23

## Phase7 hardening

- Correct idle CPU resource eviction after a failed replacement: released last-good
  bytes can be reclaimed under the explicit memory budget policy. Live leases and
  pending requests remain protected; failure diagnostics survive eviction without
  claiming the old resource is still loaded. Add regression for budget recovery and retry.
- Extend the native editor input workflow through external glTF file drop, import
  review/publication, Content search, Model inspection/reimport/placement, scene
  Undo/Redo/Save and corrupt-source rejection and valid external source reimport, with captures at100/150/200% scale.
  Geometry observation and input-driving code are test-only.
- Reconcile historical architecture decisions and current resource, camera, renderer
  and user-manual wording. Add the existing in-editor cache controls to the resource guide.

## Validation and delivery

The new resource regression reproduced the original failure; strict ASan/UBSan/LSan
passes after correction. Editor interaction tests pass locally with strict sanitizers;
manual generation/link checks pass. Expanded native workflow and final matrix are pending.
Build260922-000065 remains the current immutable delivery until the consolidated
correction passes its release gate. Physical acceptance remains PARTIAL / PENDING.
No Phase8 work or dependency pin changes.
