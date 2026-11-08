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
