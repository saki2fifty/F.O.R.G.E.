# 2026-11-04

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
