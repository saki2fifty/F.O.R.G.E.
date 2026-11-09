# UI Text Shaping

Open this project, open `Scenes/TextShaping.scene.json`, then start Play and
select Game. The Scene includes a Camera and an interactive HUD.

Compare normal Latin ligatures with tracked text, combining accents and the
explicit right-to-left Arabic run. Pause and Resume use the ordinary game
controls. The text field is local UI state.

Scroll down to **Variable weight** to compare the original geometric `H` font
at100,400 and900. `Assets/Fonts/Weight.ttf` is FORGE-authored fixture data, with
only `H` and space. Its weight axis responds to ordinary `font-weight` in
`Assets/UI/hud.rcss`; change a value, Save source and restart Play. Select the
font in Content to inspect its recorded axis range. Use your own licensed font
for general game text. Other variation axes stay at their defaults.

Scroll to **Color layers** for the independently authored geometric palette
and foreground H fonts. Palette0 supplies fixed hues. A foreground layer follows
text color; text alpha fades every layer. The final card uses half-alpha text
with native shadow/outline effects. Inspect a font in Content for its recorded
COLRv0 details. These fixtures do not establish other color-font formats or a
general emoji typeface. Edit `Assets/UI/hud.rcss`, save and restart Play.

**Edit whole UI characters** is also enabled in Game defaults. Click the field,
select its text with Ctrl+A, and paste `áb` (a combining accent after `a`). Press
End, Backspace, Backspace: the accented character is removed together. Home and
plain Left/Right/Delete also use whole character boundaries. Hold Shift with
Left/Right to extend or shrink a whole-character selection while keeping its
native anchor. Ctrl+Left/Right uses native word grouping with complete-character
destinations; add Shift to select words. Word deletion, other modified keys,
pointer selection and active IME composition
keep native behavior. Save Settings
and restart Play after changing either UI option. See
[Edit whole UI characters](../../../manual/editor/ui-text-input.md) for bounds and controls.

With a project-authoring MCP connection and execution grant, an agent can read
`forge_play_ui_fields`, focus the input with element ID `field`, then use
`forge_play_ui_text` and `forge_play_ui_key`. Read its current document instance
and presentation revision first. These actions use the same native field; its
runtime text is not saved into the RML or Scene history.

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
