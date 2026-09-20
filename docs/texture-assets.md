# Texture data, import and runtime ownership

Phase7 currently implements bounded CPU texture artifacts, a cooked-file resource
provider and private image preparation. These are internal integration APIs.
Container/Basis integration, publication/editor workflows and GPU realization are
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
premultiplied and unknown. Material alpha testing/blending remains material intent.

Sampler state includes independent min/mag/mip filters, wrap U/V/W, anisotropy,
comparison, LOD bias/range and border color. Validation uses finite values and the
D3D12-profile bias/anisotropy bounds. It permits negative minimum LOD and finite
HDR border colors, as the native sampler contract does. Anisotropy requires linear
filters. GPU sampler deduplication is not yet provided by the CPU structure.

## Image preparation

The private worker adapter admits PNG, JPEG, TGA and HDR/RGBE. It uses exact pinned
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

DDS/KTX/Basis, BMP/WebP/EXR and optional TIFF/SGI need their own audited admission
paths; this adapter rejects them rather than advertising untested support.

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
