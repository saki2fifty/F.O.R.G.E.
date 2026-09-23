# 2026-09-23

## Phase7 hardening

- Correct idle CPU resource eviction after a failed replacement: released last-good
  bytes can be reclaimed under the explicit memory budget policy. Live leases and
  pending requests remain protected; failure diagnostics survive eviction without
  claiming the old resource is still loaded. Add regression for budget recovery and retry.
- Extend the native editor input workflow through external glTF file drop, import
  review/publication, Content search, Model inspection/reimport/placement, scene
  Undo/Redo/Save and corrupt-source rejection and valid external source reimport, with captures at 100/150/200% scale.
  Geometry observation and input-driving code are test-only.
- Reconcile historical architecture decisions and current resource, camera, renderer
  and user-manual wording. Add the existing in-editor cache controls to the resource guide.

## Validation and delivery

The new resource regression reproduced the original failure; strict ASan/UBSan/LSan
passes after correction. Editor interaction tests pass locally with strict sanitizers;
manual generation/link checks pass. Expanded native workflow and final matrix are pending.
Build 260922-000065 remains the current immutable delivery until the consolidated
correction passes its release gate. Physical acceptance remains PARTIAL / PENDING.
No Phase 8 work or dependency pin changes.

## Asset diagnostics

- Automatic reimport failures now retain the affected AssetId and source path in
  Problems. Selecting the entry navigates to the asset; supported text sources use
  the existing contained source viewer. Successful publication clears the same error.
- The native corrupt-source workflow checks that this context exists and that
  selecting the diagnostic inspects the asset without changing scene state.

- Correct transform-guide contradictions about negative scale, flattened mesh
  picking and delivered prefab tools. Apply to Prefab stays deferred.
- The first expanded Windows input run exposed a test-observation error at startup:
  absent AssetIds must be represented as null rather than serialized as valid UUIDs.
  Corrected the fixture; that run supplies no successful UI acceptance.

## Renderer verification

- Add native readback/captures for the existing inside-facing volume exit path:
  absorption, two-sided-flag independence, mirrored parity and total internal
  reflection above the critical angle. These exercise the current screen-space
  transmission approximation; they do not add nested-volume ray tracing.

## Connected authoring verification

- Extend actual input through creating a reusable Material, editing roughness,
  document Undo/Redo/Save, and assigning it through the typed scene picker.
  Assert that material history/publication leaves scene state untouched, while
  the scene assignment has its own Undo/Redo and Save. Capture the authored
  Material workspace at 100/150/200% scale.

- The expanded Windows run passed the isolated optical checks, including volume
  exit absorption, mirrored parity and total internal reflection. Opened the
  resulting captures. Its input extension exposed missing inactive-tab observation;
  the fixture now observes dock tabs before testing their content visibility and
  selects Content before the file drop.
- The combined renderer test reached its old 60-second CTest limit after completing
  the new optical assertions. Earlier successful runs took 56–57 seconds. Give this
  cumulative device-lifetime test 120 seconds; retain all assertions, separate case
  limits and production GPU-wait diagnostics. This does not change runtime waits.
- Replace the manual's open inside-volume test note with the verified exit-interface
  behavior and its explicit screen-space/nested-volume limits; update the render matrix.
