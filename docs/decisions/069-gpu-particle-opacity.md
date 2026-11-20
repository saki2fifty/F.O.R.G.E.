# ADR069 — GPU particle lifetime opacity

Status: accepted.

## Existing ownership and precision

Extend the existing supplemental view projection to evaluate bounded opacity
endpoints or2–16 Step/Linear stops using the original evaluator's short float age.
ADR060 defines visual GPU float evaluation, without CPU double lockstep. Near Step
transitions and lifetime endpoints, quantization can select a different segment
from CPU double evaluation. Preserve that established contract and original47-row
analytic/authored-program input rather than change its age arithmetic or RNG.
Inactive CPU opacity remains at CPU precision; source activation and float-stop
admission remain explicit in the shared authoring workflow.

The existing seven-row vertex packet already carries per-instance alpha under
flag3 in the first normal row's spare W lane (ADR063). Apply the factor after any
camera-basis projection. Preserve all other geometry/normal/RGB lanes and old flags.
The shared Material shading multiplies this alpha with its ordinary Material,
texture and vertex alpha, through the existing weighted transport and its
complete-view refusal/omission contract (ADR068). No CPU readback/fallback, second
simulation clock/queue, per-birth entity or independent appearance owner is added.

## Private upload extension

Deliberately extend ADR065's private64-byte camera/tint constant buffer to352bytes:
existing count/mode and camera rows, short age/lifetime, endpoints and16stops. Reuse
one supplemental upload, pipeline, binding, same-UAV barrier and emitter instance
buffer. A second independent projection pipeline/buffer is unnecessary. Counts
publish only after every required dispatch succeeds and the camera owner restores
targets on both success and failure. Validate bounded normalized opacity packets
and finite short-age/lifetime inputs before dispatch; failure clears the count.

This struct lives in a private src header, is not an installed gameplay interface,
is never serialized and is rebuilt with its embedded shader. The immutable47-row
graph reflection, seven-row vertex layout, source/cook formats and gameplay C ABI
remain unchanged. Old profiles leave the opacity mode disabled and retain their
interpretation. Source12 adds `gpu_opacity_enabled`; explicit values16–19 require
GPU opacity1 alongside existing weighted/GPU and selected graph/billboard kit
capabilities. Independent CPU/GPU activation shares endpoints and stop identities.
Older sources and immutable cooks retain their meanings; recipe/envelope3 and
installed gameplay ABI remain unchanged. Native and MCP use the same source operation.

## Required proof

Compare actual device rows and weighted pixels with independent reference
appearance, including texture/Material multiplication, zero/full opacity,65+birth
slices, burst/continuous windows, Step boundaries, camera/Mesh projection, repeated
queries and recovery. Test float transition quantization explicitly. Complete the
native/source/MCP/history/publication/kit/Gallery/export/manual workflows before
reporting the game-creation feature complete. Windows graph execution and physical
acceptance remain separate from local software evidence.
