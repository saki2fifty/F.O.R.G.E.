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
accepted. There is no arbitrary executable, shell or engine-repository tool.
Project files, public SDK reads, external imports and output files retain separate
roots. **Allow C++ build and execution** separately permits the existing compiler,
isolated inspection/migration, Play and exported-game supervision. Project native
code and build recipes run with the account's OS access: process separation and
MCP path checks are not an OS sandbox for that code. Execution-capable tools carry
an open-world protocol hint; hints do not grant authority.

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
`forge_scene_diagnostics` expose committed authored data. These scene reads do not inspect
the live Play world or transient UI previews; use their explicit workflow reads.

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
old draft and recovery. Separate reviewed recovery controls are described below.

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

## Shared source and importer documents

`forge_asset_document_types` lists the actual DocumentWorkspace source editors
and their suffixes. `template` exposes each existing owner's initial source shape;
`create` allocates the ordinary fresh asset identity, and `open` borrows that
editor's current document. These tools never create a parallel draft or history.
Material, material graph, collision, editable mesh, particle, terrain, Tile Set,
Tile Map, animation states/blend space/mask and transform animation are bound.

`status` returns a transient document session, revision, draft/publication state,
Undo/Redo availability and diagnostics. `read` accepts a JSON Pointer and pages
arrays. Mutations additionally require the exact `document_session` and
`document_revision`; reopening produces a new session. A stale scene target or
scene revision still fails independently. `patch` admits up to128 RFC6902
add/remove/replace/test operations as one validated owner edit, preserving asset
identity and allocator floors. Intermediate JSON is bounded to8MiB; each source's
stricter native parser limit also applies. Failed validation changes no history.
Active editor gestures, pending publication and close requests retain owner gates.

Tile Map reads return metadata; `cells` queries up to4096 addresses and `paint`
uses the existing immutable chunk storage. Metadata patches use `with_metadata`,
and cell edits use `paint`; both retain the existing history budget. Source Undo
and Redo affect that asset document rather than the scene. `save` checks the disk
baseline and admits the ordinary owner publication; poll status to distinguish
admission, completion and failure. `close` needs explicit discard for unsaved or
unpublished work. A saved source can remain after failed publication.

`forge_asset_graph_catalog` and `forge_asset_graph_node` expose the existing
material graph node catalog, native node factory and typed ports. Returned node
values become authored only when added through the graph document's validator.

`forge_asset_settings_types/status/schema` borrow registered import-setting
editors. `open` admits contained sources, including first imports. `edit` validates
all requested setting changes before adopting them into the shared draft.
`publish`, `identity`, `cancel` and `close` require the exact importer session and
state digest. Identity choices use the existing reviewed conflict/build key;
omitting `previous` explicitly selects a new logical asset. Importer settings
and publication retain their existing semantics and are separate from Scene Undo.

Prefab tools borrow PrefabEditor, PrefabLibrary and SceneDocument: list/capture,
source open/read/patch/publish/close, refresh, and reviewed instance Apply.
Source edits require the exact draft digest and preserve identity/publication
revision. Apply review captures the existing owner's candidate; commit requires
its digest plus acknowledgment that all current scene changes will be saved.
The existing recoverable prefab/scene transaction and Scene Undo/Redo own replay.

## Projects, settings and recovery

The optional **Allow a project workspace** control grants creation/opening within
a selected existing Projects folder, in addition to the initially connected
project. `forge_project_workspace_read` describes this scope; `forge_project_list`
pages its immediate project folders. `forge_project_create` rejects existing
destinations and `forge_project_open` accepts a relative workspace path or
`initial:true`. The existing file controller acquires candidate writer ownership
before replacing the current scene. Dirty scenes need explicit discard; other
source drafts and pending tasks must settle. Canonical path containment rejects
root escapes/redirects. Approved project transitions refresh targets and retain
receipts. An out-of-scope project switch revokes the connection. Without this
separate workspace grant, project switches retain ordinary revocation.

`forge_project_settings_read/edit/save/close` borrow the actual settings editor.
Edits require the observed draft digest and validate name, simulation frequency,
startup scene, input, physics and game settings together. Native/build fields are
preserved for their owner. Save checks the ordinary disk baseline; settings are
not Scene Undo. Dirty close requires explicit discard.

