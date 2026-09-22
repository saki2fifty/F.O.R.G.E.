# Source-verified dependency issues

This register records affected paths, selected boundaries and remaining exposure.
It does not grant permission to suppress sanitizer findings or change pins.
See[dependency policy](dependency-policy.md).

| Exact source | Observed issue | FORGE boundary / status |
| --- | --- | --- |
| Ozz0.17.0/744eb9d, `gltf2ozz.cc::Create*RestPoseKey` | Unanimated matrix-authored node fallback channels read only TRS fields. A parent with matrix translationX=5 samples near0 in its child animation; equivalent explicitTRS samples near5. | Reproduced using official converter and FORGE archive admission/sampler on2026-09-20. Existing animation-only path is not generalized by this finding. New model animation integration must canonicalize validated matrix rest transforms to explicitTRS in private converter transport and verify skeleton/clip compatibility. Private canonical adapter and permanent official-converter regression now retain matrix ancestor rest values; whole-model worker integration and the official-converter regressions pass. No vendor patch or pin change. |
| DiligentTools7d113906, `TextureLoader/src/PNGCodec.c` | Read callback ignores supplied source length; a truncated span over a larger allocation still decodes | New texture importer uses the existing libpng1.6.55 through a bounds-checked callback. All truncated fixture copies reject. Old image call sites are not automatically covered. |
| Same pin, `Image.cpp::TIFFReadProc` | Requested bytes copied without checking remaining input | Native TIFF route excluded from texture importer; no supported TIFF-import claim. |
| IJG libjpeg9e bundled at selected Diligent revision, `jfdctint.c:199`, `jchuff.c:335` | Strict UBSan reports signed shifts in integer DCT and Huffman encoding; floating DCT does not resolve the latter | New JPEG import uses pinned stb2.29 with private symbols. No runtime JPEG encoder is provided by this change. Old IJG consumers retain the upstream issue. |
| Flecs4.1.6/fb55f3c | Bounded managed-file-include filename-buffer leak | Only the previously approved narrow exception applies; see[Flecs issue registry](flecs-known-issues.md). Separate signature-checked evidence, no global suppression. |

Verified2026-09-20. The[texture contract](texture-assets.md) distinguishes bounded
CPU admission, native processing, supported integration and actual codec tests.

## KTX Software4.4.2 selection

Exact4d6fc70eaf62ad0558e63e8d97eb9766118327a6, verified2026-09-20:

- `lib/basis_encode.cpp:872` allocates ETC1S global data using `new[]`;
  `lib/texture2.c:1243` destroys it using `free`. Strict ASan reproduces this
  mismatch with a valid4×4 texture. FORGE does not call that encoding wrapper.
  The bundled official Basis `basis_compressor` writes KTX2 directly, and KTX
  reads/transcodes the resulting bytes. That selected path passes the strict probe.
- `external/basisu/transcoder/basisu_transcoder.cpp` defaults to unaligned typed
  reads on x86. Its automatic UBSan detection is Clang-specific. FORGE selects the
  existing `BASISD_USE_UNALIGNED_WORD_READS=0` byte-wise path in **every** build,
  including normal Windows builds. This is not a sanitizer suppression.
- `ktxTexture_GetRowPitch` retains GL padding and floor block counts. The adapter
  uses format-specific checked KTX2 layout, and the native KTX1→KTX2 conversion
  removes legacy padding. Odd-size and tail-mip fixtures cover this distinction.

No upstream source patches, automatic upgrades or new defect exceptions are used.
These findings do not mean all other KTX entrypoints were validated or are safe
for arbitrary input; only the admitted, worker-owned subset is selected.

A later metadata-rewrite regression also exposed KTX4.4.2 hash-entry leaks:
`ktxHashList_DeleteEntry` only detaches; native writer/orientation replacement
leaks the old entry. No exception is accepted. After validating interpretation
metadata, FORGE destroys and reconstructs the **temporary conversion object's**
entire public hash list before writing. Original source bytes remain untouched;
unknown KVD is not part of the cooked texture schema. Owned Basis data/normal
output similarly rebuilds its temporary writer metadata, with a FORGE encoder
record. Source/cooked provenance belongs to the asset build record. This avoids
native replacement without freeing opaque entries manually or patching upstream.

