# MCP game-authoring adapter

FORGE's native C++ MCP adapter is launched as `bin/forge_tools.exe --mcp-stdio`.
It connects to the explicitly enabled Local automation connection in the owning
editor. It neither opens another project writer nor creates a second scene.
The adapter is tools-only; exported games do not link it. No external MCP SDK or
new library is required. See the [user setup guide](../manual/editor/mcp.md).

## Protocol and provenance

Verified 2026-10-23 against the official specification at immutable revision
[`dae694f61a31f7a3ec2af3aff709e11f70f61e48`](https://github.com/modelcontextprotocol/modelcontextprotocol/tree/dae694f61a31f7a3ec2af3aff709e11f70f61e48).
FORGE implements the bounded protocol adapter itself; no SDK/reference-server
implementation was copied. The official SDK catalog provides no C++ SDK.
The existing pinned nlohmann/json and native socket APIs provide parsing and
local transport; their versions remain unchanged.

- **2026-07-28:** stateless per-request version and client-capability metadata,
  `server/discover`, `tools/list`, `tools/call`, complete results and
  server identity metadata. Unsupported modern versions return `-32022` with
  the requested and supported modern versions.
- **2025-11-25:** `initialize`, `notifications/initialized`, `ping` and the same
  tools, using the legacy response envelope. An unsupported initialization
  version negotiates 2025-11-25; the client must accept that version or disconnect.
- Only stdio is exposed to the MCP host. HTTP, remote access, subscriptions,
  resources, prompts, sampling and asynchronous task capabilities are not
  advertised. Other client protocol revisions are not claimed as tested.

The [exact current schema](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/dae694f61a31f7a3ec2af3aff709e11f70f61e48/schema/2026-07-28/schema.ts)
and matching legacy schema define wire fields. Protocol revisions are distinct
from Authoring API1, live transport1 and scene-format3. No existing scene or
gameplay ABI changes are introduced.

## Authority and connection lifecycle

The editor offers explicit read-only, scene-edit, and scene-plus-file-edit grants.
Read-only and scene-edit grants also expose current project/document status. Copy MCP
configuration copies an absolute tools executable path, `--mcp-stdio`, and
`FORGE_AUTOMATION_CONNECTION` in the MCP host's environment configuration. That
environment value contains the existing transport/host/port/token connection
JSON. FORGE does not write credentials into project files or preferences, and
the token is not passed on the command line or included in protocol results /
diagnostics. A host that saves pasted configuration will retain that token until
the user updates or removes its configuration; the editor can revoke it at once.

Only IPv4 `127.0.0.1`, port1..65535 and the existing 64-character token format are
accepted. The bridge has no shell, arbitrary filesystem, native-code execution
or engine-repository interface. Local connection authority is not a sandbox
against a compromised same-user account or a separately privileged agent.

Stop and editor exit revoke every grant. Scene-only grants also expire whenever
the document generation changes. The separately enabled file grant is scoped to
the currently leased project: it survives scene switches and filename changes
inside that project, creates a fresh scene routing session, and retains transport
sequence receipts. Switching projects or losing writer ownership revokes it.
Re-enable, copy configuration and reconnect after revocation. Ordinary Save keeps
all connections. This scope is checked before and after each editor pump.

## Tools and synchronization

`forge_discover` returns the current target, scene revision, `next_sequence`,
schemas, available commands, capabilities and mutation availability. The target
must be echoed exactly. `forge_scene_read`, `forge_entity_query` and
`forge_scene_diagnostics` expose committed authored data. Reads do not inspect
the live Play world or transient UI previews.

On editable connections, each command in the shared authoring catalog becomes
an MCP tool named `forge_` plus its operation ID with dots replaced by underscores.
For example, `entity.create` becomes `forge_entity_create`. Its `arguments`
schema and description come directly from the existing catalog. Stable target,
`expected_revision` and `sequence` are separate required arguments. New catalog
commands therefore acquire bindings without another hand-maintained command list.
`forge_scene_apply` groups 1..128 catalog commands into one atomic Undo entry;
`forge_scene_replace`, `forge_history_undo` and `forge_history_redo` preserve their
existing transaction semantics. Editor read-only grants list those four reads
plus `forge_project_read`; isolated scene-only test hosts retain the four reads.

### Document tools

The native owner registrations in `document_automation.hpp` supply method names,
capabilities, descriptions and argument schemas. Live transport and MCP derive
the workflow catalog from those registrations. `document.read` allows
`forge_project_read` to report project settings, relative scene path, generation,
dirty/persisted state and recovery availability. It does not read arbitrary files.

An explicit `document.write` grant adds:

| MCP tool | Owning behavior |
| --- | --- |
| `forge_document_save` | Save through `EditorFiles`/`SceneDocument`, checking the existing disk baseline |
| `forge_document_save_as` | Save to a project-relative JSON path; a saved-scene copy gets fresh scene/entity identities and cleared history, while first save retains its identities/history |
| `forge_document_new` | Create an untitled scene through the existing file owner |
| `forge_document_open` | Open and validate a project-relative scene through the existing file owner |

All four require exact scene target, observed revision and transport sequence.
New/Open reject dirty scenes unless the request explicitly sets `discard:true`.
Other dirty source drafts and pending file/recovery/build/export/dependency work
block switching; they are never silently discarded and no deferred UI action is
queued. Successful discard uses existing recovery cleanup; failed Open keeps the
old draft and recovery. Recovery restoration/deletion is not an MCP tool yet.

Paths use forward slashes, contain at most1024 UTF-8 bytes and stay inside the
current project's canonical boundary. Absolute paths, parent traversal, control
characters, alternate-stream syntax and redirected paths outside the project are
rejected. Existing scene-file/control-file restrictions remain in force. Save As
cannot overwrite another existing file; ordinary Save rejects external changes
or deletion. These are file commits, not scene Undo entries.

When a file operation returns `rediscover:true`, call `forge_discover` before the
next document operation. Previous scene targets stop working, even if a new scene
has the same UUID/revision. Replaying a retained old sequence still returns its
original receipt without repeating file writes. Editor scene activation consumes
the normal file-change event, including after filename changes.

Mutations require the caller's observed revision and a positive safe-integer
sequence. The bridge never silently replaces either. The editor owns sequence
allocation and its bounded replay receipts. Repeating the same authored request
with the same sequence returns its original result without reapplying it;
changed data conflicts and evicted sequences fail. After uncertain completion,
retrieve that same receipt or inspect state before deciding what to do next.
Scene edits remain unsaved editor edits and share normal Undo/Redo.

Tool results contain an identical JSON result in `structuredContent` and text,
including `ok`, applicable revision and `next_sequence`. Authoring/capability /
connection failures use `isError:true`; malformed MCP requests and unavailable
tool names use JSON-RPC errors. Busy editor activities retain existing denials.
The adapter has one dispatch thread and does not execute concurrent mutations.

## Asset workflows

**Start project authoring** adds project-scoped asset writes to the scene/file
grant. An optional explicitly selected import folder permits raw source reads
only beneath that root. Source copying still targets a new folder inside Assets.
Read-only connections can inspect the current asset catalog and operation status;
this does not grant file commits, placement or imports.

| Tool group | Existing owner and behavior |
| --- | --- |
| `forge_asset_search`, `forge_asset_read` | Borrow ContentImports' published AssetCatalog snapshot plus immutable engine assets; search pages contain at most128 records |
| `forge_asset_jobs` | Read actual reimport jobs and the current project-scoped source batch, placement and file-operation results |
| `forge_asset_reimport` | Queue registered owners through dependency-aware AssetReimportService; blocked drafts and last-good publication rules remain intact |
| `forge_asset_import_prepare`, `forge_asset_import_review` | SourceImport prepares the bounded source/dependency copy plan; review pages and its digest identify exactly what was inspected |
| `forge_asset_import_commit`, `forge_asset_import_cancel`, `forge_asset_import_close` | Confirm the exact batch/review, cancel pending work, or release a settled review; native workers publish each imported asset independently |
| `forge_asset_place`, `forge_asset_place_cancel` | SceneAssetDrop queues the same undoable Model/Mesh/Tile Map/Prefab placement used by viewport drops; Model options include source scene, animation clip and material variant |
| `forge_asset_files_prepare`, `forge_asset_files_review` | ContentFiles/AssetFileService prepare Move/Duplicate/Delete and known reference impact; discovered scene registration preserves its existing identity |
| `forge_asset_files_commit`, `forge_asset_files_cancel`, `forge_asset_files_close` | Confirm the exact operation/review or cancel/close its owner; Delete additionally requires `acknowledge:true`, and retained originals use existing recovery storage |

All mutations retain the complete scene target, observed revision and sequence.
Job identity is local to its owner; never apply an old batch/job ID to new work.
Queue admission is not completion. Inspect the returned owner progress/result;
failed candidates retain last-good content. Import and source-file commits are
not Scene Undo entries. A duplicated imported source needs its own valid import
before it has a usable cooked selection.

Only registered owner controls for pending work can bypass the general busy
message. They still validate exact job/review identity and the owner's own state.
Scene edits, new job admissions and file writes retain the general editor guard.
Discovery marks these controls and their transport availability separately.
Cancellation is cooperative: completed copies, publications or disk commits stay
completed. Project-scoped status never exposes a previous project's retained job
metadata after switching projects.

## Bounds and cancellation

Input is UTF-8 JSON lines, capped at1MiB and64 nesting levels. The bounded reader
holds at most eight queued messages; excessive pipelining or duplicate active
request IDs close the bridge rather than overwrite another request's state.
The existing editor caps request/response lines at1MiB/16MiB. The MCP response
cap is34MiB because structured data is also included as text. Large scenes
should use paged entity queries. No large-scene latency guarantee is made.

Each editor exchange has a12-second total deadline. Cancellation is processed
while the dispatch thread waits on a socket. Queued cancelled work is skipped;
an in-flight exchange is abandoned and no response is emitted for that cancelled
request. A request already fully sent to the editor may still commit. Cancellation
does not undo committed work; use the same sequence receipt to resolve uncertainty.
Closing stdin cancels outstanding bridge work and exits. Valid notifications
never invoke scene tools, and unknown notifications receive no response.

## Coverage and remaining game-making workflows

This adapter covers **scene authoring, granted scene files and asset workflows**. It is
not complete game-development parity.
The following table is the maintenance checklist for subsequent integrations.

| Workflow | Current MCP coverage | Existing owner for remaining work |
| --- | --- | --- |
| Scene/entities/transforms/reflected properties | Implemented through the shared catalog and scene methods | `AuthoringSession`, `Scene`, domain validation |
| Undo/Redo and diagnostics | Implemented for authored scenes | Existing scene history/diagnostics |
| Current project/settings and document status | Read through `document.read` | `SceneDocument`, `ProjectSettings` |
| Scene Save/Save As/New/Open | Implemented with explicit `document.write`, normal owner gates and fresh routing targets | `SceneDocument`, `EditorFiles`, `ProjectLease` |
| Create/open/switch project; recovery restore/delete | Not exposed; use the editor | Existing project/file/recovery owners |
| Asset search/raw-source import/reimport, placement and source-file operations | Implemented through existing owners and explicit asset grants | AssetCatalog, ContentImports, SourceImport, SceneAssetDrop, ContentFiles |
| Asset import settings and source/domain documents | Remaining owner integrations | Existing import/source document editors |
| Materials/shaders/graphs, animation assets and domain documents | Not exposed as document tools | Existing asset authoring services and document editors |
| C++ source creation/editing, schema inspection and build | Not exposed | Existing source editor and native project/build controller |
| Play/Stop, live runtime inspection, logs and visual inspection | Not exposed; scene diagnostics remain available | Existing Play session, diagnostic and presentation owners |
| Game settings changes, validation and export | Not exposed | Existing project settings and game-export controller |
| FORGE engine/editor source modification | Outside game-project MCP authority | Separate explicitly authorized repository development |

Each missing workflow needs explicit targets, capabilities, lifecycle / dirty
draft handling, errors, cancellation and integration checks before advertising it.
Do not route file writes or jobs through scene transactions or launch an isolated
writer against an editor-owned project to work around a missing command.

## Validation

`mcp_stdio` launches the actual native tools process and real `LiveAuthoring`
socket hosts. It checks both protocol forms, shared command/schema coverage,
scene parity through an independent client, atomic failure, property updates,
Undo/Redo, revisions, replay/conflicts, read-only and busy denial, authentication,
disconnection, malformed/oversized/deep input, cancellation, EOF and credential
redaction. Existing `authoring_api`, `authoring_cli` and `authoring_live` tests
remain the shared service regression checks. `mcp_document_stdio` uses real editor file/document owners without a GPU window:
both protocol forms, ordinary/copy/first saves, disk conflicts/deletion, project
containment, leases, dirty/busy/source gates, recovery cleanup/failure preservation,
fresh targets, old receipt replay and scope revocation. Editor tests cover owner
lifecycle and controls at supported scales. Native Windows/editor validation is
separate from portable Linux process/socket and ImGui harness execution.


### Portable editor-owner fixtures

`FORGE_BUILD_MCP_EDITOR_TESTS=ON` with asset tools and SDL platform support builds
real owner fixtures on Linux without enabling the Windows/D3D12 editor. It reuses
the pinned ImGui dependency only in test targets; exported games still do not
link it. The shared Windows editor acceptance selection includes all `mcp_`
checks. `mcp_asset_stdio` uses actual workers and editor owners for raw glTF/audio
imports, source revision conflicts, model/mesh placement and history, reviewed
file operations, recovery, permissions and replay. Native Windows/GPU validation
remains a separate gate.
