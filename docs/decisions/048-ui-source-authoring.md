# ADR 048 — Native UI authoring keeps one source draft

Date: 2026-11-08. Extends the central document contract in ADR004 and the borrowed
automation ownership in ADR025.

## Decision

RML/RCSS can be edited in one native UiSourceEditor registered with
DocumentWorkspace. The existing TextEditor owns its only draft and Undo history.
Source-derived ranges support bounded attribute, leaf-text, child insertion,
removal and sibling movement. Extend the existing FORGE markup admission parser
to collect optional ranges; do not introduce a second parser or serialize a
rendered DOM. Raw Source accepts incomplete drafts; structured candidates and
saved bytes pass the existing complete-source validation.

ProjectTextSource extracts the already supported contained raw-file admission,
disk baseline and atomic replacement from ProjectSourceAutomation. Shader
declaration identity checks remain in the shader wrapper. A native-owned UI file
rejects raw automation writes until that native document closes. Both native and
MCP edits use this same owner and transient session/revision/digest guards.

The small `forge_ui_source_authoring` library is created only with the existing
optional UI presentation profile. Its pinned Core escaping dependency does not
propagate into headless runtime, catalog or package targets. Source range
inspection and raw-file storage remain available without the presentation stack.

The C++ editor retains its paths, source management and build semantics. Its
existing single-record TextEditor replacement helper is shared with UI source;
temporary clipboard callbacks are restored synchronously without OS clipboard
reads or writes. There is no second editor framework or history manager.

## Boundaries and compatibility

Save is a validated, baseline-checked file write, outside Scene history. It does
not register resources, publish a catalog or reload the runtime. Clean file moves
track the existing Content file owner; dirty drafts block those operations.
Unsaved drafts also participate in the existing project/Play/export/close guards.
The source editor is read-only while Play or its owning task blocks editing.

Existing closed-document raw source tooling retains its external-style semantics.
SourceDocumentAccess may carry file/publication hooks without a typed JSON invoke
callback; typed source discovery excludes those hook-only entries. UI source has
its own borrowed automation methods, rather than pretending RML is a typed source
JSON document.

Structured text escapes markup through the pinned RmlUi Core API. RML model
bindings and attribute expressions retain ordinary source semantics. This is
source authoring, not a literal runtime text-node API. Runtime UiDocument,
UiRuntime, UiPresenter, resource inspection and package closure remain unchanged.
No scene format, asset identity/type, private UI protocol, SDK layout or runtime
kit capability changes. Unrestricted visual layout, rich text shaping and
accessibility remain separate capabilities.

## Native draft preview and pixel layout extension

2026-11-09: The same source document additionally owns a Preview view and bounded
absolute pixel layout gestures. Source, Elements, numeric layout and canvas edits
share the one TextEditor draft/history. Optional inline declaration ranges extend
the existing admission scanner; four-value replacements preserve unrelated bytes.
The native DOM remains a read-only layout and hit-test observation, never source.

UiResources seeds one matching registered RML draft in its existing immutable
per-candidate map, sharing ordinary admission and budgets. It requires a fresh
editable-project candidate; saved dependencies retain their normal read policy.
There is no package-manifest bypass, extra authoritative resource map, implicit
registration or publication of draft metadata.

RuntimeUiHost exclusively lends its existing presenter/backend to source preview
while Play is stopped. Source preview omits gameplay model attachment and OS text,
clipboard and IME callbacks. Runtime/staged/project/close handover retires it through
the same lifetime owner. UiDiligentRenderer owns bounded opaque image allocation;
retained views keep last-good pixels alive through native/backend retirement and
ImGui draw-list consumption. Preview observations are withheld from runtime asset
publication and gameplay input/field/command access.

Unique authored IDs bind native geometry to current source ranges. Eligibility
combines native layout with the source's plain inline absolute-pixel subset;
unsupported transforms, bindings, constraints and relative values remain source
edits. Gestures retain transient candidates only, commit once on release and
cancel on owner/source/lock/close boundaries. Invalid candidates keep last-good
pixels but cannot authorize edits. Agents borrow the same document and recheck
source guards plus native preview instance/revision before atomic layout writes.
No persisted layout database, parallel selection/history, Scene identity, SDK
layout, dependency pin or runtime protocol change is introduced. Unrestricted
visual design remains a separate capability.

