# Native UI form access

UiPresenter::form_controls, form_options and form_control_action operate on the
displayed native document set through the same owner as text_fields. The private
ui_control_access helper supplies classification, IDs, visibility/admission and
one bounded walker. It preserves the text-only DOM scan/count policy and uses an
independent form scan including native non-DOM option children. There is no form
registry, copied widget implementation, second DOM, retained option token store or
alternate gameplay/authoring writer.

Forms include checkbox/radio input, select and read-only progress; other inputs
and password/text/textarea values are outside this snapshot. Option text reads
bounded public native text leaves, refuses nested controls and never serializes
source. Native value admission and secret handling remain explicit. See
[MCP form tools](mcp.md#native-ui-form-tools) for fields, budgets and failures.

At the pinned native revision, dropdown GetNumOptions/GetOption can normalize
pending DOM children and emit Change. Reads call those getters only after the
native control has no pending DOM children, otherwise returning readiness false.
Native Update remains the normalization owner. Read calls never move focus or
invoke native actions. Option state uses the existing asset content digest over
current scope/index/value/text/admission, excluding the mutable selected hint.

Actions share current document scope, presentation revision, unique-ID, held-input,
output/surface mapping and native focus gates with text actions. Call-local native
observers detect retirement. Identity, kind and option facts are checked again
after native blur/focus callbacks. Already-satisfied requests perform no focus or
activation. Native Click and SetSelection own events and radio-group semantics;
no extra Change event is dispatched. Results report delivery separately from
observed change, with null change if the activated target detaches, retires or changes kind.
Callbacks keep their ordinary native effects; there is no authored rollback.

RuntimeUiHost, the editor provider and the MCP workflow host call these presenter
methods. Automation metadata supplies the five MCP bindings and their schemas;
read and execute grants, Play scope, ordinary mutation receipts and stale guards
retain their owners. The existing gameplay input release runs before semantic
actions. Source creation, bindings, Scene history, Save, packaging and standalone
input retain their existing owners. MCP operates in editor Play; no server is
added to an exported game.

The nonvirtual PImpl presenter gains methods without data-layout changes. Its
header is not part of the installed native gameplay SDK. This feature does not
change SDK fingerprints, serialized schemas, dependency pins, or runtime editing
and shaping capability profiles.

Native extension callbacks must retain removed native owners until synchronous
dispatch returns, or defer removal. Observers do not keep widgets alive; immediate
dropdown destruction inside Change is a pinned native limitation. See
[the independent finding and boundary](dependency-known-issues.md#native-ui-dropdown-callback-lifetime).

Focus callbacks also have a native retained-pointer limit. Defer focused-element
removal; deliberate early detach requires the native caller to retain the owner,
restore attached native focus after dispatch and release the owner afterwards.
See [focus callback removal](dependency-known-issues.md#native-ui-focus-callback-removal).

## Native button access

Literal button elements use the native generic Element instancer; input
type=button uses ElementFormControlInput and its native disabled/value authority.
The shared source kind table adds input-button through the existing atomic
insert/attribute candidate owner, without replacing the generic button kind.
The same kind enum reaches native Elements and source MCP creation.

ui_control_access classifies buttons and reuses its walker with an independent
64-button scan including native non-DOM nodes. Existing text/form counters are
unchanged. UiPresenter::buttons reports native scope/ID/visibility/focus/pressable
facts, nullable enabled for generic buttons, admitted native input values and
bounded generic text through the existing raw text helper. Nested form values
are refused. This read never dispatches, focuses, updates or writes.

press_button reuses document/ID lookup, held-input/output routing and weak native
observers. Identity/kind/eligibility are checked after blur and focus before one
native Click. Buttons have no desired-state no-op: each fresh request can dispatch,
while existing Play mutation receipts provide exactly-once replay. Delivery is not
a gameplay acknowledgement. Native command queues, callbacks and their documented
ownership limits remain authoritative. RuntimeUiHost/Main/MCP providers share
these methods and the existing gameplay input release. The new buttons/press
metadata generate MCP bindings; no second command or widget state is maintained.

Non-text input classification uses the native case-sensitive lowercase type
values. Unsupported uppercase BUTTON/CHECKBOX/RADIO inputs use the native text
fallback and cannot authorize button/form actions. Focused tests verify native
text classes and rejection. The existing conservative password field redaction
remains unchanged. Semantic press delegates Click without moving the pointer or
inventing mouse coordinates; source/Scene state and native command queues retain
their existing owners.

## Native slider access

The shared source kind table adds range (Slider) through the existing source
insert/attribute/history owner. UiPresenter::range_controls and set_range_value
reuse ui_control_access, its bounded non-DOM walker, current-control lookup,
field routing, native focus and call-local observers. Range discovery has an
independent 64-control count and the existing 4096-node limit. No second widget
state, slider math, event generator or source writer is introduced.

At the pinned native revision, ElementFormControlInput::GetValue for range reads
the widget and formats it with six decimal places. Raw value attributes can
differ. Native SetValue owns quantization, clamping and Change. A fresh repeated
out-of-range request can emit Change without changing returned value text.
Consequently every fresh semantic request calls the native setter once; only the
existing mutation receipt suppresses replay. delivered reports invocation;
value_text_changed is a nullable comparison of native formatted text, not private
float precision or gameplay acknowledgement. Detachment/kind retirement prevents
value confirmation. Native callback lifetime limits above still apply.

A private standard-library helper admits finite request/min/max magnitudes up to
1000000 and step from 0.000001 to 1000000. These bounds keep the native float
quantization intermediates representable. Public native Variant conversion reads
attributes using native float semantics; no private widget access is used.
Unsupported configurations remain observable with parameters_available:false,
parameters:null and editable:false. Native equal/reversed bounds remain native;
zero/negative/tiny steps cannot authorize semantic setters. This is an action
admission profile, not a new source format or claim of all possible native ranges.

The Play read/range_value providers, RuntimeUiHost and MCP use these same methods,
with existing input release, execute permissions, revision guards and receipts.
Source creation supplies a finite initial numeric value (default 0); min/max/step
attributes retain their existing source authoring owner. No SDK, dependency,
serialized schema or runtime capability profile changes are required.

## Non-text native keyboard access

UiPresenter::widget_keyboard_action reuses current_form/current_button/
current_range and their independent bounded walkers. Supported kind metadata
contains checkbox/radio/select/button/input-button/range. Focus and paired-key
operations reuse field_document/field_route/native input/focus/observers, with
post-focus pointer/document/context/ID/type/config admission. Keys require the
exact target already to have focus. Progress and text/password/textarea are
outside these operations; existing text-field access remains supported.

The existing private key-to-native mapping is extracted once and shared; text
key names and nullable navigation consumption remain unchanged. A separate
13-name widget menu adds Space/Escape without widening text schemas. Native
ProcessKeyDown/Up and ElementDocument default actions own Tab/Shift-Tab, event
cancellation and activation. No synthetic Click, desired-state no-op or focus
order is supplied. Generic buttons with native tab-index:none do not activate
for Space/Return; tab-index:auto enables the native behavior. Input buttons
retain their native default. Widget range keyboard admission uses the same
numeric profile as semantic values. Call-local observers do not keep objects
alive; the callback retention/deferral and attached-focus rules above apply.

Fresh results reuse the corresponding family observations with delivered,
nullable consumed and requested_kind. Consumption never confirms a changed
value or gameplay command. Native Tab can move to a different family; subsequent
keys require the newly exact focused target. focusable/tab_stop expose cached
native style facts, separately from visibility/enabled/admission. Existing
Play grants/scope/revision/receipts and input release generate widget_focus/
widget_key MCP bindings through the same providers; no new ownership layer.

## Source control properties

UiMarkupControlProperties is a derived, stateless source view, distinct from
native runtime observations. The source kind table and property metadata own
labels/types/defaults/choices and generate the source MCP value field names.
Numbers use the shared finite native-float envelope; flags model attribute
presence, including present checked="false". Progress source20/max0 remains20/0;
only the native runtime clamps/falls back. Generic buttons do not gain native
disabled/value fields. Options require a select parent, matching source creation.

The reader borrows the current inspected source outline. The setter composes
existing escaped attribute/inline-style candidates privately; one replace_selected
publication retains the element and one widget Undo. Numeric no-ops preserve
existing spelling, quotes and interstitial bytes. Null resets, bindings/duplicate
source refusals, explicit literal repair and source/resource ID ownership remain
explicit. No native DOM serialization, normalization, widget state or writer is
introduced. SourceEditor caches this view/staging on source/selection refresh,
not per frame. Apply/Discard/Reset affect staging until publication; Save remains
explicit. Tool bindings reuse source guards/grants/receipts and the same owner.

Text source profiles use the same metadata/reader/candidate: text-input and
password-input expose initial value/placeholder/maxlength/size; textarea exposes
placeholder/maxlength/rows/cols/wrap without replacing its body; label exposes for
without replacing captions or IDs. Query rows additionally report optional
minimum/maximum and integer. The same metadata validates source and typed patches.
Strict decimal integer source parsing refuses fractional/exponent/overflow forms
rather than accepting a rounded native float; explicit replacements can repair
plain literals. Length is -1..1000000, dimensions1..4096. Existing range native-float
admission/step bounds are retained through that metadata. Source bindings and
ambiguous attributes still refuse, without changing native password observations,
UI history, runtime state or source text normalization. Native widgets alone apply
length restrictions and label focus; textarea body/attribute precedence remains
with the exact native parser and existing Text/source controls.

## Literal typography styling

UiStyleLiteralProfile borrows immutable keyword/unit spans for optional source
input controls. The shared36Common names/profile back inline/group/rule staging
and the read-only style_profile query. Existing color/number formatters and native
source writers remain authoritative; the helper is not a CSS parser or cascade.
Ten existing numeric helpers keep all four units and their prior finite envelope.
Typography keywords follow the exact native registration. New numeric helpers
admit whole unitless weights1..1000, nonnegative line height in supported units,
and px/dp letter spacing. Unsupported/mixed/custom CSS stays in the old text field;
opening/reading controls makes no normalization or source change. Set/Remove,
selected-root/group history, explicit Save and current source guards retain their
owners. Existing style_read/rule_read JSON results are unchanged.

Choosing bold/italic requires a matching loaded face. The native source-hidden
fixture imports actual Lato BoldItalic font data from the unchanged dependency
pin through a project-relative @font-face, without altering fallback or aliasing a
regular font. Native parsing/rendering checks all adopted style values; no physical
typography/Windows acceptance is implied. See dependency provenance and the user
source guide for controls, units, custom values and font prerequisites.

## Image source properties

Image adds src/sprite/rect and float width/height to the same source property
metadata/candidate owner; it acquires no form disabled flag. Missing dimensions
carry no invented native value: absent numeric defaults remain null/unavailable
but editable, with a native-resource/cascade reason. Native positive dimensions,
sprite precedence, crop/layout/density and CSS overrides are not reimplemented.
The rect literal helper only validates four finite source numbers and size at least0.000001
through the existing scalar admission. Image attributes acceptzero orpositive
values at least0.000001; optionalminimum_positive metadata drives shared validation
and hints. This bounds native float aspect calculations without copying them. The native rectangle value type validates distinguishable crop edges at float
precision; UV generation and layout stay in the renderer.
Data bindings/duplicates/source/current guards and explicit reset/Save apply as
for other profiles. image-color reuses the original color literal/staging owner.

## Responsive source layout controls

The existing immutable UiStyleLiteralProfile also supplies display/flex/alignment
presets and bounded growth, basis, row/column gap and min/max dimension inputs.
There are36 Common properties. Grow/shrink use nonnegative unitless factors;
basis/gaps/size limits use nonnegative px/dp/% values up to65536, with native auto
or none presets where applicable. These helpers stage the original text field;
raw shorthand/custom values retain the same source writer/admission. No native
layout math, resource owner or preview geometry eligibility is replaced.