`forge_document_recovery_read` reviews current/untitled snapshot availability and
its exact content digest (bounded64MiB read). Restore/discard require that digest
plus the current scene target/revision/sequence. Restore retains native history /
reset behavior and does not implicitly save the scene. These controls can bypass
the general recovery-dialog busy state only after the file owner verifies its
actual sole popup and all other task/interaction gates. Other popups remain busy.
The completed synchronous operation precedes visual dismissal on the next frame.

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

## C++ project code

Project authoring exposes `forge_code_status`, `sources`, `read`, `setup`, `create`,
`open`, `replace`, `save`, `undo`, `redo` and `close`. Names after the first retain
the `forge_code_` prefix. Source files live under `Native/`; they have no AssetIds.
Open tabs return a transient `session` and content `state` digest, both required
for tab changes in addition to scene routing/revision/sequence. Reopening produces
a new session. Logical redirects into another owner, generated paths and external
save conflicts are rejected.

Replacement uses the actual text widget's single Paste/Delete Undo record and a
temporarily substituted clipboard reader; it never reads or writes the OS
clipboard. Widget text uses LF and an implicit final newline. A save without an
execution grant suppresses Build on Save. With that grant, the user's ordinary
Build on Save preference still applies. Save/build is not a native hot reload.

`forge_code_api_list` and `api_read` expose only public headers, templates, samples
and guides from the editor-selected matching SDK, not the engine checkout. Read
paths are relative to `sdk/`, for example `include/forge/native_sdk.h`.

The execution grant adds `forge_code_compiler_test`, `build`, `build_cancel`,
`inspect`, `inspect_cancel` and migration controls. Compiler tasks use SdkBuild;
`status`/`build_log` expose the existing timestamped log and exact operation UUID.
Cancel requires that UUID. Failed candidates retain the last good module.
Inspection runs in the matching isolated runtime; copied admitted metadata is
available in `forge_code_schemas` and the ordinary component/scene tools.

Migration uses `forge_code_migration_open` (`document:scene|prefab`), `read`,
`prepare`, `apply` and `close`. Read the exact review digest and advertised schema
pairs. Prepare validates rules and values in the existing isolated worker; Apply
rechecks document/module baselines. Scene migration is one ordinary Undo step;
prefab migration changes its draft and requires separate publication. The editor
never loads the project's native libraries. Managed deployment kits and graphical
exported-game execution currently target Windows; no portable kit is synthesized.

## Play and live inspection

The execution grant exposes `forge_play_start`, `pause`, `resume`, `step` and
`stop` through PlaySession and the editor's existing source/asset/physical-input
policies. Start requires ready assets/current admitted C++ and never silently
builds or discards drafts. Pause/Step retain the existing physical-release checks.

`forge_play_status` returns an owner-generated `launch` UUID, wire `session` and
world `generation`. Controls must echo all three; an old starting process cannot
accidentally control a new process before its wire hello. Queued controls are
acknowledged by later status/timing, not by admission alone. The authored scene's
revision is separate from advancing runtime snapshots.

Runtime reads include paged `snapshot`, `schema`, timestamped `log`, `diagnostics`,
`physics`, `input`, `ui`, `audio`, `animation` and `profile`. Pause for stable paged
inspection. `tune` submits one existing reflected property; inspect `tune_pending`,
`tune_ack` and the runtime snapshot for actual acceptance. It changes no source or
schema. `input_submit` accepts up to64 validated events from `controls`, using the
existing input queue and physical release policy; reset releases controls.
`time_scale` uses the existing finite [0,4] clock policy.

`watch` selects an Inspector entity for native audio/animation observation.
`audio_control`/`animation_control` require the exact watched entity; a human
selection change cannot redirect them. Mix and CPU controls use their native
owners. `profile_refresh` requests one snapshot without taking over the Profiler
window's watch. UI commands go through the actual presenter's declared command
list, ID queue and acknowledgements, never DOM/script injection.

`forge_play_tuning_capture` freezes an eligible live-value review. Read/select its
fields, Stop, then Apply through RuntimeTuningReview. Apply creates one Scene Undo
step and Save remains separate. Changes to the reviewed scene/schema invalidate
it; arbitrary runtime-only entities or C++ state do not become authored values.

