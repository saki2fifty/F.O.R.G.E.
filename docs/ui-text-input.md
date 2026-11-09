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

Shift+Left/Right uses a transient observation of the same native field's
one-scalar key transition. The pinned widget handles keys in a capture listener;
a call-local listener on that field observes its selection afterwards. Ancestor
cancellation prevents this observation. After dispatch, the same surviving focus,
value and observed range must remain, with no reentrant public key dispatch.
Only a proven movement of one endpoint by one scalar establishes the native
anchor and moving caret. The helper aligns the oriented selection to complete
grapheme boundaries using the public selection API; subsequent extension,
shrinking and reversal retain the native anchor. Ambiguous transitions, changed
values/focus, retired controls, later callback selection changes and active
composition retain native behavior. There is no persistent cursor/anchor copy.

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

Other modified keys, word navigation, vertical movement and pointer selection
remain native. Plain keys move in logical text order, without full paragraph
bidirectional semantics. Complete IME, accessibility and general Unicode editor
coverage are not implied. Native readonly is not available at this pin; supported
disabled fields remain authoritative. Ordinary key edits never call context
SetText, which bypasses native maximum length.

## Export, automation and validation

Enabling the option requires exact runtime-kit capability
`engine.runtime_ui_grapheme_editing: 2`. This profile includes native Shift-arrow
selection; the older plain-key profile is rejected. Missing/malformed/mismatched support is
rejected before replacing a good distribution. Default projects preserve their
existing kit compatibility. The standalone host reads the packaged setting with
source files unavailable. No Scene/asset format, serialized identity, cook format
or installed gameplay SDK ABI changes.

Existing settings and UI source tools can author this profile and native fields
under their normal grants, revisions, histories and saved-baseline rules.
Native field observation/focus/text/key tools use these same presenter methods
and the existing Play provider; see [the field contract](#native-field-automation).
`play.input.submit` remains the gameplay semantic queue. IME composition injection
is not exposed as an agent operation.

The focused helper test runs all853official Unicode18 cases plus admission/key
selection checks. Actual native-control tests cover text/password/textarea,
combining marks, emoji sequences, flags, Hangul, events, limits, modifiers,
composition forwarding, blur/removal/reset and both font modes. The original
example is exercised with its four IDs preserved and source-hidden relocation.
Platform/physical input review remains separate from these CPU consumers.

## Native field automation

UiPresenter::text_fields observes one current document instance from the displayed
set. A call-local public native scan admits4096DOM nodes and64text/password/
textarea controls. IDs are nonempty valid UTF-8 up to256bytes and must be unique
across all scanned DOM nodes, including other element types. Missing/ambiguous
IDs and truncated scans cannot authorize an action. No field registry or cached
text model is created. Hidden/disabled fields can be observed but not edited.

Observations include the native session/world generation and presentation
revision, field kind, visibility/enabled/focus state and scalar selection range.
Non-password values are included only when the existing UI scalar validator
admits valid non-NUL UTF-8 up to1024bytes. Oversized or malformed values are
omitted without truncating or normalizing the field. Password values are omitted
from observations and action results. Routine field observations are not a
security boundary around trusted native project code.

UiPresenter::text_field_action checks the current presentation revision and
exact unique native element before action. A field must be visible/enabled and
there must be no held native UI key or pointer press. Focus uses the existing
focus_target owner and public native Focus, guarded by a native ObserverPtr
against callback retirement. Camera-output focus/input requires an existing
current native focused/hovered surface hit and its geometry mapping; it does not
invent a surface or bypass IME projection. Text/key additionally requires this
exact field/output still to hold native focus.

Text uses the existing text/ProcessTextInput path. Named keys send a balanced
native down/up pair; next/previous/accept reuse native navigation. Home/end and
select-all retain native semantics. `select_left` and `select_right` send paired
Shift+Left/Right through that same key owner. The private Rml-free key catalog
drives dispatch and automation schema discovery. The saved grapheme option affects
its four plain keys and these two Shift arrows. Events, bindings, native restrictions and declared command
queues retain their existing owners. Action results report delivered/consumed
and fresh observations; they do not acknowledge gameplay or persist UI values.
Native model/game updates may replace a field value between observations.

Play automation compares the actual shown session/world/presentation revision
with the current Play owner before invoking the input provider. Mutations retain
launch/session/generation, execution grant, authoring target/revision/sequence
and replay receipts. RuntimeUiHost rejects suspended/preparing input. Its Play
provider uses the existing truthful GameInput release policy with menu routing retained.
Read access never grants native input. Source/Scene/history and the installed
gameplay ABI remain unchanged. See [MCP field tools](mcp.md).
