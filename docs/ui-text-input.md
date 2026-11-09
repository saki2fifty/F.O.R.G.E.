# Native UI text input boundaries

The optional boolean `project.game.ui.grapheme_editing` defaults to false.
**Edit whole UI characters** in Project Settings and existing MCP settings tools
use the same GameSettings validation, draft, digest and Save owner. The option
does not enter Scene Undo or player overrides. Save and restart Play. Editor
RuntimeUiHost, standalone GamePresentation and isolated UI inspection read the
same saved choice. It is independent of the font shaping option.

## Existing native authority

UiPresenter continues to route keys, text, focus and output contexts. RmlUi owns
the actual field value, selection, restrictions, native events and widget state.
The original FORGE adapter uses only public ElementFormControlInput,
ElementFormControlTextArea, TextInputContext and TextInputHandler interfaces.
There is no copied widget implementation, parallel text buffer, DOM, history,
focus manager or agent-specific editing path.

For plain Left/Right/Backspace/Delete in visible enabled text/password/textarea
controls, the helper computes Unicode extended-grapheme boundaries from the
current native UTF-8 value. Coordinates passed to the native selection API are
Unicode scalar offsets. A temporary selection lets the original native key
action move/delete a complete adjacent character. Partial selections expand to
whole boundaries. An interior caret moves to the adjacent boundary; interior
deletion selects the containing character. Source bytes are never normalized.

Native key events are dispatched once. A canceled action restores the original
selection only when the same field/focus survives, its value is unchanged and
the prepared selection remains untouched. Callback-authored edits are not
rolled back. A shared retirement token avoids dereferencing a control removed
during dispatch. Native output-focus release, reload, reset, staged retirement
and cold shutdown retain their existing ownership.

The optional handler forwards the supplied platform handler through public
context proxies. Native contexts remain borrowed until OnDestroy. Proxies
observe composition calls without owning composition text, and forward all
public methods to the same native object. Active composition bypasses boundary
preparation. Deactivation clears observations; destruction invalidates the token
before notifying the platform delegate. The platform handler must outlive the
presenter. The normal SDL preedit/empty-commit/text-input sequence is preserved.
The pinned native nonempty custom-commit limitation is recorded in
[dependency known issues](dependency-known-issues.md#native-ui-custom-composition-commit).

## Selected Unicode profile

Private static utf8proc2.12.0 at
`79cdd5ab40c79afa559f689ffc13b76812dee1ac` supplies Unicode18.0.0 extended
grapheme boundaries, matching UAX29 revision49. Stateful segmentation processes
every adjacent pair in order, beginning with zero state. Glyph-shaping clusters
are not substituted for Unicode boundaries. Existing RmlUi/FreeType/HarfBuzz
pins remain unchanged. See [dependency baselines](dependencies.md).

The helper validates UTF-8, native scalar ranges, a64KiB byte limit and
16,384scalar limit. Non-admitted fields retain native editing without truncation.
Font/glyph/document limits apply independently. The bounded full-field scan
has no persistent invalidation cache and is not a physical performance claim.

Modified keys, word navigation, vertical movement and pointer/Shift selection
remain native. Plain keys move in logical text order, without full paragraph
bidirectional semantics. Complete IME, accessibility and general Unicode editor
coverage are not implied. Native readonly is not available at this pin; supported
disabled fields remain authoritative. Ordinary key edits never call context
SetText, which bypasses native maximum length.

## Export, automation and validation

Enabling the option requires exact runtime-kit capability
`engine.runtime_ui_grapheme_editing: 1`. Missing/malformed/mismatched support is
rejected before replacing a good distribution. Default projects preserve their
existing kit compatibility. The standalone host reads the packaged setting with
source files unavailable. No Scene/asset format, serialized identity, cook format
or installed gameplay SDK ABI changes.

Existing settings and UI source tools can author this profile and native fields
under their normal grants, revisions, histories and saved-baseline rules.
`play.input.submit` remains the gameplay semantic queue. Native field focus,
text/key injection and IME composition are not exposed as MCP tools; do not claim
automated native-field interaction from settings/source access.

The focused helper test runs all853official Unicode18 cases plus admission/key
selection checks. Actual native-control tests cover text/password/textarea,
combining marks, emoji sequences, flags, Hangul, events, limits, modifiers,
composition forwarding, blur/removal/reset and both font modes. The original
example is exercised with its four IDs preserved and source-hidden relocation.
Platform/physical input review remains separate from these CPU consumers.
