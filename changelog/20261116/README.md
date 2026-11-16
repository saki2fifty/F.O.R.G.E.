# 2026-11-16

## Directional 2D sensor overlaps

Static/Kinematic Box and full-cell/custom Tile Map sensors can use the existing
one-way controls to admit overlap events from their front original boundaries.
Sensors remain non-solid; native pair continuity, epochs and recovery retain
their existing owners. Character movement and rigid-body point velocity share
the current motion owner when evaluating approach. Scene commands, Inspector,
MCP and matching SDK observations use the same physics implementation. The
editable directional-sensor Gallery scene compares front/back passage and is
included with the packaged Gallery.

## Camera Shot splitting and duplication

Timeline can split a shot at the playhead or copy its clip values into a complete
free interval. The existing Scene command owner publishes one Undo step, preserves
camera/curve/source intent and rejects invalid timing or incompatible transitions.
Automation/MCP exposes the same commands with normal scope, permission and replay
guards. Source candidate checks are shared with the worldless command draft.

## Selected C++ source formatting

Code assistance can prepare formatting for the current source selection. Native
controls and automation/MCP share the existing finite analysis task, reviewed
complete-file proposal, caret mapping and one source Undo. UTF-8 range and provider
capability checks preserve the draft on refusal. Whole-file formatting, Save and
Build remain available through their existing workflows.

## Direct camera-shot timing edits

Timeline offers explicit shot timing editing with body moves, edge trims and
millisecond snapping. A transient preview commits one Scene Undo step on valid
release; invalid bounds, overlaps or blend lengths leave source unchanged.
Escape, stale edits, focus/selection/view changes, Play, Save and Close cancel
unfinished gestures. Chronological rows preserve stored order. Numeric fields,
gestures and MCP use the existing multi-field Scene command; normal scrubbing
remains available with timing editing off.

## Camera-shot timing profiles

Incoming shots select Smooth, Linear, EaseIn or EaseOut timing through the existing
copied view/lens projection. Smooth preserves prior timing. Timeline duration and
curve edits share one Scene transaction; live tuning and Keep runtime values use
the same typed owner and reflected controls/MCP. Private recovery16 retains exact
nondefault curves and the prior nested profile; default checkpoints keep their
existing shapes. The public component header requires a matching SDK rebuild.
The editable Camera transition curves Gallery scene compares the four profiles
without a project C++ module.

## Active-file C++ find and replacement

C++ Sources provides literal case-sensitive Previous/Next with wrap and a hidden
inline replacement panel. Replace next uses the actual matching selection or the
next match; Replace all handles non-overlapping matches and follows the live caret.
Both use one existing source Undo step, preserve saved files and Scene state, and
check text/match/output limits before mutation. Native controls and guarded MCP
operations share the source owner without compiler/provider requirements. Pinned
widget selection observations are staged in the build tree; the original checkout,
revision, class layout, SDK and runtime ABI remain unchanged.

## C++ diagnostic navigation

Next diagnostic and Previous diagnostic navigate retained Check source locations
relative to the live cursor, wrapping and grouping equal positions. From another
tab they open the first/last location in the checked source. Native controls and
MCP share existing result/source/context guards and Back/Forward; text, source
Undo and Save remain separate. No additional provider query or navigation state
is created.

## Reviewed C++ diagnostic quick fixes

Quick fixes requests suggestions at the source cursor using diagnostics for the
current draft version. Available actions show a complete read-only source proposal
and apply through the existing current-caret/source Undo path. Unavailable actions
retain a reason; commands, deferred actions, cross-file/resource/annotated edits
remain refused. Native controls and MCP share the same bounded task and guards.
Private edit mapping is indexed by proposal, supporting multiple alternatives
without another source or history owner. Save and Build remain separate.

## Portable UI source acceptance

UI source save assertions explicitly decode UTF-8 instead of using the host's
default encoding. Unicode binding labels retain the same expected contents and
the existing CRLF/no-op Save byte checks remain in place.

## C++ call signatures

C++ Sources includes read-only Signature help through the optional supported
provider and existing managed compiler context. It lists bounded overloads,
plain-text documentation and active parameter labels, including Unicode names.
Current drafts are analyzed without changing source, cursor, Save or Undo.
Compiler context retirement marks existing information stale. MCP exposes the same
query and bounded status metadata; signature results reject source application.
The existing provider pin, SDK, runtime, persistence and ABI are unchanged.

