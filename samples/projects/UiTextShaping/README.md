# UI Text Shaping

Open this project, open `Scenes/TextShaping.scene.json`, then start Play and
select Game. The Scene includes a Camera and an interactive HUD.

Compare normal Latin ligatures with tracked text, combining accents and the
explicit right-to-left Arabic run. Pause and Resume use the ordinary game
controls. The text field is local UI state.

**Edit whole UI characters** is also enabled in Game defaults. Click the field,
select its text with Ctrl+A, and paste `áb` (a combining accent after `a`). Press
End, Backspace, Backspace: the accented character is removed together. Home and
plain Left/Right/Delete also use whole character boundaries. Modified keys,
pointer selection and active IME composition keep native behavior. Save Settings
and restart Play after changing either UI option. See
[Edit whole UI characters](../../../manual/editor/ui-text-input.md) for bounds and controls.

`Tools → Project Settings → Game defaults → Shape UI text` is enabled. Save
settings and restart Play after changing it. Edit the RML/RCSS under `Assets/UI`
through Content, Save source and restart Play. Use a matching runtime kit for
export. Full paragraph bidirectional layout and complete international text
editing are outside this example's scope.

`Assets/Fonts/Cairo.ttf` is unchanged font data from the pinned RmlUi distribution,
Copyright 2009 The Cairo Project Authors, under SIL OFL 1.1. See
`FONT-LICENSE.txt`. FORGE's independent adapter uses the pinned shaping and
raster libraries; sample backend implementation is not included.

See [Shape game UI text](../../../manual/editor/ui-text-shaping.md) for controls,
saving, export and current limits.
