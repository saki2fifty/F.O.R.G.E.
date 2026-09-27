# September 27, 2026

## Runtime shutdown diagnostics

Play now collects the worker's final stderr buffer after joining it. Runtime
crash or malformed-response diagnostics remain available even when shutdown
occurs before the next editor polling pass. Existing bounded log limits remain.
A regression verifies Stop without polling retains the diagnostic and fails
against the previous implementation. Focused native workflow audits now include
process isolation and native reload checks alongside graphical workflows.

## SDK Play response progress

The SDK runtime now services pending response bytes between fixed ticks, matching
the legacy runtime. Slow catch-up batches no longer restrict a prepared large
response to one pipe quota per batch. No gameplay or clock ownership changes;
commands still execute outside fixed ticks and request deadlines remain unchanged.

Native SDK acceptance evidence now identifies the caller of cursor-release
observations and the active focus-test substep. Diagnostics are fixture-only and
bounded; focus and stale-acknowledgement protections are unchanged.

## SDK Play cursor handoff

Cursor-release retries now wait for the reply confirming the submitted release
observation. Replies already in flight no longer trigger a duplicate release that
cancels a fresh capture. The runtime waits for outstanding physical release before
offering a new capture. Regression tests cover both ordering boundaries and
confirmation reset on restart. Windows acceptance remains required.

SDK acceptance reports now include the staged renderer/UI readiness, activation
wait state, loading state and confirmed input epoch when a workflow ends.

Failed SDK scene admission also preserves the frozen candidate and bounded
renderer/resource diagnostics for reproducing a preparation stall.

## Reference game asynchronous capture

Starting or resuming Editor Play no longer immediately queues Pause while the
editor is still acknowledging cursor capture. The sample retains the requested
capture intent until its receipt completes. Rejection and actual capture loss
still pause the game. A delayed-ack regression reproduces the old failure and
passes with the fix; rejection handling is also covered.

Escape now takes precedence when capture failure/loss occurs in the same control
frame, preventing an automatic Pause followed immediately by an unintended Resume.
The SDK focus fixture uses its existing per-action bounds independently of the
initial scene-loading timer; overall and transport deadlines are unchanged.

The SDK focus checks now use the existing 20-second action budget: the old
four-second bound was shorter than the five-second wire deadline and included
multi-second WARP screenshot work. The overall 240-second cap, transport deadline
and functional assertions remain unchanged.

## Play import and preparation boundaries

- Play waits for the initial source scan and queued asset publications, preventing known startup import work from racing scene preparation.
- Completed mesh-pose validation failures no longer masquerade as pending resource loads; preparation reports the failure and preserves the previous active scene.
- Added initial-scan/queued-import readiness checks and a renderer regression for completed pose-budget rejection.

- Corrected the SDK acceptance fixture’s duplicate Resume activation and wait for runtime acknowledgement before testing interaction; failure evidence includes simulation/input counters.

- Closed the initial-scan debounce gap: Play also waits for already-observed source changes to reach the import queue. A regression reproduces premature readiness before this fix.

- Runtime reply draining now gives the polling reader a bounded opportunity to free pipe space before slow simulation resumes. Both runtime hosts use the same helper; request deadlines and domain ownership are unchanged.
- Added a real-pipe regression with a 512 KiB reply, a 4 KiB pipe and 100 ms work gaps; the old implementation times out, the correction passes.

- The SDK acceptance fixture now waits for acknowledged gameplay capture after both same-process and restarted Continue before testing persisted Jump bindings.

- Added backend-neutral frame pacing to the shared presentation owner: editor and standalone keep at most two submitted frames outstanding, preventing runaway GPU backlog after swapchain wait timeouts.
- A stalled completion wait produces a bounded rendering error; VSync remains independently controlled. Native fence admission/retirement checks cover the shared implementation.

## Phase 9 selection and batch authoring — source work, delivery pending

- Extend the existing selection owner with ordered entity selection, a primary
  entity, additive clicks and visible Hierarchy ranges. Asset selection clears it.
- Common-component Inspector shows mixed properties and uses atomic batch writes.
- Move/rotate/scale gestures operate on effective spatial roots, preserving
  independent transform channel ownership and one-step Undo. Selection changes cancel.
- Duplicate/delete normalize structural subtrees; selection framing uses the union
  of retained geometry bounds. Mark all selected origins; keep one primary gizmo.
- Add focused rejection/history/prefab regressions and UI-driven capture steps.
- Correct outdated project startup instructions and the earlier ADR claim that
  entity multi-selection already existed. No schema, dependency or ABI change.
- Build 105 remains the delivered build. Windows execution and visual acceptance
  of these source changes have not yet occurred.
