# Texture data, import and runtime ownership

FORGE implements bounded texture cooking, isolated publication, typed asynchronous
CPU resources, Diligent GPU realization, Content previews and central import/viewer
documents. The format, encoder and device profiles below are separate contracts;
a represented format does not imply every encoder or GPU supports it. Phase7 final
clean-build and packaged acceptance remain separate validation gates.

## Cooked layout

`TextureData` separates 2D, 2D array, cubemap, cubemap array and 3D dimensions.
Layers count array elements; each cube has six faces in +X,-X,+Y,-Y,+Z,-Z order.
Subresources are ordered array element, face, mip, then depth slices. Image rows
start at the top. Payload rows are tight, without GPU upload padding. Floating
scalars are little endian; non-finite float/half pixels reject.

The version1 `FRGTEX` envelope carries bounded JSON metadata and a contiguous
payload. It shares the checked envelope implementation with cooked meshes.
Lengths, dimensions, mip count, enums, semantic compatibility and the exact
expected subresource bytes are validated before allocation/copy. Complete payload
consumption is required. Default budgets are512MiB payload,16384 width/height,
2048 depth/layers and32768 subresources. Device capabilities still require a
separate realization check. Block-compressed volumes have a backend-neutral byte
layout; upload requires the selected device to support that format/dimension.

Represented formats cover R/RG/RGBA8/16 normalized, half/float channels,
BC1/2/3/4/5/6H/7 including applicable signed and sRGB variants. BC block extents
round up independently at every mip; small tail mips still occupy a full block.
BC6H/7 representation does not imply an implemented HDR/BC7 encoder.

## Semantics and sampler intent

Color, data, normal and HDR color are explicit. sRGB storage is valid for ordinary
color; normal/data/HDR maps reject sRGB interpretation. Normal maps use +Y tangent
space, with an explicit green-channel flip during import. Imported normalized
normal vectors are renormalized at every prepared mip; two-channel normals
reconstruct positive Z. Alpha metadata distinguishes opaque, straight,
premultiplied, unknown and custom data. DDS custom alpha means the fourth channel
is data, not opacity; encoding must not silently replace that intent. Material alpha testing/blending remains material intent.

Sampler state includes independent min/mag/mip filters, wrap U/V/W, anisotropy,
comparison, LOD bias/range and border color. Logical validation requires finite
values, positive integer anisotropy, ordered LOD bounds and linear filters for
anisotropic sampling. It does not impose one graphics API's numeric bounds.
Negative minimum LOD and finite HDR border colors remain representable. At GPU
admission, Diligent adapter properties supply anisotropy and support flags;
the narrow backend adapter supplies missing numeric LOD-bias/border rules.
Unsupported sampling rejects before allocation, preserving authored settings and
the previous usable draw bundle. D3D12 uses its SDK bias range and unit-range
border color; the Linux Vulkan probe queries the physical device bias limit and
enforces the pinned three-color border palette. Unused native border values are
zeroed without editing the asset. Other hosts cannot enable unverified optional
states. See [backend capabilities](render-backends.md).

## Image preparation

The private worker adapter admits PNG, JPEG, TGA, BMP, WebP and HDR/RGBE. It uses exact pinned
Diligent processing for mip generation and BC1/3/4/5 encoding, and the pinned image
codecs where their input boundary is suitable. JPEG uses the private, bounded JPEG entry points in Diligent's already pinned
stb_image2.29 source at46fcb30365c5f35425751d275eecd8e5f8efc786. Its symbols
are private, and no codec pin or source is changed.
PNG uses the existing libpng
subdependency directly through a bounded memory callback; see the source finding
below. No decoder runs in a runtime-resource loader.

- No-mip mode explicitly requests one native level; it cannot expose uninitialized
  native mip storage.
- Mips for sRGB color are filtered in linear light. Normal mips use linear values
  and are normalized before compression.
- Maximum size selects the first prepared half-resolution mip that fits; aspect
  is retained with the standard floor-to-one mip convention. Exact arbitrary
  resampling is not currently offered.
- Native BC encoding accepts8-bit normalized pixels. R/RG map toBC4/BC5;
  decoded RGBA maps toBC3. This path does not quantize16-bit/HDR data silently.
