# 2026-11-04

## JPEG still-image export

Scene/Game image capture and published opaque sRGB byte textures can save JPEG
(.jpg/.jpeg) through the existing shared task. Native quality controls and the
automation/MCP jpeg_quality field accept1..100/default90; even100 is lossy. Actual
transparency, non-sRGB stored channels, invalid quality and output limits reject
before atomic publication. The private approved pinned callback API retains its
original third-party ownership; no codec/version change or vendor patch is made.

The native chooser/filter mapping has one ordered owner. Quality/cancel controls
precede pixel loading/readback and appended-name replacement. Agent previews
decode the saved JPEG rather than substituting the original pixels. Shared output
grants, operation/replay/revision, overwrite and project lifetime rules remain.
Focused core, native and MCP checks and the illustrated manual cover the workflow;
Windows/full-editor/physical-device acceptance remains pending. No numbered package
or remote publication accompanies this local source change.

## HDR texture image export

Published opaque HdrColor RGBA16Float/RGBA32Float textures can export one selected
mip/layer/cube-face/depth slice as Radiance HDR. The Texture preview's Export image
command chooses the HDR picker for a supported float variant. Existing selected
artifact admission, owned CPU pixels, one shared image task and atomic file writer
remain authoritative. The independently authored RGBE writer preserves linear
high range through shared-exponent quantization, declares working primaries/D65,
and rejects unsupported alpha, nonfinite/negative/range or budget values before
replacing a good file. Source metadata and calibrated units are not preserved.

The existing capture.texture/MCP tool accepts the same HDR profile and explicit
hdr semantic, reporting the written file path and registered MIME. HDR inline image
previews are explicitly unsupported; PNG/WebP inspection remains available.
Rendered Scene/Game display images cannot be relabelled as HDR. Existing byte
output, project scope, replacement and pending cancellation behavior is retained.
The illustrated guide describes format, precision, storage and workflow limits.
Windows/full-editor/physical-device acceptance remains pending; no numbered package
or remote publication accompanies this local source change.
