# September 28, 2026

## Phase 9 acceptance hardening — delivery pending

- The native editor multi-selection/Delete/Undo workflow passes all 399 steps.
- Final package acceptance exposed an SDK fixture readiness gap before menu
  rebinding. The fixture now waits for acknowledged input routing before
  Options/Rebind/key capture; failures retain routing and epoch diagnostics.
- Acceptance assertions and runtime input-reset semantics remain unchanged.
  A new numbered Windows gate is required; Build 105 remains delivered.

## Validated Windows delivery

- Build **260928-000109** passed all nine required CI jobs at source
  `9db10421e0b998ce45b8362e1d2c4b43660ef670`; three optional audit jobs were
  deliberately skipped. Final assembled editor/SDK acceptance passed, including
  rebinding, Save/Load and persisted binding use after process restart.
- Windows multi-selection input checks passed; Inspector, helper-aware framing,
  duplication and restored-scene captures were visually reviewed on D3D12 WARP.
- The numbered package was hash-verified and locally promoted. Physical GPU
  acceptance and interactive batch-transform feel remain user acceptance items.
  This completes the first Phase 9 bundle, not all proposed Phase 9 work.