Bounded same-parent groups extend this same source selection and gesture owner.
At most32 source elements share one atomic pixel replacement, native candidate
validation and TextEditor Undo record. Native border-box observations supply
movement/alignment/distribution geometry; source sizes and unrelated bytes remain
unchanged. The single-element path delegates to the batch source-range owner.
Selection is one transient ordered set whose last entry is active; source changes
clear it. Group moves retain one transient candidate and the existing cancellation
boundaries. Matching width/height/both extends this same group candidate owner:
the last member supplies native border dimensions, source sizes change by native
size deltas and the complete native result must attain those dimensions without
moving members. Shared action metadata supplies native controls and automation.
No new selection manager, layout solver, persisted data, runtime capability or
gameplay SDK layout is introduced. General group scaling/resize gestures and
relative/flow layouts remain outside this bounded profile.

Grid alignment extends the same action/geometry owner for1..32admitted siblings.
Native border corners round through standard numeric operations to a bounded
viewport grid; the source owner converts those deltas into existing pixel literals.
An optional expected native-rectangle span validates grid and matching candidates
without another layout authority. The existing Preview deferred request evolves
to one guarded envelope with placement or keyboard-layout payloads. Both dispatch
before the next image submission and share cancellation/source history. Native
canvas focus and shortcut routing protect fields, Source text and other panels.
Single layout and group movement already expose equivalent programmatic nudges.

Mouse drag projections move to the existing source geometry owner, shared with
the guarded source transform operation. Observed/expected native rectangles reuse
the existing layout-box type. Optional grid anchoring, dominant-axis movement and
single visible-aspect resizing preserve the established source bounds and transient
gesture/history ownership. One native candidate validator serves gestures and
atomic layouts. A gesture retains its newly borrowed frame before validation can
refuse it, preserving image handover; projection validity and native candidate
validity remain distinct so returning from an invalid projection can recover the
last good candidate. Grid spacing has one Preview control, with no persisted
settings or parallel coordinate authority. General group scaling remains outside
the profile.

Rectangle and programmatic selection use the existing ordered source selection
owner and 32-member limit. The native observation producer reports visibility and
ID uniqueness from its established native traversal. Rectangle selection fully
encloses visible, unambiguously source-bound border boxes, sorted in source order;
additive selection retains old order and appends new members. Source-only index
selection needs source guards, while rectangles also reborrow native preview
guards. Neither creates source history or persisted selection. The explicit Box
select mode preserves default layout dragging and excludes placement mode. Its
transient rectangle cancels at existing source/view/focus/lock/close boundaries;
release dispatches through the existing deferred Preview envelope before image
submission, preserving preview image lifetime. No DOM writer or second selection
manager is introduced.

Focused canvas selection and structural shortcuts extend that same deferred
Preview envelope. Select-all uses the existing native rectangle selection;
clear uses the existing ordered source selection. Duplicate calls the existing
single-subtree owner and delete calls atomic sibling removal. Both retain their
source admission, ID rules, history and Save behavior. Source/session guards and
existing cancellation run before pre-image dispatch. Cancellation of a pending
edit or gesture takes priority over Escape's idle selection-clear behavior.
Native item focus and shortcut routing protect Source, fields and other panels.
Existing guarded MCP selection/duplicate/remove operations already expose these
authored results; no agent-specific editing implementation or input injection
is introduced.

Preview image navigation belongs to the existing source editor presentation.
Fit remains the default; actual-pixel and bounded manual zoom use a native clipped
child canvas with standard scrolling. Display scale/origin feed the existing
pointer conversion; native document dimensions, source coordinates and presenter
images remain authoritative. View controls/scroll navigation are disabled during
source gestures. Canvas rendering/input is a private method in the same owner,
letting the parent balance the child scope across every canvas early return.
Full-image exports and existing automation/MCP geometry/editing contracts remain
independent of the display crop; no extra layout or agent-authoring manager is
introduced.

Static subtree duplication and sibling-group removal extend these same source
range, selection and history owners. Duplication retains original source/IDs and
assigns collision-checked prefix IDs only to the new raw fragment. Literal internal
label targets remap; unknown references, selectors and external targets are not
rewritten. Unsupported bound/dynamic/ambiguous input rejects. Removal admits at
most32distinct unprotected same-parent roots, validates disjoint ranges and the
complete result, then replaces the draft once. Both native controls and borrowed
automation use these helpers. No persisted source identity, native DOM writer,
clipboard protocol, schema/SDK change or separate history is introduced.

Bounded static reparenting also uses these source ranges and admission owners.
At most32same-parent roots move in original source order, keeping their bytes/IDs;
an existing container supplies an append or direct-child insertion position.
Complete cycle, scope, membership and candidate checks precede the one draft
replacement. Outside comments/text remain at their original ranges. CSS and
parent-relative coordinates are not rewritten. Native Elements and borrowed
automation/MCP use the same source transaction; exact no-ops create no history.
This extends source editing, not the Scene hierarchy or a second native DOM owner.

