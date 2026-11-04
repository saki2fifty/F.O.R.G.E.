# 2026-11-04

## Compressed Texture still-image export

Published UNORM BC1/2/3/4/5/7 textures and their current sRGB forms can export
decoded reference texels through the existing native and automation/MCP image
task. One checked mip/layer/face/depth plane is decoded by the packaged asset
worker, then saved as PNG, sRGB lossless WebP or opaque sRGB JPEG. Status names
decoded_rgba8 and the native completion identifies decoded block texels. These
values are distinct from original pre-compression pixels and compressed payload.

The approved pinned native decoder stays worker-only. BC1 punch-through, all
BC2/3 endpoint orders, explicit alpha, missing channels and cropped block edges
have defined behavior. Signed BC4/5 and BC6 reject. Selected source128 MiB, decoded
RGBA8 64 MiB and4096-axis limits preserve existing output/overwrite guarantees.
The original Gallery BlockColors.dds is published through the normal pipeline;
source-hidden native/MCP checks use its immutable cooked data. Windows/full
editor/physical-device review remains queued; no numbered delivery is created.

## Normalized16-bit texture image export

Published R16/RG16/RGBA16 textures can save precision-preserving PNG16 through the
existing Texture selection/slice, typed image task, shared libpng writer and
atomic file transaction. Actual samples, alpha and hidden RGB remain exact;
missingG/B0 andA65535, with no invented sRGB label or gamma/normal conversion.
The profile admits256MiB selected source,128MiB RGBA16,4096axis and136MiB encoded
PNG while existing byte/HDR/JPEG/WebP output bounds remain.

The native preview hints PNG-only and describes16-bit completion. Automation/MCP
reports actual component_bits/profile; current and delayed PNG inspection uses one
nearest8-bit quantizer and existing preview sizing. Its preview is distinct from
the exact full file. The original published Gallery precision ramp uses the normal
isolated import/publication owner; previous scenes, settings and asset identities
remain. Native/MCP workflow, source-hidden precision checks, known wire-endian
vectors, strict CPU boundaries and the illustrated manual cover the behavior.
No vendor code/version/texture schema changes or numbered delivery is introduced;
Windows/full-editor/physical-device acceptance remains queued.

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