## Raw UI/shader sources, recipes and navigation

`forge_source_text_list/read/write` and `source_folder_create` cover raw project
RML/RCSS/HLSL/includes and shader-program declarations under Assets, Sources, UI or
Shaders. These external-style file writes require the exact disk digest (or
`missing`), have no document Undo, and cannot write scenes, prefabs, typed source
JSON, metadata, C++ or engine files. Check logical paths before canonicalization
so a contained alias cannot cross source ownership. Shader declaration changes
preserve their identity; `source_shader_create` mints a new identity and validates
stages. Compilation stays in the existing shader importer/worker. UI register and
example tools use the existing UI catalog admission; live reload stays separate.

`forge_script_*` borrows FlecsScriptEditor for registration/open/read/replace/save,
isolated diagnostic preview/result/cancel and close. Source-session digests and
native disk baselines remain authoritative. Failed previews retain the prior
successful result; they never write the Scene. This data/world-recipe facility
is separate from C++ gameplay and has no advertised document/Scene Undo.

`forge_navigation_status/build/cancel` uses NavigationTools and its bounded static
bake worker. The native settings/profile and stale-geometry publication check
remain authoritative. Publication is separate from Scene Undo.

`forge_dependencies_open/read/edit/save/discard` borrows the selected asset's
RuntimeDependenciesEditor draft. Edit supplies a finite typed set with reasons
and optional module ownership. Save reviews source revisions, optional native UI
resources and the persisted catalog baseline in the existing worker. Poll its
actual result; there is no parallel dependency graph or automatic conflict merge.

## Output, image inspection and export

A separately selected **Allow an output folder** grant permits output only inside
that canonical root. `forge_output_folder_create` creates a new child with an
existing parent. Root redirects and path aliases are rejected.

`forge_capture_start` requests the next rendered Scene/Game PNG. Existing files
require `replace:true`. `capture_status` reports the native image task. Its ready
`capture_image` returns a bounded PNG **image content block** plus metadata; base64
is not repeated in text or structured JSON. The preview fits1024 pixels, while the
full PNG retains rendered dimensions. Only the task's exact current ready operation
is readable. `capture_preview` prepares a completed owned PNG or sequence frame;
there is no arbitrary filesystem image-read tool.

Timeline frame export uses `capture_sequence_open/read/start`, the actual sequence
controller and observed source revision/preview generation. Start chooses a new
folder and native end-exclusive frame plan; fixed dimensions render real pixels.
The existing task owns sampling, resource readiness, IO, cancellation, cursor
restoration and `frames.json`. Completed/cancelled frames stay available. A single
readback/file write already committing must finish rather than promise rollback.

`forge_export_start/status/cancel` borrows GameExportTask and the same native
export service as the UI. Save drafts first. The selected runtime/module kits,
closure validation, worker progress, staging, recognized-output replacement and
promotion recovery remain authoritative. Native-project inspection additionally
requires the execution grant. `replace:true` is required for an existing output;
unrelated directories are never adopted. No editor, SDK, compiler or MCP service
is added to the exported game.

With execution allowed, `forge_export_run` verifies this task's exact completed
manifest/files and supervises that Windows executable, with no arbitrary path,
arguments or shell input. Poll status and timestamped `export_run_log`; cancel
stops its process. Native code retains the account's OS access.

## Coverage and maintaining parity

These workflows are implemented in **development source**, not the current
Build208 ZIP. Platform execution/visual acceptance remain separate from implementation.