## C++ draft diagnostics

Check source reports versioned diagnostics for the active draft using the existing
optional provider, managed compiler context and unsaved headers. Results include
receive date/time, severity, message and guarded source navigation. Empty completed
publications are distinct from absent or malformed data. Native controls and MCP
share the same bounded task and source/context guards; navigation adds no source
Undo or Save. Compiler output and Problems retain their existing owners.

## C++ symbol inspection

Symbol info returns bounded plain-text type/documentation at the current cursor.
Symbols in file lists nested declarations and navigates through the existing
source guards. Both workflows share the finite provider task, managed compiler
context and current drafts. The outline reuses the existing AST-ready response;
shared source-location normalization keeps declaration/diagnostic/outline behavior
consistent. Read-only information and navigation preserve source Undo and Save.
Native controls wrap at narrow widths; MCP exposes the same query kinds and pages.

## C++ source navigation

Back/Forward retains up to 128 explicit source locations across file opens, tabs,
Find next and analysis/search jumps. Closed files reopen through the source owner;
edited targets clamp old locations without restoring text. Clear navigation,
project retirement, stale revisions and failed targets preserve the existing
draft/Undo/save ownership. MCP exposes the same read/move/clear workflows with
permissions, active-tab witnesses and ordinary mutation receipts. Code assistance
also identifies the queried file while another tab is active.

A source-owner regression exposed incorrect tab insertion endpoints in the pinned
text widget's Paste history. A signature-checked build-tree repair preserves the
immutable checkout and MIT notice while correcting exact single-step Undo/Redo
for C++ and UI source. No dependency version, runtime, SDK, ABI or saved format
changes. Actual widget tests cover partial replacements, Unicode and repeated
Undo/Redo.

## C++ reference and type navigation

Find references lists provider-reported usages in the current compiler context,
excluding declarations. Go to type navigates from a variable to an available
Native type definition. Both share the finite provider/task and source-location
owner, guarded native/MCP navigation, Back/Forward and source Undo/Save boundaries.
Reference scope is explicitly non-exhaustive; the finite provider has background
indexing disabled. Shared supported-kind metadata keeps task/worker/MCP discovery
aligned. Empty, malformed, unavailable, external and limited results retain their
distinct outcomes. Existing six assistance modes remain supported.
Location links require their selection to fit the enclosing target range. Source
jumps collapse earlier selections while project search still selects its new
match, preserving text, caret and source Undo ownership.

## Reviewed C++ source formatting

Format source queries the current draft, shows a read-only whole-source proposal
and applies only on request through the existing one-record source Undo path.
The current caret follows validated edits. Native controls and MCP share bounded
complete-candidate admission, LF/final-newline handling, permissions, stale checks
and receipts. Invalid, overlapping, ambiguous or over-budget sets fail without
partial formatting. Source-position decoding retains its existing public path and
uses a transient line-start table for many formatting ranges. Private cursor edits
stay with the task; MCP exposes the complete proposal and edit count. Style comes
from the pinned provider's style files/fallback, without new preferences or pins.

## C++ definition and implementation navigation

Find definition opens an available Native function body, with declaration fallback
when a body is unresolved. Find implementations lists overrides in the current
compiler context with explicit non-exhaustive scope. Native controls and MCP reuse
the existing finite provider/task, location validation and guarded Back/Forward
navigation. Querying preserves cursor/draft/history; explicit navigation preserves
source Undo/Redo and saved files. All existing assistance modes remain supported.

## C++ syntax selection

Select syntax lists bounded containing source ranges and selects one in the
actual source widget, with its caret at the range end. The existing selection/
source/history owner serves native controls, project search, collapsed positioning
and MCP. Borrowed status reports selection active/bytes/digest; source bytes,
Undo/Redo and Save stay separate. The implicit finalLF is excluded from selectable
extents. No selection manager, persisted state or dependency change is introduced.

## Reviewed C++ rename in file

Rename in file previews a complete current-source rename and applies only on
request through the existing source Undo/caret path. Native name entry and MCP
new_name use the same task/provider/admission. Extra-file/resource/annotated or
command replies fail without partial edits. Formatting and rename share complete
source-edit validation and private mapping data; source tab consumers retain their
existing lightweight dependencies. Save and Build remain separate. Current-context
scope is explicit; project-wide refactoring remains unavailable.
