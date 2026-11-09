# UI text shaping

`project.game.ui.text_shaping` selects a bounded horizontal shaping backend for
game UI. It is an optional boolean; absence and `false` retain the existing
RmlUi FreeType backend. Opening Project Settings does not insert the option.
The native **Shape UI text** control and `project.settings.edit` use the same
ProjectSettings draft, validation, disk baseline and Save behavior. The option
is project-owned, outside Scene history and player preference overrides.

Save settings and restart Play to change the backend. UiPresenter owns one
font interface for its complete RmlUi lifetime, including staged and retained
contexts. Editor RuntimeUiHost, standalone GamePresentation and the isolated
native UI inspection worker select the same saved option. Shutdown retires
contexts and renderer resources before releasing the font interface. There is
no second font manager or glyph database in gameplay or automation.

## Native implementation

The FORGE adapter uses the public RmlUi FontEngineInterface, FontEffect,
CallbackTexture and RenderManager APIs. FreeType supplies face admission,
metrics and grayscale outline rasterization. HarfBuzz supplies glyph selection,
advances, offsets and source byte clusters. The adapter does not use private
RmlUi Core headers or the sample font backend implementation.

Native text-shaping context supplies language, horizontal direction, kerning
and letter spacing. RCSS uses `-rmlui-language` and `-rmlui-direction` with
the pinned RmlUi parser. Direction may be `ltr`, `rtl` or native automatic
selection. Tracking disables Latin `liga` and `clig` features and adds spacing
once per native cluster. Kerning `none` disables `kern`. Source text, bindings
and localization values remain UTF-8; no glyph positions are serialized.

Project `@font-face` file loads pass through the presenter's existing admitted
FileInterface. Faces support family aliases, family lists, style and nearest
weight selection. A missing glyph can select a declared fallback covering the
whole run; there is no general per-character mixed-script fallback. Missing
coverage retains the selected face's missing-glyph rendering. Default Lato is
available as before. Supply a font containing the required script.

The existing native font effects use their public glyph-metrics and texture
callbacks. Effect lists are comma-separated in RCSS. Glyph pages are immutable
after publication, and renderer-owned textures retain the existing resource
lifetime. Font changes can invalidate handles only through the native font
resource lifecycle; restart Play after replacing a font family.

## Bounds and unsupported typography

| Resource | Limit |
| --- | --- |
| One input run | 64 KiB UTF-8, 16,384 output glyphs |
| Font files | Standalone scalable Unicode font or TTC/OTC v1/v2 collection; 4 MiB per file, at most 32 collection faces |
| Backend face storage | 32 admitted faces, 16 MiB unique retained font bytes including the default font; aliases/indices share identical bytes |
| Sized faces | 128; sizes 1–512 px |
| Glyph cache | 16,384 entries |
| Glyph atlas | 128 immutable pages, 32 MiB retained RGBA; page side at most 1,024 px |
| Glyph bitmap/effect extent | 1,022 px per dimension |
| Preparation | At most 32 MiB temporary glyph RGBA |
| Font effects | 16 effects per group; 64 cached groups |
| Tracking | Finite absolute spacing at most 4,096 px |

Existing document, file-admission and aggregate UI resource limits also apply.
Native callback failures become presenter diagnostics and participate in its
normal last-good candidate handling. A rejected allocation is not a claim that
the renderer accepted an incomplete replacement.

This profile shapes individual horizontal runs. It does not implement paragraph
bidirectional layout, joining across separate text spans, vertical text,
complete international IME or accessibility,
variable axes, named variation instances, color glyphs or a general typography editor.
RmlUi still owns wrapping, DOM layout, focus and text input. Font coverage and
author-selected language/direction affect the result.
The independent [native input boundary option](ui-text-input.md) supplies bounded
plain-key grapheme movement/deletion; shaping alone does not provide it.

## Export and compatibility

The optional field preserves existing project and Scene versions. A project
which enables shaping requires runtime-kit capability
`engine.runtime_ui_text_shaping: 1`; absent, malformed or mismatched support is
rejected before distribution promotion. Default projects retain their previous
kit compatibility. The standalone host reads the saved packaged configuration,
not the editor's current checkbox or the original project directory.

