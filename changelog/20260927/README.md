# September 27, 2026

## Runtime shutdown diagnostics

Play now collects the worker's final stderr buffer after joining it. Runtime
crash or malformed-response diagnostics remain available even when shutdown
occurs before the next editor polling pass. Existing bounded log limits remain.
A regression verifies Stop without polling retains the diagnostic and fails
against the previous implementation. Focused native workflow audits now include
process isolation and native reload checks alongside graphical workflows.
