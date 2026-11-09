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
| Font files | Standalone scalable Unicode face at index 0; 4 MiB per file |
| Backend face storage | 32 faces, 16 MiB retained font bytes including the default font |
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
grapheme-aware selection/deletion, complete international IME or accessibility,
font collections, variable axes, color glyphs or a general typography editor.
RmlUi still owns wrapping, DOM layout, focus and text input. Font coverage and
author-selected language/direction affect the result.

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
