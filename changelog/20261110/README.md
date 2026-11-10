# 2026-11-10

## Guided font fallback participation

The existing font declaration form stages fallback participation with explicit
false/true/default removal. The same guarded MCP writer accepts optional boolean
fallback or null reset. Fixed native property patches preserve the existing
font loading, resource, source history and Save owners.

## Font declaration weight and style

Guided font declarations stage weight and style alongside the existing alias,
resource and face index. Apply publishes one source edit; default reset removes
the override, and untouched source values retain their spelling. The existing
guarded MCP writer exposes matching optional values and null reset. Saved and
source-hidden project checks exercise styled faces under one alias in both text
modes.

## Project font declaration authoring

RCSS Rules adds font declaration creation, editing and removal through registered
project fonts and collection faces, with explicit family aliases, current-byte
admission, staged controls and one source Undo/Save. The existing face-details
table is shared with Source. Four matching guarded MCP operations include two
read-only queries and asset-write-gated mutations. Saved/source-hidden project
checks exercise an authored collection alias and its input-field assignment.

## UI image resource selection

UI source image properties reuse the Content asset picker and drop payload to
stage a validated document-relative source reference. Apply, Undo, Clear and
Save retain the existing source owner. A read-only guarded MCP query resolves
the same standalone PNG/JPEG/TGA resources for the existing property mutation.
Typed references and sprite precedence remain available. Resource loading preserves
the first useful path-origin diagnostic when a document lacks its native header.

## UI Preview image navigation

Preview adds fit, actual-pixel viewing, bounded image zoom and native canvas
scrolling. Picking and edits retain native viewport coordinates when zoomed or
scrolled. View navigation leaves source/history and full-image capture unchanged;
existing guarded MCP operations retain their coordinate contracts.

## UI canvas editing shortcuts

Focused Preview supports select-all, clear, single-subtree duplication and group
deletion through its existing selection/source owners. Structural requests share
the pre-image queue, validation, ID handling and one Undo with existing controls.
Text fields and other panels keep their input; cancellation retains the original
source and selection. Existing MCP tools expose the same authored results.

## UI rectangle and programmatic selection

Preview's Box select mode selects fully enclosed named visible controls, with
Ctrl-addition and atomic 32-member admission. Ordered source selection and native
rectangle selection are also available through guarded MCP tools. Selection does
not change source, Undo or Scene history; subsequent edits retain their existing
validation and Save behavior. The illustrated guide covers the workflow.

## Precise UI mouse layout gestures

Preview adds optional drag-grid snapping, Shift movement-axis constraint and Shift
visible-aspect resizing for one control. Grid spacing has one toolbar control;
default dragging remains available. The same source geometry and native candidate
validation serve the new guarded MCP transform operation, with one Undo and explicit Save.

## Precise UI canvas positioning

Preview can snap native border corners to a bounded pixel grid and move focused
selections with arrows or Shift-arrows. Layout edits retain one Undo and explicit
Save; keyboard requests share the deferred placement boundary and cancellation.
The existing guarded arrange operation exposes the same grid workflow to agents.
Native focus routing preserves text-field and other-panel arrow input.

## Match UI group dimensions

Preview group operations match visible width, height or both to the last-selected
control. Source CSS sizing accounts for each member's existing padding, borders
and box-sizing; native candidates must attain the target without moving members.
The shared arrange metadata exposes the same actions through MCP, with one source
Undo, unchanged-source refusal and explicit Save.

## Place UI controls in native Preview

Create controls offers named control placement inside an editable absolute-pixel
container. Native padding geometry supplies local coordinates, and the complete
candidate validates before one source Undo. Escape and source/view/owner changes
cancel queued placement. The matching guarded MCP operation uses the same owner;
the illustrated manual covers placement, supported layouts and explicit Save.

## Configured UI control creation

Add child and source MCP creation can assign a unique source ID and an explicit
absolute pixel layout in one Undo step. The default remains flow layout. Shared
ID/geometry/source validation preserves private candidates and explicit Save;
native Preview remains authoritative for canvas editing eligibility.

## UI box styling and property search

The shared source controls expose box sizing, position, overflow, side spacing
and simple borders. Common can be filtered by name through the native UI filter.
Source staging, one Undo, explicit Save and guarded MCP use the existing owners;
raw compound styles and native layout retain their behavior.

## Responsive UI source controls

Source styles offer flex direction, wrapping, alignment, gap, factor, basis and
size-limit controls through the existing native staging and guarded MCP owners.
Raw CSS, one Undo and explicit Save remain available; native layout owns reflow.

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