- Higher-precision sources require explicit linear storage in the current adapter;
  a high-precision sRGB conversion workflow is not yet implemented.
- Color gray/gray-alpha expands to RGB/RGBA while preserving actual alpha; data
  channels remain R/RG. HDR/RGBE with no source alpha remains opaque.
- Native alpha premultiplication is performed on decoded pixels before mip
  generation. Alpha metadata is derived from actual prepared pixels.
- Cooperative cancellation is checked between codec operations; actual worker
  process CPU/memory/time containment remains the outer importer responsibility.

KTX/Basis use the separate container adapter below. DDS uses the separate native container path. EXR and optional TIFF/SGI
are not advertised by these adapters.

## Exact-source correction

DiligentTools7d113906 `TextureLoader/src/PNGCodec.c::PngReadCallback` ignores
`PngDataSize` while copying requested bytes. A minimum truncated-span regression
showed the supplied length was not enforced. `Image.cpp::TIFFReadProc` has the same
unchecked-copy pattern; that route is not selected for FORGE texture imports.

The dependency remains unchanged. FORGE's C libpng adapter uses the official
`png_set_read_fn`, dimension/chunk limits, strict CRC errors and `png_read_end`.
It rejects reads beyond the supplied bytes, keeps `longjmp` within C, and releases
pixels/row pointers on errors. libpng still owns PNG parsing/decoding; Diligent
still owns subsequent native image processing. The exact selected libpng is1.6.55
at65bc84e803c0ccbf7aa1023e91b5808586ea1b66. Every truncated copy of the minimal
fixture is a rejection regression. This does not claim unrelated preexisting
Diligent texture-loader callers have already been converted to the new boundary.

## Runtime resources

`texture_resource_loader` reads only an already-contained cooked file, checks its
expected digest, validates data and reports retained CPU memory. The caller owns
path containment. Failed candidates retain the previous resource; strong leases
retain immutable revisions. Semantic/backend variants share one durable AssetId
and remain separate pool selections. See[resource ownership](runtime-resources.md).

### JPEG decoder selection

Strict instrumentation of IJG libjpeg9e found undefined signed shifts in integer
DCT and Huffman encoding. Its floating DCT does not fix the latter. FORGE therefore
uses the JPEG implementation in the already pinned stb source, with private
symbols, no SIMD, header/pixel limits, framing checks and final decoded-layout
validation. The valid owned2x2 JPEG fixture is fixed bytes generated with pinned
stb_image_write in supported C++20, verified under ASan/UBSan/LSan; its generation
is not a runtime JPEG encoder feature. Malformed-input prefix tests also use
separately allocated bounded input copies. No IJG defect exception, vendor patch
or sanitizer suppression was introduced.

TGA packet/palette byte extents are admitted before the native stb pixel decoder:
uncompressed and RLE pixels cannot request more data than the source supplies.
Extension/developer data may follow the image. All current native CPU adapters
require little-endian hosts explicitly; unsupported byte order fails compilation
instead of creating an incorrectly labeled cooked artifact.

## KTX and Basis worker adapter

The private asset-tool adapter reads bounded KTX1/KTX2 containers using official
KTX Software4.4.2. It preserves supplied mip chains, array layers, cube faces and
supported volume slices. Raw formats map to the existing27-format texture
contract, including BC6H passthrough. KTX2 supports native Zstandard inflation
and ETC1S/BasisLZ or UASTC transcoding. There is no BC6H encoder in this selection.

Admission checks the complete header, native-format descriptor, sizes, nonoverlapping
ranges, mip ordering, metadata, dimensions and ETC1S image/codebook extents before
native parsing. File and decoded byte budgets are checked independently. KTX1 GL
format/type fields, padded rows and endian metadata are checked before native
conversion to KTX2. One-dimensional textures and Basis volume transcoding are outside
the current adapter profile. Raw block-compressed volumes retain their native byte
layout, subject to GPU format/dimension admission. Orientation must be right/down/in; nonidentity channel
swizzles and other color primaries/transfer functions require explicit conversion.
They receive diagnostics rather than silently displaying incorrectly.