Inline CSS authoring extends that same optional admission collector with full
declaration ranges. Plain style/property extraction also serves pixel layout;
literal set/remove validates the complete candidate and replaces the source once.
An inline declaration cannot change element ordering, so changed styles can safely
restore the edited transient index. Exact no-ops retain revision/history. Existing
bindings and Scene model values keep their owners. Native outline drag/drop uses
the existing reparent operation with bounded immutable source/interaction guards,
gesture cancellation/locks and post-traversal publication. Neither workflow adds
a parser, style/DOM database, identity service or native serialization boundary.

Stylesheet Rules extends the existing optional admission scanner with top-level
selector/content/source ranges and flat-block eligibility. The runtime path has
no rule collector or authoring cap. Flat RCSS property edits share declaration
matching/value validation/patch ranges with inline CSS; attribute quoting remains
an inline-only restriction. Selector editing and rule insertion/removal remain
source replacements under the same draft/history/guards. Other at-rules/nested
blocks stay Source; literal top-level font-face declarations use the same scanner
and value patches for guided read/set/remove, with no general at-rule editor; native DOM parsing or serialization supplies no authoring identity.

Sibling style/class operations borrow the existing sibling admission and source
patch composer, sharing value/attribute validation and one complete replacement.
Read values remain ordered optional authored values, with explicit mixed feedback
in native/MCP consumers. Selection is copied before replacement can clear it.
Literal class assignment retains binding/ID ownership and existing equal-value
encoding; flat rule copy/order remains source manipulation under the same owner.
No extracted theme, second stylesheet parser or persistent source identity follows.

Common control creation has one kind metadata table beside the existing source
authoring owner. It composes detached generic insert/attribute/text candidates,
including input types and an initial selected dropdown option, before one document
replacement. Existing escaping/full candidate admission remain authoritative;
the result carries only the new snapshot's transient root index for selection.
Native creation and its control automation/MCP method share that function, kind
enum, guards/history and explicit Save. No persistent widget identity, native DOM
serialization, second template tree or runtime field-state owner is introduced.

Literal color/number controls are optional authored-value adapters in
ui_markup_authoring, with no native initialization or computed-style reads.
The color subset is six/eight hex digits with alpha last; normalized RGB channels
are authored color bytes, not linear-light material colors. Scalar adapters accept
finite magnitudes up to65536 and unitless/px/dp/% suffixes. Standard character
conversion formats round-trippable numbers; edited colors round to eight-bit
channels. These adapters do not replace complete candidate admission, property
limits, stylesheet parsing or the separate absolute-pixel layout eligibility.
The native controls stage only their existing CSS text field and preserve mixed,
absent and unsupported values until an explicit replacement. Existing single,
group and flat-rule property operations publish the resulting text through the
same guarded source replacement/history. Automation/MCP use those same operations
and optional authored reads; no control-state API or additional writer is needed.

Source-preview image output borrows the same observed native image through
ViewImageTask. A rendered input retains the source/preview identity and rechecks
it before the existing nonblocking GPU copy is submitted. Pending observations
cannot silently switch drafts; after submission the queued copy owns its pixels
independently. Source Preview's Save preview image and the capture.ui_source
operation reuse output grants, file conflicts, encoding, status and bounded PNG
projection. No second capture owner, source history or gameplay execution is added.


Image selection borrows the current immutable Content catalog. The existing asset
picker/drop filters standalone project UI image sources and stages only `src` in
the existing control-property map. Catalog/source identity and UiResources own
reference translation and current image admission; their same read-only helper
backs the guarded assets.read image-reference query. Apply/Undo/Save and sprite
precedence remain with their existing owners. Literal editing remains available.


Guided font declarations borrow current project font admission/face metadata from
UiResources and the existing catalog. A private shared source-info helper serves
font/image references; recorded names remain independent of authored aliases.
Native font controls reuse the existing face-details drawer through a const JSON
overload, with no metadata copy or second UI data owner. Font source paths keep
the existing project-relative convention. RCSS font-face changes and their MCP
bindings share one source replacement/history, guards, grants, explicit Save and
source range ownership. Generic flat-rule operations retain their at-rule barriers.
The Add rule event returns after publication before new range observations.

Font-face weight/style edits reuse ordinary literal profiles and existing
declaration value patches. The native automatic weight keyword is scoped to font
faces; ordinary text weight controls retain their existing range. Optional MCP
arguments preserve values when omitted and remove overrides on null. Appearance
and alias/resource/index compose before one source publication and share staged
native controls, validation, Undo and explicit Save. No additional font registry
or native font cache owns an authoring copy.

The fixed native fallback declaration shares that same closed property whitelist,
private value patch, staged choices and optional guarded MCP writer. It is not
admitted as a general CSS identifier. Default removal preserves nativefalse and
existing loaded-font fallback order; no source/runtime font registry is added.