Strict testing of `ktxTexture2_DeflateZLIB` also reports misaligned typed reads in
the pinned bundled `basisu_miniz.h:1961`. The inflater source has the same native
x86 read policy; unlike the Basis transcoder switch, miniz overwrites an external
macro value. FORGE rejects KTX2 supercompression scheme3 before native parsing.
This optional KTX path is not required by the glTF Basis extension. No supported
zlib KTX writer/reader claim, patch or sanitizer suppression is introduced.

## BMP source profile

Pinned stb2.29 at46fcb303, `stb_image.h::stbi__bmp_parse_header/bmp_load/bmp_info`:
CORE palette count uses a different header-size offset;56-byte BITFIELDS consumes
both in-header bytes and extra masks; non-paletted gaps are skipped in two places.
The importer admits24-bit CORE,40/108/124-byte headers and zero non-paletted gap.
These are explicit conversion diagnostics, not claims that those BMP variants
are universally invalid. Native parsing also ignores embedded color profiles and
uses zero bytes on exhausted reads; FORGE checks color-profile policy and full
pixel/palette extents before native decoding. Palette indices must address a
present entry. `INT_MIN` height rejects before native `abs`; masks are bounded,
contiguous and disjoint. No source patch, replacement decoder or suppression.

[Microsoft BMP fields](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapv5header)
and[INFO stride/palette semantics](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
were checked against the exact source on2026-09-20.

### RGBE extent admission

The same pinned stb `stbi__hdr_load` ignores the return from `stbi__getn` in
flat mode and `stbi__get8` supplies zero at EOF in RLE mode. Mixed late flat/RLE
rows also enter its fallback that resets row indices. FORGE now checks complete
flat/RLE extents and consistent row encoding before that native entrypoint.
Old repeat encoding is diagnosed rather than interpreted as literal pixels.

## DiligentFX punctual-light adapter

Exact FX`aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`,
`Shaders/PBR/public/PBR_Shading.fxh::ApplyPunctualLight`, inspected2026-09-20:
its spot path uses linear cosine falloff and retains the spotlight cone axis as
the BRDF light direction; only the point path switches to the position-derived
ray. The pinned Khronos KHR_lights_punctual reference squares the cone factor.
FORGE's shader adapter applies that factor, then invokes the native point-light
path with the original position/range/shadow data. Native BRDF/IBL/shadow algorithms
remain upstream; there is no vendor patch or version upgrade.

Native punctual distance normalization and half-vector normalization also have
undefined zero inputs. The wrapper rejects those contributions, stages native
lighting output, and retains existing lighting if the candidate produces nonfinite
HDR values. The return flag distinguishes numerical rejection for its consumer.
This does not silently restrict authored LocalScale or claim that the full scene
renderer's diagnostics are already connected. A WARP compute reproduction compares
the corrected spotlight to the position-derived native point BRDF multiplied by
the squared cone factor, and covers coincident/opposite/overflow cases. Native
execution of this newly added fixture is pending.

## nlohmann/json mixed-number equality

Exact selected commit `55f93686c01528224f448c19128836e7df245f72`,
`include/nlohmann/json.hpp:3664–3670`, verified2026-09-21: mixed signed/unsigned
`operator==` converts the unsigned operand to the signed integer type. On FORGE's
supported compilers this can make `UINT64_MAX` compare equal to `-1`. Integer/float
comparison also converts to floating point and can hide differences above2^53.
This is not a validation primitive.

FORGE validates schema candidates before their unchanged fast path. Authoring
scene/prefab comparisons use a narrow value comparator that preserves integer
width, permits equal nonnegative signed/unsigned representations, and distinguishes
integer from floating-point storage. Prefab conflict checks and draft dirty state
use that same rule. Named native transport still validates storage bounds before
assignment. No dependency patch or version change is made. Regression cases cover
invalid unsigned defaults, direct scene edits and prefab publication that must not
be mistaken for no-ops or reach a durable writer.