Basis desktop selection uses BC7 for RGB/RGBA, BC4 for red, and BC5 for packed
red/green. UASTC's unpacked RG layout falls back to linear RG8. The CPU RGBA target
retains red/RG channel meaning, including ETC1S/UASTC packed red-in-RGB, green-in-alpha
layouts. Texture semantics reject sRGB data/normal maps. The glTF admission option
also checks the extension's dimension, mip, orientation, channel, transfer,
primaries and alpha restrictions; it does not itself connect an imported model
to renderer materials.

Encoding uses the official bundled Basis encoder's own KTX2 output for prepared
RGBA8 2D mip chains. It does not call the defective ETC1S `CompressBasisEx` wrapper.
Both ETC1S and UASTC use one thread, no OpenCL/SSE and no UASTC RDO; ETC1S quality128,
compression level2. Prepared normals disable ETC1S endpoint/selector RDO. Native KTX
metadata APIs set unspecified color primaries for data/normals. Premultiplied and custom-alpha input
is rejected by this encoder instead of losing its alpha metadata. Other dimensions,
HDR encoding and arbitrary channel layouts are not advertised as encoder features.

Repeatability applies to an identical source/settings/platform/toolchain profile.
The upstream release explicitly does not promise bit-identical Basis results across
platforms. Record that profile in build inputs. Calls are worker-only, use bounded
source/payload allocations and cooperative cancellation between native operations;
whole-process peak memory/time containment remains the worker supervisor's job.
The container importer connects this adapter to Content, validated publication and
semantic resource loading. GPU realization independently checks device format usage.

KTX2 rows are tightly packed; KTX1 has four-byte row alignment. Do not use the
pinned `ktxTexture_GetRowPitch` as a general KTX2 layout oracle: it applies legacy
padding and floor block counts. FORGE uses its checked format layout, tested with
3×5 R8 and7×5 BC7 fixtures. This is an adapter correction, not a vendor patch.

Zlib-supercompressed KTX2 is explicitly rejected. The pinned optional miniz
path uses unaligned typed access on x86, exposed by strict codec testing. It
has no external override equivalent to the Basis byte-read switch. This path
is not needed by `KHR_texture_basisu`, which permits BasisLZ/UASTC with optional
Zstandard. No native zlib KTX encode/decode capability is advertised.

## DDS container preparation

The private adapter checks legacy and DX10 DDS headers, exact tight payloads,
format masks, dimensions, mip chains, arrays, all six cube faces and volume
slices before calling pinned Diligent's native DDS loader. It requests an owned
aligned source copy before the native reader's typed header access. Supplied
mips remain intact. DX10 transfer and alpha metadata are authoritative; legacy
DDS has no declared sRGB transfer. Common normalized/float/BC formats use the
existing texture representation, including BC6H and BC7 passthrough.

Native pixel utilities convert BGRA/BGRX to RGBA and expand legacy luminance
for color usage; data usage retains R/RG channels. Typeless formats, incomplete
cubes, unsupported packed layouts, padded payloads and trailing bytes reject.
The native profile caps mip count at15, total array faces at2048 and volume
width/height at2048, alongside the shared texture budgets. DDS container imports use the shared import workflow. Successful container
preparation does not prove device format/dimension support.

`TextureAlpha::Custom` is appended as value4; previous values retain their
numbers. The not-yet-released Phase7 cooked format accepts this explicit metadata;
earlier readers reject an unknown enum instead of misreading its channels.

## BMP preparation

BMP reuses the exact existing private stb2.29 instance. Admission precedes native
parsing: dimensions and signed-height bounds, complete padded rows, palette
indices, bitfield masks and color-profile requirements. INFO/V4/V5 support
1/4/8-bit palettes and16/24/32-bit pixels, with applicable uncompressed/bitfield
layouts. CORE support is24-bit only. Top-down and bottom-up storage both produce
top-first pixels. Explicit bitfield alpha is retained, including all-zero alpha;
legacy32-bit BI_RGB follows native stb behavior, treating an entirely zero fourth
channel as unused/opaque.

