# 2026-11-10

## Image source properties and tint

The shared source panel and MCP properties expose image sources, sprite names,
crops and size attributes without guessing unloaded intrinsic dimensions.
image-color uses the existing color control. Native resource registration, sprite
precedence, aspect sizing, CSS overrides and cooking retain their owners; an
applied source batch stays one Undo with explicit Save.

## Source typography literal controls

Inline/group and stylesheet controls share keyword presets for text weight/style,
alignment, decoration, case and wrapping, with numeric line/letter spacing helpers.
A bounded immutable metadata profile also powers read-only source MCP discovery.
Raw CSS and old source mutations/results remain supported; presets stage the value
before one Set/Undo and explicit Save. Font loading and native styling keep their
existing owners.

## Text control source settings

The shared source property panel and typed automation expose text/password initial
values, placeholders, bounded integer length/dimensions, textarea wrapping and
label targets. Metadata reports numeric constraints. Source integer parsing keeps
unsupported spellings explicit; a complete patch remains one Undo with explicit
Save. Textarea body/label markup and live password restrictions keep their owners.

## Authored control properties

Elements adds staged typed properties for existing form, button and range controls,
using the same source view/candidate helper as two source automation/MCP methods.
Flags follow native attribute presence; numbers preserve source/native-float
semantics and equal-value spelling. Bindings, duplicates and unsupported forms
remain explicit. Apply publishes one selected-root Undo transaction, with Save,
resource refresh and runtime normalization retaining their owners. The panel
separates source defaults, inline overrides, staged edits and reset intent.

## Native widget keyboard interaction

Play and MCP expose scoped native focus and paired keys for checkbox/radio/select,
buttons and sliders. Existing family observations expose native focus/tab-stop
facts, while text fields retain their existing tools and key schema. Native focus
order, cancellation and default actions remain authoritative; generic buttons
need native tab-index:auto for keyboard activation. Current identity/type/config,
input/surface routing, grants, revisions and receipt replay share existing owners.
One extracted native-key mapping serves text and non-text consumers. The manual
covers supported keys, focus changes, button styles and saving limits.

## Native slider creation and values

Elements and source MCP share a Slider kind with finite numeric initialization,
atomic source history and explicit Save. Independent range observations and a
semantic native value action share existing presenter/input/focus/Play owners.
Native GetValue/SetValue retain quantization, bounds and events; observations use
native formatted text rather than the potentially different value attribute.
Fresh out-of-range requests retain native repeated-change behavior while mutation
receipts prevent duplicate replay. Numeric admission and post-focus identity/type
checks bound supported actions without another widget state or event generator.
The manual covers creation, bindings, agent input, saving and current limits.

## Native button creation and activation

Elements and source MCP share an additional Input button kind with a literal
value caption and native disabled state. The existing Button kind remains.
Independent bounded native button observations and semantic press use the
existing presenter/input/focus/Play owners; fresh presses can activate again and
receipt replay never activates twice. Read state distinguishes generic focus
facts from native input enabled state and excludes nested field/password values.
Non-text type matching follows the case-sensitive native pin. Native, source-
hidden command/ACK, normal/SDK MCP tick/replay and source-editor creation/history
checks cover the workflow. The manual includes an authentic Input button staging
capture. Gallery identities/bytes, SDK, pins, formats and runtime profiles remain
unchanged; full Main/Windows/physical review remains queued.

## Native form observations and actions

Editor Play exposes native checkbox/radio/dropdown state and read-only progress
through the existing presenter and five automation/MCP operations. Dropdown pages
carry observed option state; semantic actions retain Play/presentation/focus/input
guards, native events, radio groups, eventless no-ops and receipt replay. Native
Update owns option normalization; observations do not focus controls or dispatch
events. Shared control access preserves text/password/textarea behavior and secret
redaction without a parallel runtime state store. Exact-pin native Focus/Change
callback destruction limits are recorded with independent diagnostics; native
callers retain ownership through dispatch and use attached focus before release. Gallery 212 demonstrates the
controls, labels, groups, disabled options and transient defaults. The manual
includes native screenshots and agent/saving/export steps. SDK fingerprints,
dependency pins and runtime editing/shaping profiles remain unchanged. Local
native/MCP/software-render checks are separate from queued Windows/full-editor
and physical-device acceptance.

## Common UI control creation

Elements offers16common control kinds and initial text/value through one atomic
creation operation. Existing insertion/attribute/text candidate owners supply
escaping, input types, selected dropdown options and full source validation before
one Undo; the new root becomes selected. Dropdown/option and non-container limits
are explicit, IDs/label targets remain authored, and generic source access/Save
stay supported. The same operation and kind enum serve automation/MCP, bringing
UI source tools to33. Specialized Play field APIs retain their text/password/
textarea scope. Native, protocol and saved/source-hidden controls tests cover the
profile; the manual describes creation and interaction prerequisites. No Gallery,
SDK, dependency pins or runtime profiles change; Windows/full-editor/physical
review remains queued.

## Authored UI color and numeric controls

Single-widget, sibling-group and flat-rule properties share compact RGBA hex
color and numeric/unit controls. These stage CSS text until Set property publishes
one source Undo; mixed, absent and unsupported values remain explicit. Literal
adapters use standard character conversion, preserve untouched source spelling,
and keep full candidate/property admission authoritative. Existing32source MCP
operations already cover the same raw values, guards, reads and history without
duplicate UI state. Focused native, protocol and saved/source-hidden runtime checks
cover alpha order and unitless/px/dp/% values. The manual documents staging,
replacement defaults, limits and unit semantics. No SDK, pins, runtime profiles
or Gallery content changes; full Windows/editor/physical review remains queued.
