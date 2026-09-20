# Source-verified dependency issues

This register records affected paths, selected boundaries and remaining exposure.
It does not grant permission to suppress sanitizer findings or change pins.
See[dependency policy](dependency-policy.md).

| Exact source | Observed issue | FORGE boundary / status |
| --- | --- | --- |
| DiligentTools7d113906, `TextureLoader/src/PNGCodec.c` | Read callback ignores supplied source length; a truncated span over a larger allocation still decodes | New texture importer uses the existing libpng1.6.55 through a bounds-checked callback. All truncated fixture copies reject. Old image call sites are not automatically covered. |
| Same pin, `Image.cpp::TIFFReadProc` | Requested bytes copied without checking remaining input | Native TIFF route excluded from texture importer; no supported TIFF-import claim. |
| IJG libjpeg9e bundled at selected Diligent revision, `jfdctint.c:199`, `jchuff.c:335` | Strict UBSan reports signed shifts in integer DCT and Huffman encoding; floating DCT does not resolve the latter | New JPEG import uses pinned stb2.29 with private symbols. No runtime JPEG encoder is provided by this change. Old IJG consumers retain the upstream issue. |
| Flecs4.1.6/fb55f3c | Bounded managed-file-include filename-buffer leak | Only the previously approved narrow exception applies; see[Flecs issue registry](flecs-known-issues.md). Separate signature-checked evidence, no global suppression. |

Verified2026-09-20. The[texture contract](texture-assets.md) distinguishes bounded
CPU admission, native processing, pending integration and actual codec tests.

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