UI files and fonts continue through the existing admitted resource observation,
catalog dependencies and runtime package closure. Shaping introduces no extra
asset identity, cook format, Scene component or installed C++ SDK ABI. HarfBuzz
is static and private to the optional UI presentation build. Core runtime,
catalog and packaging targets do not acquire a native UI dependency.

Dependency pins, build options and notices are recorded in
[dependency baselines](dependencies.md). See [runtime UI](runtime-ui.md) for
resource ownership and [MCP](mcp.md) for the existing project
settings permissions and revision checks.

## Font collections

The existing UiResources validator admits `.ttc` and `.otc` with TTC headers
version 1.0 or 2.0, bounded face counts and checked absolute sfnt directory/table
spans. Version 2 signature spans are structurally bounded, not authenticated.
Standalone admission remains compatible. Native FreeType remains responsible
for opening scalable Unicode faces and decoding actual glyph tables.

Use the pinned native `@font-face` property `-rmlui-face-index` for a zero-based
face index, default 0. No FORGE URL fragment or replacement style parser is added.
The optional original font adapter supplies the same validated face index to
FreeType and HarfBuzz, includes it in deduplication and shares immutable source
bytes among aliases/indices. Native family/style/weight/fallback selection and
renderer lifetime remain authoritative. The default backend uses its existing
public font API. No new font manager or installed gameplay SDK ABI is introduced.

Collection resource observations include `faces` and
`runtime_features.font_collections: 1`. Existing catalog refresh preserves IDs,
automatic source closure includes the whole collection and source-hidden content
uses the usual runtime access. Used collections require exact kit capability
`engine.runtime_ui_font_collections: 1` before export promotion. Projects without
collection resources retain their existing compatibility. Source registration,
metadata inspection and RML/RCSS edits use existing editor/automation/MCP owners.
See the [collection workflow](../manual/editor/ui-text-shaping.md).

### Saved face discovery

The same headless font admission owner derives `font_faces` alongside `faces`
in `forge.ui_source` metadata. Every row has its zero-based `index`; optional
`family`, `style`, `full_name`, `postscript_name` and numeric `weight` describe
the saved bytes. Content's asset Inspector displays family/style/weight, and
ordinary automation `asset.read` / MCP `forge_asset_read` returns the same rows.
Existing source registration and resource refresh publish the metadata through
the catalog owner, preserving source bytes and existing AssetIds. Old records
remain readable and acquire rows on refresh. No new grants, mutation tools,
history owner, font database, native font selection or runtime-kit capability is
introduced by informational discovery.

The independently authored reader uses the already-checked sfnt directories.
It examines optional OpenType `name` versions 0/1, up to 512 records and 32
language tags, and `OS/2.usWeightClass` in 1..1000. Typographic names 16/17 take
precedence over legacy 1/2; names 4/6 supply full and PostScript labels. English
Windows names, English language tags, Unicode names and other supported names
have deterministic preference. Unicode and supported Windows Unicode encodings
use strict UTF-16BE decoding; Macintosh Roman names use only their ASCII subset.
Other legacy codepages are omitted. Labels are bounded to 64 UTF-8 bytes without
truncation. Malformed, missing, duplicate or unsupported optional naming data
omits labels without rejecting an otherwise admitted font or changing its index.
These labels are informational: native FreeType/RmlUi still owns usable font
faces, glyph coverage, style selection and rendering. Variation controls and
system-font discovery remain outside the supported profile.

The bounded name-table layout follows the primary
[OpenType naming specification](https://learn.microsoft.com/en-us/typography/opentype/spec/name).

The existing SourceImport controller admits font files as a reviewed copy-only
batch, through both native Import files and the same asset.import automation/MCP
operations. Its ordinary SourceCopy transaction owns bytes, destination checks,
source digests and commit. Font-containing batches reject cooking with typed
`copy_only`; they cannot index an importer route. After copying, UI registration
uses the same font validator/catalog owner. No second import queue or file writer
is added; native registration and raw copying remain separate steps.
