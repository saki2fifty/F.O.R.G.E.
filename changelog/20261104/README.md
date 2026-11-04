# 2026-11-04

Runtime UI adds bounded drop-shadow filters through existing typed native filter,
scratch-pool, main/camera-output, source-candidate, export and MCP owners. Original
Gallery162 demonstrates hard/soft/offset/stacked/model/animated shadows. Additive
runtime filter profile2 preserves profile1 basic-filter support and rejects new
shadow-capable content on older kits. No new effect database, dependency pin or
native SDK ABI is introduced.

## BC6 HDR texture image output

Published HdrColor BC6 unsigned/signed textures can export decoded texels as
Radiance HDR through the existing isolated image worker and atomic file task.
The independently authored standard-based decoder supports all fourteen modes
and thirty-two partitions; typed half-float transport reuses existing HDR checks,
RGBE writing and owned inspection. Native chooser/provenance and scoped MCP reads
share the same output; negative/nonfinite values preserve a good destination.
Original Gallery patterns retain their encoded blocks and demonstrate a negative
signed mip. No dependency upgrade, source import/SDK boundary change or general
BC6 encoder is introduced. Windows/full-editor/physical acceptance remains pending.

## Declared writes shared by native workers

The existing native query observation now identifies bounded fixed-source write
declarations in configured multi-threaded systems with partitioned This-source
rows and multiple stages. Profiler Worker writes and scoped Play/MCP reads share
the same references, counts and unresolved/partial coverage. Native ownership and
execution remain unchanged; these are declarations, not observed races or writes.
Original tests confirm actual shared field identity, private row partitioning and
no-This dispatch at one and four stages without performing concurrent shared
writes. Windows/full-editor/physical acceptance remains pending.

## Native system dependency inspection

The Profiler's Ordering links view inspects bounded native DependsOn targets,
phase tags, owned-pair presence and referenced nodes through the existing
post-tick runtime observation. Plan labels its legacy first target accurately.
Automation/MCP reads share the same copy, bounds and lifecycle. Native scheduling
remains authoritative; the view does not resolve phases or prove race freedom.
The manual documents current controls and limits. Windows/full-editor/physical
acceptance remains pending.

## Declared native system access

The existing fixed-pipeline observation now copies bounded native query access,
source and operator metadata. Profiler Plan, Declared access and Access overlaps
views share the same Play/MCP data. Potential read/write and write/write overlaps
retain observed native order, with explicit unresolved and truncated coverage;
they are not race verdicts or a replacement scheduler. ParallelMotion now selects
its translation source with the correct zero-based term index, requiring owned
storage. Its canonical source is checked against inherited and private entities
at one and four stages. Windows/full-editor/physical acceptance remains pending.

## Native ECS worker trace tracks

Optional native dispatch capture now records runtime-owned worker stages through
bounded detached buffers and publishes through the existing owner/epoch store.
The Profiler separates stage summaries and Timeline tracks, with stage-aware trace
export and existing MCP reads. Default-off, custom-clock/external-OS fallback,
Clear/Stop, bounds, concurrent worlds and lifetime handling preserve execution
ownership. Native accumulated per-system timing remains disabled in parallel.
The illustrated manual explains observed stages, retention and current limits;
Windows/full-editor/physical/external-client acceptance remains pending.

## Opt-in fixed ECS workers

Project Settings now saves 1..16 native fixed ECS stages, defaulting to one and
including the calling owner thread. Explicitly parallel, declared-access C++
systems use native worker stages and merge boundaries; services retain their
ownership. Ordinary/SDK runtime hosts, world replacement, private recovery,
standalone settings and kit admission use the shared execution value. Existing
project automation/MCP edits the same settings draft. The installed SDK includes
the original ParallelMotion component/system and an editable Gallery161 stage.

Parallel per-system CPU totals are explicitly unavailable and native trace
capture remains owner-thread-only. Whole-tick scopes retain synchronization
cost. The manual documents controls, callback constraints, setup/export and these
limits with an authentic Project Settings capture. Local source checks are
separate from pending Windows, full-editor and physical-hardware acceptance.

## Local fog volumes

Fog Volume adds native box/ellipsoid geometry fog with linear HDR medium color,
world-metre density, ordinary transforms and deterministic layer/priority selection.
Shared surface and indirect helpers retain alpha and existing rendering owners.
Native creation, Inspector/live tuning, Keep values, guides/framing and existing
automation/MCP use the same component and history APIs. Surface profile16 and
saved-document/runtime-kit Fog profile1 protect new content.

Gallery160 supplies both shapes, camera/light and depth markers without project
C++. The illustrated manual teaches the controls and current limits. Local
source/software checks remain separate from pending Windows/cooked Shader,
full-editor, physical GPU and exported-game execution acceptance.

## Input Message trigger modes

Input Message adds Pressed, Released and Changed controls. Changed accepts fixed
digital/Axis1/Axis2 actions and sends once when the conditioned value changes,
including return to neutral. Older missing-trigger documents keep Pressed behavior.
Native value snapshots, lifecycle/bounds and shared Scene/Prefab/history remain
the dispatch and persistence owners. SDK receivers can read the same fixed action;
matching module fingerprints and runtime-kit profiles protect new content.

Gallery159 demonstrates release and analog callbacks with original shared C++
examples, installed SDK targets, scoped MCP Play and an illustrated user guide.
Local checks are separate from pending Windows/full-editor/physical acceptance.


## Imported diffuse lightmaps

Mesh Material graphs expose optional linear Lightmap irradiance, with zero
default, HDR Texture usage and ordinary Material/UV/gain bindings. The shared
renderer adds its material-dependent diffuse response to color and indirect
lighting, independently of Scene environment gain. No editor baker or UV unwrap
is introduced. Surface capability15, checked publication facts and runtime-kit
admission protect cooked exports and retain compatible older artifacts.