| Workflow | MCP coverage | Authoritative owner |
| --- | --- | --- |
| Scene/entities/components/relationships/properties | Shared catalog, reads/queries, atomic changes, history | AuthoringSession / Scene |
| Projects/scenes/settings/recovery | Workspace grant, file lifecycle, settings defaults/draft/save, reviewed recovery | EditorFiles / SceneDocument / ProjectSettingsEditor |
| Assets/imports/settings/placement/file changes | Existing reviews, identities, jobs, cancellation and publication | ContentImports / SourceImport / import editors / SceneAssetDrop / ContentFiles |
| Typed reusable sources and graphs |12 actual documents, schemas/templates, validation/history/publication | DocumentWorkspace / AssetSourceDocument |
| Prefab source and instance Apply | Native draft/library and durable paired history | PrefabEditor / SceneDocument |
| C++ sources/API/compiler/reflection/migration | Scoped source editor, exact SDK, isolated native workers | CppSourceEditor / SdkBuild / AuthoredComponents |
| UI/shader text and Flecs recipes | Baseline file writes, native registration/import, diagnostic preview | Source storage / UI catalog / shader importer / FlecsScriptEditor |
| Navigation and declared runtime dependencies | Actual bake and reviewed catalog publication | NavigationTools / RuntimeDependenciesEditor |
| Play/inspection/input/live values/profiling/UI | Exact launch/session/world, existing controls/observations/review | PlaySession / GameInput / UiPresenter / RuntimeTuningReview |
| View images and sampled frames | Owned output, real image blocks, native sampling/readback/IO | ViewImageTask / Timeline / presentation |
| Export and supervised testing | Native validation/publication/run/cancel under output/execution grants | GameExportTask / export_standalone_game |
| Engine repository, arbitrary shell/OS automation, editor layout/preferences | Outside game-project MCP authority | Separate repository or desktop work |

When an engine change affects game authoring, update its existing owner binding,
shared schema/discovery, capability and lifecycle guards, actual-owner integration
checks, this coverage table and the user manual in the same coherent batch.
A feature is not MCP-complete merely because a descriptor or stub exists. Do not
introduce another writer/world/history/asset database or bypass an open project's
lease. Do not route non-Scene files/jobs through Scene Undo. Platform-specific
execution and newly changed consumers require their own proof.

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

`mcp_source_stdio` exercises all12 real source editor owners, graph schema/port
discovery, document sessions/revisions, source Undo/Redo, failed identity edits,
copy-on-write tile painting, saved-state dirtiness, native publication, typed
import setting atomicity, source disk conflicts, prefab source publication and
recoverable two-file Apply Undo/Redo through both MCP protocol envelopes. It
publishes Material, Collision, Editable Mesh, Particle Effect, Terrain, Tile Set,
transform-animation and WAV candidates through their real owners. Rig-dependent
animation sources are exercised for draft validation/history; graph shader
compilation is not claimed by this fixture. It drives ImGui frames without a GPU
window; graph shader compilation and native
Windows/client execution are distinct acceptance gates.

`mcp_project_stdio` checks both protocol forms with actual project/settings/file
owners: approved-root creation/opening, stale state, replay after retarget,
symlink/root escapes, dirty drafts, contended writer ownership, saved settings
conflicts, current/untitled recovery, scope revocation, readonly denial and actual
ImGui recovery/foreign-popup gates without a GPU window.


`mcp_code_source` checks the actual text widget's replacement/history, clipboard
isolation, disk conflicts and source ownership. With the Native SDK enabled,
`mcp_install_sdk` installs the real selected kit for `mcp_code_inspection_stdio`
and Release-only `mcp_code_stdio`. They use actual compiler and isolated SDK
workers for build/cancel, reflection and reviewed schema migration, including
exact64-bit values and Scene Undo/Redo. The managed deployment helper is
Windows-only; portable compilation does not imply Windows deployment acceptance.

`mcp_workflow_stdio` uses native project owners for UI/shader sources, dependency
publication, Flecs preview diagnostics, navigation baking, an actual headless Play
process, RmlUi semantic commands, fixed stepping, live tuning/Keep/Undo and export.
It is registered when `FORGE_TEST_MCP_RUNTIME_KIT` selects an authentic prepared
Windows runtime kit. Export admission and output hashes are checked on Linux;
that does not execute its graphical Windows game or a combined C++ Windows export.

`mcp_image_stdio` exercises the real image-task IO, PNG encoding, bounded previews,
MCP image blocks and sequence receipts with detached recorded pixels and a
recording frame source. It checks owner/path/operation guards, cancellation and
frame restoration without creating a GPU device. Actual renderer/readback,
Timeline playback and Windows MCP client acceptance require the native gate.
These fixtures do not claim physical display, audio or GPU acceptance.
