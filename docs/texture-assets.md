# Texture data, import and runtime ownership

Phase7 currently implements bounded CPU texture artifacts, a cooked-file resource
provider and private image preparation. These are internal integration APIs.
Concrete texture publication is tested; editor workflows and GPU realization are
still being implemented. A format having a data representation is not evidence
that its encoder, viewer or renderer has been delivered.

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
separate realization check. D3D12-profile compressed volume textures reject.

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
comparison, LOD bias/range and border color. Validation uses finite values and the
D3D12-profile bias/anisotropy bounds. It permits negative minimum LOD and finite
HDR border colors, as the native sampler contract does. Anisotropy requires linear
filters. GPU sampler deduplication is not yet provided by the CPU structure.

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
conversion to KTX2. One-dimensional textures and compressed volumes are outside the
current texture contract. Orientation must be right/down/in; nonidentity channel
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
This adapter is not yet connected to Content import or GPU resource publication.

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
width/height at2048, alongside the shared texture budgets. This is container
preparation, not an editor import workflow or proof of device support.

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
only the importer uses it, and tests use its encoder to generate owned fixtures.
This does not expose a WebP export or runtime decoder API. Source/output budgets
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
are unchanged. This is a live document preview, not yet the browser thumbnail cache.

The standalone texture import document owns its draft/save workflow and shows its
preview before a collapsed import-settings section. Generated model textures use
a read-only central document. Neither view creates a Flecs world or authored data.