The manual and an original HDR Gallery comparison describe authoring, numeric
and platform limits. Windows package preparation cooks the Gallery Shader and
Material with the matching workers; packaging rejects unprepared sources.
Local CPU, source-level native GPU, graph controls/history and MCP checks remain
distinct from pending Windows cooked Shader and graphical game/export acceptance.

## Signed block Texture PNG16

Published BC4/BC5 SNORM selections export through the existing Texture image
worker/task as mapped PNG16. Stored channels map -1..1 to0..65535 with fixed
half-up reference quantization; missing channels stay zero and alpha stays
opaque. Native controls label the mapping and offer PNG only. Automation/MCP
uses the same producer and reports decoded_snorm_rgba16; owned inspection uses
the existing8-bit quantizer. Ambiguous ordered endpoints -127/-128 and BC6 reject.

The bounded worker gains a distinct typed profile and checked little-endian
transport, preserving existing UNORM limits and process/time policy. Two original
Data Gallery patterns, illustrated instructions and focused numeric/worker/UI/MCP
checks accompany the feature. No dependency upgrade, SDK ABI or runtime format
change; Windows/physical acceptance remains queued.

## Static Mesh wind

Mesh Renderer owns optional bounded world-height wind, with disabled defaults,
reflected Inspector controls and shared live tuning/Keep Values. Existing pose,
instance, shading, shadow, motion, bounds and picking paths consume the same
geometry sample. Time comes from the fixed runtime clock or explicit capture
sample, with a frozen Edit preview. Authored transforms and physics stay fixed;
active skin/morph combinations reject before replacing complete geometry.

Automation/MCP authors the same complete property and copies it through ordinary
scatter. Both protocol framings cover normal and SDK Play, including scope,
invalid-value, Step and reviewed-value publication. Runtime documents declare
their required wind profile; older kits reject opted-in content. The installed
SDK value-header fingerprint includes MeshWind, without changing the C host ABI.

Gallery157 compares64 moving plants with24 still plants using existing assets.
The illustrated guide covers settings, tuning and export. Local numeric, native
UI, software-Vulkan geometry/shadow/frame and source-hidden relocated content
checks have distinct coverage. Windows/MSVC/D3D12/full-editor/physical hardware
acceptance remains pending.

## Foliage collision clearance

Optional world-space sphere clearance filters the original foliage candidate pool
against authored native 3D colliders, composing with published terrain heights,
slope masks and surface-frame alignment. Radius, world offset, layers, sensors and
canonical object exclusions are shared by the editor and read-only MCP
start/read/cancel workflow. Bounded preparation reuses private runtime resources,
data-only copied schemas and native physics without running project C++ or
advancing simulation. Source/revision and published selection guards reject stale
results; ordinary mesh.scatter remains the publication/Undo owner.

Gallery 156 adds 59 saved plants around a visible static collider from 96 candidates,
using existing blade/terrain assets. The illustrated manual describes setup,
progress/cancellation, limits and save/export. Native host boolean overlap keeps
the existing all-hit query and SDK ABI unchanged. Local resource/state/custom-data,
actual controls, MCP and software-rendered source-hidden export checks cover the
workflow. Full editor/Windows/physical-device acceptance remains pending.

## Heightfield foliage slope filters and alignment

Foliage placement and read-only terrain preparation accept an inclusive world
geometric slope range. The shared float-triangle query and spatial normal owner
respect the rendered surface and nonuniform terrain scale. An explicit mask keeps
a subset of the original seeded candidate pool; native status/MCP report retained
and candidate counts. The ordinary Scene command creates only retained children
with one Undo/Redo entry. Optional surface-frame alignment uses the same prepared
normal, with seeded yaw/template rotation relative to that frame and signed scale
preserved. Direction normalization handles subnormal values; invalid excluded
normals reject atomically. Existing default unfiltered/upright behavior remains. No foliage runtime component, terrain source schema or SDK ABI
changes; Windows/physical review remains queued.

## Lossless WebP sequence output

Timeline **Export frames…** can add a repeating `animation.webp` while retaining
numbered PNGs and the completion receipt. Native controls and automation/MCP share
the existing sampling, new-folder, restoration and cancellation owners. Detached
PNG snapshots enter the existing supervised worker; the already approved exact
libwebp mux creates bounded lossless full-canvas RGBA8 frames with cumulative
integer-millisecond timing. Encoding failure/cancellation preserves PNGs and an
explicit receipt. No dependency upgrade, authored schema or SDK/ABI change.
Windows/full-editor/physical-player review remains queued; no numbered delivery.

## Owned HDR inspection previews

HDR still output now provides bounded PNG inspection through the existing owned
image task and automation/MCP image operation. Both immediate and later inspection
read the actual quantized written RGBE. A fixed unit-exposure per-channel c/(1+c)
curve, sRGB transfer and nearest8-bit samples are explicitly labeled
preview_profile hdr_reinhard_srgb; full HDR stays linear RGBE. Native Texture /
Scene display processing and exposure are separate.

An independently authored bounded reader admits only the existing FORGE writer's
shared fixed-primary header and raw/literal profile. Invalid profile, extent,
packets, normalization, trailing data and source budget fail without changing the
completed HDR artifact. Typed float inspection reuses the same writer/reader.
Existing output/permissions/revision/replay owners and PNG8/16/WebP/JPEG/compressed
behavior remain. The manual includes actual unmodified MCP inspection pixels;
Windows/physical/external-client review remains queued. No new dependency or
numbered delivery accompanies this local source change.

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
