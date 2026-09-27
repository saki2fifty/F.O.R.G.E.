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