RLE, embedded JPEG/PNG,56-byte headers, non-paletted header gaps and custom/ICC
color profiles require conversion. Exact-source reasons for native-specific
limits are recorded in[known issues](dependency-known-issues.md#bmp-source-profile).
V4/V5 requires declared sRGB; older headers use the chosen import semantics.

## WebP preparation

Official libwebp1.6.0 owns lossy/lossless decode and RIFF demux. FORGE admits full
RIFF files with bounded chunk extents/counts and complete file length, checks
native features/dimensions before allocating pixels, verifies a single still
frame and decodes into an explicitly bounded RGBA buffer. Native output is
straight alpha; shared image processing handles subsequent semantics/mips.
Animated WebP and ICC conversion are not texture-decoder capabilities: these
inputs reject clearly. EXIF/XMP and other ancillary metadata stay in the original
source; pixel preparation does not apply EXIF orientation or edit metadata.

The codec is private to asset tools, single-threaded, with official runtime SIMD
dispatch. Its native demux CMake target links the full upstream codec library;
the importer and view-image tools use it, and tests use its encoder to generate owned fixtures.
Single Scene/Game view images can be saved as lossless WebP through the existing
image task. The native encoder uses the advanced lossless preset with exact RGB
under zero alpha, single-threaded encoding and a bounded output callback shared
with PNG. It accepts complete top-to-bottom RGBA8 display pixels, dimensions up to
4096 per axis,64MiB input and68MiB output. Row padding is validated; WebP's native
stride must fit an int. Invalid input or encoding failure produces no replacement.
`asset_storage::replace` publishes only a complete image under the existing file
and overwrite rules. Heavy codec work remains in the task's IO job. Once committing,
that job finishes before task retirement; there is no native encoder preemption.
Protocol previews are still PNG, bounded by the existing1024-pixel/8MiB limits.
The owned still-WebP decoder prepares them without an asset import or publication.
This view-output path does not add runtime image decoding or GPU texture formats.
Source/output budgets
and cooperative boundaries complement the isolated import worker's CPU/memory
limits; codec-internal peak allocations are not bounded by the output buffer alone.

HDR/RGBE admission validates bounded header lines, canonical -Y/+X orientation,
positive dimensions, complete flat pixels or all four RLE channels per row.
It rejects truncated packets, changed row widths, mixed flat/RLE rows and old
repeat markers the native decoder does not implement. RGBE conversion stays in
pinned stb/Diligent; metadata exposure is not applied by this pixel adapter.

## Isolated recipes and semantic bundles

The private `forge.texture.image` and `forge.texture.container` importers now
connect discovery, a supervised `forge_asset_build` process, parent-side cooked
validation, immutable cache publication and CPU resource loading. The worker
receives owned input snapshots; it never receives a project writer lease, ECS
world or graphics device. Content and the central Texture import document use these factories; broader batch
file import and browser thumbnails remain tracked separately.

An imported texture publishes one `forge.texture-bundle` version1 index named
`texture.json`, plus canonical `texture-color.ftex`, `texture-data.ftex`,
`texture-normal.ftex` or `texture-hdr.ftex` files. Each entry records its semantic,
byte count and SHA256. All selected variants are validated and published together
under **one Texture AssetId**. Additional usages do not allocate new logical IDs.
Color and data variants can be loaded simultaneously; the runtime pool's variant
key keeps their interpretation and leases independent. A failed replacement
preserves the previous selected revision.

Primary usage defaults to automatic color/HDR detection. Up to three distinct
additional usages can be requested. Raster color filtering uses the selected
transfer; data/normals/HDR always use linear storage. Green-channel flipping affects
normal variants only; alpha premultiplication affects color/HDR variants only.
Container variants preserve declared transfer and supplied mip chains; a request
that contradicts container metadata rejects instead of reinterpreting bytes.
Sampler wrap/filter/anisotropy settings apply to every variant. Container schemas
do not expose raster resizing, mip regeneration or pixel-edit settings.

The recipe revision hashes its source and relevant build configuration, compiler
identity/version, platform, configuration and codec pin/options. Cached output is
repeatable within that exact profile; the same numeric library version alone is
not sufficient evidence of identical output. This private worker protocol is not
a stable extension ABI.

### Staging and bounds

Each attempt uses a fresh `.forge/jobs/<UUID>` directory. Request/output manifests
bind generated portable filenames to exact byte counts and hashes. Admission
rejects duplicate names, missing/extra files, traversal, symlink redirection,
Windows device names, changed bytes and unsupported protocol versions. The output
completion manifest is written only after all output streams close. Existing
staging files are never overwritten. Normal completion, failure and cancellation
remove only that job's created directory.

Current limits:256MiB source/per file,512MiB aggregate output,16 files,1GiB process
memory,120seconds wall time and110seconds CPU time. Texture payload and aggregate
variant preparation reserve space for envelopes/manifests. Diagnostics are bounded;
parent-side format validation remains mandatory even after worker success.
Production process limits are not weakened for sanitizer tests. Direct recipes and
codecs run with ASan/UBSan/LeakSanitizer; ordinary builds separately exercise actual
supervised processes because ASan's shadow reservation exceeds the production
virtual-memory limit on Linux.

## Presentation preview

`TexturePreviewRenderer` is a private presentation adapter over admitted Diligent
textures, independent of ImGui and asset identity. Exact DiligentCore
744f079f61cdbda15d371383682418fc927e4a61 generic validation permits a single
cube/array slice as a 2D SRV, but `TextureD3D12Impl.cpp` rejects a nonzero first
slice on that non-array view. Preview therefore uses a one-slice **2D-array SRV**
and matching array shader for cubes/arrays; ordinary 2D and volume textures use
their matching shaders. Volumes sample the selected depth center. One-mip views prevent implicit selection of another mip. Settings validate
indices, finite exposure (±20 stops), normalized nonempty crop, and nonzero output
dimensions up to 2048 per axis before replacing output.

Color textures use hardware sRGB decoding and the pinned DiligentFX sRGB output
transfer exactly once. Data previews display sampled numeric values. HDR uses the
same pinned PBR Neutral implementation as display resolve. Isolated channels show
sampled linear values directly; alpha is unsigned and not gamma-transformed.
Premultiplied input is unpremultiplied for display, with a defined zero-alpha path.
The optional checkerboard composites in linear space for color/HDR views. Unknown
or custom alpha modes display as straight alpha; they are not a custom blend evaluator.

`TextureAssetPreview` reuses `request_texture`, `ResourcePool<TextureAsset>` and
`GpuResidency<TextureAsset>`; it is not another asset database or resource identity.
Selected DDC receipts and hashes are verified by a worker. GPU realization stays
on the presentation thread. One worker/eight requests/sixteen slots and 256 MiB
CPU/GPU payload budgets bound each preview owner. The underlying decoder still
has its existing independent temporary-admission bounds. Previous good revisions
survive failed replacement, while switching asset/semantic clears that fallback.
Native SRBs release source bindings after draw; accounted leases and fences retain
submitted inputs. Closed viewer resources retire after ImGui submission. Cached
output is reused while selected resource revision, dimensions, crop and controls
are unchanged. Content thumbnail caching reuses these preview/resource owners with bounded
entries and explicit retirement; preview success does not publish authored data.

The standalone texture import document owns its draft/save workflow and shows its
preview before a collapsed import-settings section. Generated model textures use
a read-only central document. Neither view creates a Flecs world or authored data.


## Published texture image output

The native Texture preview and `capture.texture` / `forge_capture_texture` compose
`asset_detail::load_texture_selection` with the checked Texture image projection and the existing
ViewImageTask image IO transaction. Standalone and generated model members use the
same selected-publication admission as runtime resources, including recipe/hash,
semantic and family revision checks. A copied catalog and owned selection values
outlive the UI; no borrowed GPU texture or preview pixels enter the encoder.
There is no editable-source decode, reimport, new cache owner, asset registration or Scene
history entry. Original image bytes may be absent when the cooked selection exists.

The first output profile accepts R8/RG8/RGBA8/RGBA8Srgb and one explicit mip,
array layer, cube face (+X,-X,+Y,-Y,+Z,-Z) and depth plane. Existing TextureData
validation and layout own subresource ordering and tight row strides. The complete
selected texture is bounded to128MiB; the projected top-row-first RGBA8 image is
bounded to4096 per axis and64MiB. Missing G/B are0, missingA255. All present bytes,
including RGB under zero alpha and premultiplied/normal/data channels, remain exact.
There is no color/alpha/normal conversion or source/import metadata roundtrip.
The byte profile rejects block compression,16-bit and floating storage before
publication. The compressed, normalized16-bit PNG and HDR output profiles are described below.

The detached RGBA8 value records an sRGB label without changing bytes. PNG emits
its sRGB chunk only for sRGB storage; otherwise no color-space label is invented.
The lossless WebP output profile requires sRGB storage. Existing rendered Scene /
Game output remains sRGB-labelled. Owned protocol previews retain the label when
re-encoded; downscaled previews are inspection images, not byte-exact full outputs.
Encoder and file-publication limits/overwrite guarantees remain those above.

Texture exports share the one existing image task, native format chooser and file
writer. The button requires a current published preview and idle shared task;
unsupported storage or output sizes report a job error. Draft import settings and
view display/crop controls are excluded. Pending requests/pickers cancel on project
change; Scene changes do not invalidate an accepted texture selection. An active
bounded pixel/encoding job finishes before retirement, with no preemption promise.
The MCP output grant, canonical path checks, explicit replacement, revision/sequence
and exact-current-operation read rules remain authoritative. No second agent-only
export path is maintained. See [the user guide](../manual/editor/textures.md).


## Decoded compressed Texture image output

Published UNORM BC1/2/3/4/5/7 and their existing sRGB variants use the same
selection and image-file transaction. The shared private texture_image_plane
helper validates the complete selected Texture (128 MiB), selector ordering and
4096-axis extent. The parent stages only that immutable tightly packed plane in
the existing run_import_process transport. A fixed forge.texture.image-decode
recipe with profile bc-unorm-rgba8-v1 runs in the packaged asset worker; it does
not import, publish or select assets. Transport version1, OS lease, staging
cleanup, input/output digests, fixed command and supervisor remain unchanged.
Output admission requires exactly one rgba8.bin with width*height*4 bytes.

Only the worker and direct codec tests link the pinned KTX/Basis block APIs.
BC1 uses unpack_bc1 with alpha enabled; its return indicates punch-through,
not validity. BC4/5 use the native unsigned channel primitives with G/B zero
where absent and A255. BC7 uses native validity checking. BC2/3 RGB always has
four palette colors: a local endpoint/selector normalization composes the native
BC1 primitive without its conditional three-color interpretation. BC2 expands
stored alpha nibbles by17; BC3 uses native BC4 for alpha. See the primary
[S3TC format specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt)
and [BC7 format layout](https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html).
No external implementation is copied into the adapter or patched upstream.

Decoded RGBA8 is bounded to64 MiB and top-row-first; partial edge blocks are
cropped to the logical plane. Native calls use aligned block scratch, the pinned
byte-read definitions and required no-strict-aliasing profile. Stop is checked
between block rows. The existing worker bounds are512 MiB address space,30 seconds
wall/25 CPU,64 MiB per file and72 MiB output storage, with250 ms cooperative
cancellation grace. These are independent bounds, not a process peak-memory claim.
The existing image-file job still finishes before retirement once started.

Results are decoded reference texels, not original pre-compression samples or
original compressed bytes. BC1 preserves encoded punch-through despite an opaque
metadata hint. There is no transfer, alpha-convention conversion or normal-Z
reconstruction. PNG retains the format's sRGB label; lossless WebP requires sRGB;
JPEG also requires actual opaque alpha. Signed BC4/5 and both BC6 forms reject.
No universal physical-GPU rounding match is claimed. Native completion identifies
decoded block texels; automation/MCP reports component_bits8 and
texture.profile decoded_rgba8. Current/later owned previews, permissions, revision,
replay, overwrite and last-good file rules use the existing owners.


## Normalized16-bit PNG output

Published R16/RG16/RGBA16 storage projects through the same checked image slice and
selected-artifact loader into an owned Rgba16Pixels value. Cooked little-endian
scalars are explicitly assembled as native uint16 values. Present channels/alpha
and hidden RGB remain exact; missingG/B are0 and missingA65535. No source decode,
color/gamma/normal/alpha conversion or invented sRGB label is performed. The
complete selected normalized texture limit is256MiB, packed RGBA16 is128MiB,
each axis4096 and encoded PNG136MiB. Existing byte/HDR/JPEG/WebP output limits
remain. PNG selected-artifact reading can admit256MiB before the actual typed
projection enforces the byte or16-bit source profile; this is not a128MiB process
memory ceiling. Native codec/read/preview buffers are additional bounded storage.

One shared libpng C writer handles8/16-bit IHDR, bounded callbacks and longjmp
cleanup. The pinned public png_set_swap API converts native little-endian16-bit
rows to PNG network order after png_write_info. Existing PNG8 entry points and
byte budgets remain intact; the16-bit entry point deliberately emits no sRGB chunk.
The C++ exact sample-count/format check precedes the existing atomic file writer.
Wrong suffix, sample layout, selector or budget cannot replace a good artifact.

The Texture preview hints PNG-only for normalized16-bit selections through the
same optional single-format chooser contract as HDR-only output. Byte selections
retain their PNG/WebP/JPEG choices. A completed native status identifies16-bit PNG.
Automation/MCP uses the same producer/transaction, reports component_bits16 and
stored_rgba16 for a completed texture, and preserves output permissions, exact
revision/replay, overwrite and source-lifetime rules. Pending texture metadata uses
stored_samples until the actual producer establishes8/16-bit result precision.

Owned current/later PNG inspection uses one bounded quantizer: each uint16 sample
maps to nearest uint8 via(value*255+32767)/65535, then the existing1024-axis preview
resampler. Alpha/hidden RGB and the absent color label are retained at that reduced
precision. The full PNG stays16-bit; no lossless-preview claim is made. The private
PNG decoder retains native uint16 samples and full framing checks. It may read a
136MiB normalized16-bit artifact and decode128MiB; byte codecs keep their68MiB
encoded admission. The inspection path still reads only owned artifacts, with no
new arbitrary-file or decoder authority. See [the user guide](../manual/editor/textures.md).


## JPEG still-image output

The existing image transaction also supports opaque sRGB byte pixels as JPEG
(.jpg/.jpeg, image/jpeg), quality1..100 with90default. The native chooser's filter
array and returned index mapping share one owner. Its quality modal precedes pixel
loading/readback and appended-suffix replacement; cancellation and stale project
rules retain the existing ownership boundary. Programmatic requests use the same
validated ImageEncodingOptions; jpeg_quality is rejected for other destinations.

The private C++20 adapter calls the approved pinned stb_image_write public callback
API without copied or modified encoder code. It validates packed RGBA8 extent,
quality, actual alpha255 and output budget before publication. Alpha is never
silently discarded and no matte/data/linear reinterpretation is provided. All
selected pixels must be opaque, irrespective of the TextureAlpha hint. Existing
4096-axis/64MiB RGBA/68MiB encoded limits and128MiB selected byte-source admission
apply. Callback allocation/budget failure stays sticky, partial output is freed,
and the atomic writer receives only a completed encoding.

JPEG quantization/chroma sampling can change colors even at quality100. The file
is an sRGB still-image convention without source ICC/EXIF/import metadata. Owned
protocol previews decode the actual completed JPEG using the existing bounded
private decoder, expand gray/RGB to opaque RGBA and return bounded sRGB PNG. No
new arbitrary-file reader or source decoder owner is introduced. A preview error
is separate from successful file publication. See [dependency selection](dependencies.md)
and [the user guide](../manual/editor/textures.md).


## HDR texture image output

The same selected Texture loader, slice/layout helper and ViewImageTask now accept
a bounded Radiance RGBE .hdr profile. `texture_hdr_pixels` admits only HdrColor
RGBA16Float/RGBA32Float with actual selected alpha1. Little-endian float32 and IEEE
binary16 normal/subnormal scalars are projected into owned linear RGB float rows;
RGB must be finite/nonnegative. No exposure, tone mapping, alpha/color conversion,
source decoding or metadata roundtrip occurs. The full selected HDR texture limit
is256MiB, detached RGB floats192MiB, extent4096axis and encoded output68MiB;
existing cooked-artifact admission limits also apply. Byte output keeps its prior
128MiB source/64MiB RGBA limits.

`encode_hdr_image` is independently authored from the published numeric/container
format. It uses one normalized shared exponent per RGB pixel; black is explicit,
nonzero magnitudes outside exponents-127..127 reject, and mantissas quantize
weaker channels. The writer declares working sRGB/Rec.709 primaries/D65 rather
than inheriting the format's different default primaries. It makes no calibrated
photometry or lossless-float claim. Widths below8 use flat pixels; other widths use
bounded literal channel packets of at most127bytes. This keeps allocation/output
bounds deterministic without a copied codec implementation. Existing pinned HDR
import decoding verifies generated files and independent known-value vectors.

The published format specification describes the header, orientation, shared
exponent and scanline structure. [Radiance File Formats](https://floyd.lbl.gov/radiance/refer/filefmts.pdf).
Working chromaticities follow the [ICC sRGB registry](https://registry.color.org/rgb-registry/srgb).
The output MIME is the registered [image/vnd.radiance](https://www.iana.org/assignments/media-types/image/vnd.radiance).
These references define interchange data, not an imported encoder implementation.

Native Texture preview emits an output-format hint for its current selected
float HDR variant, opening the .hdr-only chooser with the proper filename.
Unsupported selections still validate in the shared detached producer. Rendered
Scene/Game display pixels cannot be relabelled as original HDR; their native and
MCP paths retain PNG/WebP/opaque JPEG admission. The shared writer encodes before the existing
atomic file transaction, so failed samples/formats preserve a good destination.

The HDR file exists on disk and status returns its path/MIME. Requested current
and later owned previews use the written RGBE artifact. The existing image task,
preview/read operation, output grant and exact-operation guards remain authoritative.
A preview failure stays independent of successful file publication. Full-file
download is not an MCP operation. See [the user guide](../manual/editor/textures.md).

## Owned HDR inspection

The FORGE-owned bounded reader admits exactly the existing writer's fixed working
primaries/profile, top-row-first resolution, flat pixels below8px width and bounded
literal channel packets otherwise. One shared header prefix owns interpretation.
It does not load imported HDR files or call a native source decoder in the editor.
Changed primaries/profile, invalid dimensions, malformed/truncated/oversized
packets, unnormalized RGBE and trailing bytes reject before returning an image.
Encoded admission is68 MiB, each axis4096, detached RGBA8 64 MiB; only one row of
RGBE scratch is needed, without allocating a full RGB-float decode. Common owned
file reads may admit the existing PNG16 byte ceiling before format-specific
validation; these are independent limits, not a process-memory ceiling.

Decoded radiance follows the actual quantized mantissas: channel*2^(exponent-136),
with canonical zero for black. Inspection applies fixed unit exposure and the
per-channel curve c/(1+c), then standard sRGB encoding and nearest8-bit opaque
samples. The existing1024-axis resampler and8 MiB PNG cap apply afterwards.
This display inspection is distinct from linear/radiometric samples, the native
Texture viewer's display processing, Scene/Game exposure and lossless full HDR.
Working chromaticities and transfer follow the
[ICC sRGB registry](https://registry.color.org/rgb-registry/srgb).
No photographic calibration, cross-platform libm bit identity or full
photographic tone-reproduction operator is claimed.

ViewImagePreview carries profile hdr_reinhard_srgb. capture.status and
capture.image expose it as preview_profile only for the exact ready inspection.
The full artifact keeps texture.profile linear_rgbe and its image/vnd.radiance
MIME. Current HDR capture reads its completed file, as JPEG already does; later
inspection uses the same reader/transform. Typed RGB-float inspection canonicalizes
through the existing writer and reader, so it cannot substitute unquantized
source channels. Black, weak-channel quantization, exponent extremes, flat/packet
boundaries, malformed input, current/later metadata and last-good artifact behavior
have focused checks. PNG8/16, WebP, JPEG and compressed Texture inspection retain
their existing interpretation and header-based owned-file admission.
