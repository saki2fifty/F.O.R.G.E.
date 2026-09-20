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
