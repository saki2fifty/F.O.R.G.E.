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

`forge_play_ui_reload` validates saved RML/RCSS/resources through the live editor
presenter and publishes only a usable replacement. It requires `play.execute`,
the current launch/session/generation and the usual target/revision/sequence.
Success returns `reloaded:true` and restarts native UI animations. Native rejection
returns `ui_reload_failed` with a diagnostic while retaining the previous usable
UI; unavailable presenters reject. Loading/gestures and stale Play identities
retain their normal guards. This is presentation state, with no Scene Undo entry
or arbitrary DOM/script mutation. Source APIs remain Edit-only: Stop Play to save
animated RML/RCSS through `forge_source_text_write`, then restart. Reload also
supports a user's externally saved UI edit during Play. See [Runtime UI](runtime-ui.md).

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

`forge_scene_rendering_set` includes the Scene's optional `auto_exposure` section
alongside manual exposure, lighting, bloom and color. For example, send
`arguments: {"auto_exposure":{"enabled":true,"min_ev":-4,"max_ev":6,"low_percentile":0.1,"high_percentile":0.9,"target_gray":0.18,"speed_up":1,"speed_down":3}}`
with the current discovered target/revision/sequence. Percentiles are normalized
0–1; the human editor displays percentages. Partial sections preserve omitted
values, inverted gain/percentile pairs reject atomically, and Undo/Redo use Scene
history. Save through the existing document owner to retain settings for export.
Read-only grants do not expose this mutation. These are authored Game settings,
not a new live-Play control. See [automatic exposure](scene-auto-exposure.md).

`scene.rendering.set` / `forge_scene_rendering_set` accepts
`{"temporal_anti_aliasing":{"enabled":true,"stability":0.85}}`.

The optional nested fields share Scene permissions, expected revision, replay,
Undo/Redo and document saving with the human Scene lighting controls. Stability
is finite 0–0.95; invalid candidates leave values and revision unchanged. Read-only
connections do not advertise the mutation. Export retains the authored settings,
not transient history. See [temporal anti-aliasing](temporal-antialiasing.md) for
geometry, camera-clear, resource and validation limits. Explicit sequence captures
reuse the capture owner's operation/sample identity and wait for temporal warmup;
no agent-specific rendering or clock is introduced.

The same tool accepts `{"depth_of_field":{"enabled":true,"focus_distance":10,
"focus_range":5,"blur_radius":8}}`. Distance and range are finite 0.001–1000000
metres; radius is finite 0–12 pixels. Nested patches retain omitted and opaque
extension fields. Scene edit permission, revision checks, replay, Undo/Redo and
Save use the same authoring owner as the controls. These are scene-wide settings
applied independently to each view; they add no capture clock or temporal delay.
See [depth of field](depth-of-field.md) for depth-writing Mesh eligibility,
queue-preserving fallback and resource limits.
`scene.rendering.set` / `forge_scene_rendering_set` also accepts
`{"distance_fog":{"enabled":true,"color":[0.25,0.35,0.5],"start":12,"end":34}}`.
Color is linear HDR RGB; start/end are ordered radial world metres. The same
Scene grant, revision, replay, history and save rules apply. Read-only clients do
not receive the mutation. See [distance fog](distance-fog.md) for limits and legacy
surface cook requirements; these are authored settings, not live Play controls.

`scene.rendering.set` / `forge_scene_rendering_set` accepts
`{"ambient_occlusion":{"enabled":true,"radius":0.5,"strength":1,"bias":0.01,"normal_influence":0.7}}`.
Radius0–10m, strength0–1, bias0–1m and normal influence0–1 are finite, including
when disabled. Normal influence defaults to0. Partial
patches use the existing Scene permission, expected revision, replay, atomic
Undo/Redo and Save behavior. Disabled/zero radius/zero strength skip rendering.
See [ambient occlusion](ambient-occlusion.md) for indirect lighting, visible-depth,
shader recook, camera and payload limits. This is authored Scene state, not a new
live-Play control.

`scene.rendering.set` / `forge_scene_rendering_set` accepts
`{"screen_space_reflections":{"enabled":true,"max_distance":25,"thickness":0.1,"strength":1,"max_roughness":0.5,"roughness_filtering":true}}`.
The optional `roughness_filtering` boolean defaults false. It uses the same Scene
validation, partial update, history and Save operation as the other fields.
Finite bounds are distance0–100m, thickness0.0001–1m, strength/roughness0–1,
including when disabled. Partial patches preserve extension fields and use the
same Scene grant, revision, replay, history and Save rules. Disabled or zero
distance/strength/roughness cutoff bypasses rendering. See
[screen-space reflections](screen-space-reflections.md) for material, visible-depth,
recook, camera and budget limits. This is authored Scene state with Play locks.

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
| `forge_asset_atlas_prepare` | Pack ordered published root sRGB Texture assets into a detached PNG/source review; no copy or publication yet. Reuses the import review/commit/cancel/close tools |
| `forge_asset_import_prepare`, `forge_asset_import_review` | SourceImport prepares the bounded source/dependency copy plan; review pages and its digest identify exactly what was inspected |
| `forge_asset_import_commit`, `forge_asset_import_cancel`, `forge_asset_import_close` | Confirm the exact batch/review, cancel pending work, or release a settled review; native workers publish each imported asset independently |
| `forge_asset_place`, `forge_asset_place_cancel` | SceneAssetDrop queues the same undoable Model/Mesh/Tile Map/Prefab placement used by viewport drops; Model options include source scene, animation clip and material variant |
| `forge_asset_files_prepare`, `forge_asset_files_review` | ContentFiles/AssetFileService prepare Move/Duplicate/Delete and known reference impact; discovered scene registration preserves its existing identity |
| `forge_asset_files_commit`, `forge_asset_files_cancel`, `forge_asset_files_close` | Confirm the exact operation/review or cancel/close its owner; Delete additionally requires `acknowledge:true`, and retained originals use existing recovery storage |

All mutations retain the complete scene target, observed revision and sequence.
Job identity is local to its owner; never apply an old batch/job ID to new work.
Place a published Tile Map with `forge_asset_place`, add `forge.physics_body_2d`
and `forge.tile_map_collider_2d`, then set the collider's exact layer key. The
existing `forge_property_set_many` operation authors `motion` (0 Static or1
Kinematic for Maps) and `sensor` together, with normal revision guards and one
Undo/Redo step. Sensor applies to the selected layer's rectangles. Save before
Play/export. Static or Kinematic nonsensor Boxes and full-cell Maps can set the
existing `one_way`/`one_way_margin` fields through the same property operations.
Kinematic targets or Transform Animator clips can move them. The existing
Character Controller 2D `platform_carry` property enables walkable support transport.
Sensor, Dynamic and custom-polygon one-way collision and Dynamic Maps remain
outside the runtime profile;
the general body schema still supports Dynamic for ordinary inline bodies.

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
An external source publication also starts a new document session when the clean
editor reloads. Read `status` again before editing; requests carrying the previous
session fail with `wrong_document`. Dirty drafts are retained and block automatic
replacement through the same registered owner used by the interactive editor.

`forge_asset_document_tile_collision` edits one Tile Set definition through the
same source owner. Supply `editor:"tile_set"`, exact session/revision, the full
uint64 local `tile` key and `collision`. Profiles are `{"kind":"full_cell"}`,
`{"kind":"none"}`, `{"kind":"convex","points":[[0,0],[1,0],[1,1]]}`, or
`{"kind":"polygon","points":[[0,0],[1,0],[1,0.5],[0.5,0.5],[0.5,1],[0,1]]}`.
Convex points require3..8 strictly counterclockwise unit-cell XY/up coordinates.
The published schema validates the exclusive profile alternatives; native source
validation rejects bad ordering/degeneracy and guards legacy reserved fields.
Polygon admits one simple concave contour, rejects repeated/adjacent-collinear
vertices and crossed/touching nonadjacent edges, and decomposes into triangles.
Explicit profile editing promotes to Source2, or Source3 for Polygon, when safe;
an already promoted source never downgrades through ordinary edits. It does not remint keys,
write Scene values or publish automatically. Undo/Redo and Save use the ordinary
asset-document methods. After publishing a Set, republish its dependent Maps
against that selected revision. Collision uses existing Static/Kinematic bodies,
sensors, cell flags and shape budgets; custom one-way polygons are unsupported.
Interactive unapplied collision drafts participate in the existing document-busy
guard. Read-only connections advertise no collision mutation binding.

`forge_asset_document_tile_animation` edits an atlas sequence through the same
Tile Set source owner. Supply `editor:"tile_set"`, exact session/revision, uint64
local `tile`, and `animation:{"enabled":true,"frames":[{"frame":0,"seconds":0.125},
{"frame":3,"seconds":0.25}]}`. Enabled loops need 1..64 rows; disabled sequences
allow 0..64. Frames must fit that tile's image grid, and durations must be finite
0.001..600 seconds. Order controls playback, including reverse order and repeated
frames. Disabled animation uses the scalar frame. Safe editing promotes to Source4
without changing keys, allocator or collision. It preserves unknown profile and
retained frame-position siblings. Legacy reserved animation fields block promotion.
Use ordinary Undo/Redo and Save, then republish dependent Maps. Unapplied interactive
animation/collision drafts block conflicting requests through the shared busy
guard. Read-only connections advertise no animation mutation binding. Runtime
animation uses completed simulation ticks; the asset preview has its own local
Play/Pause/Restart controls.

Timeline component fields use the same `forge_asset_document_patch`, validation,
Undo/Redo and publication tools. Read the current inspected custom component
admission with `forge_code_schemas`. Use its `reference_schema.components` fields
or the ordinary Scene schema tools for current edit/animation capabilities; the
raw declaration `structure` remains versioned admission data.
Promote the source to6 and add `component_bindings` descriptors containing stable
`component`, `field` and the exact `admission` stamp. Each key needs a matching
`component_values` row containing `component`, `field`, numeric `value` and optional
linear/cubic `curve`. For Boolean/integer/enum/bitmask fields use source7 and put
`value_type` on every descriptor: `scalar`, `bool`, `int64` or `uint64`. The integer
categories retain exact signed/unsigned JSON values; native widths, ranges, choices
and allowed flag bits still come from the matching schema. Discrete values hold
until the next key, reject curves, and require actual Boolean/integer primitives,
not strings or floating representations. Source8 supports nested struct leaves:
put an identical `member_path` array of stable member IDs below `field` on its
binding descriptor and every matching key-value row. Use an empty/omitted path for
root fields. Source9 adds `element_path` after the struct-only member prefix. Its
first part selects an entry: `{"index":0}` for unkeyed positional collections or
`{"key":"left"}` / `{"key":["left","speed"]}` for declared single/composite keys.
Later parts can be member IDs or nested selectors. Copy the entire identity to all
key-value rows. Use the schema's declared key order, native counts/domains and
current entries; keyed collections reject positional selectors. Missing entries
reject without creating/resizing a collection. Combined paths have at most 16parts;
read-only ancestors and UI fields remain unsupported. Preserve other source fields and
key/asset identities. Read the latest
document session/revision before patching; an invalid value leaves history intact.
The existing code creation/build/inspection, component-add and animator attachment
tools supply the rest of the workflow. Runtime schema/domain checks still apply;
a syntactically valid source alone does not establish native target admission.
See [transform animation](transform-animation.md) for the format and current limits.

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
startup scene, input, physics and game settings together. Analog input bindings
accept `outer_deadzone` and `response_exponent`; their effective range is validated
with Game/player inner-deadzone settings. Save and `forge_play_input` observations
use the same native input owner as the editor. Native/build fields are
preserved for their owner. Save checks the ordinary disk baseline; settings are
not Scene Undo. Dirty close requires explicit discard. `forge_project_settings_game_defaults`
opens the actual settings draft and retains its generated application identity
across editor frames; it does not save implicitly. Accepted close, including
Discard, clears obsolete diagnostics and queued Save requests. Cancel retains the
failed draft.

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
Each input line must contain one complete JSON record. A raw NUL cannot hide a
suffix or make a valid request prefix executable; malformed records return the
existing parse error before dispatch or mutation sequence allocation. Escaped
JSON control values remain valid. Editor response records receive the same
complete-record check, retaining their 128-level nesting bound; a malformed
response reports connection failure without adopting its result.
On Windows, JSON stdio runs in binary mode so text translation cannot hide an
input end marker. Both LF and CRLF input lines remain valid JSON framing.
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
Complete 3D/XY physics rays are Project Code APIs in that header and
`include/forge/sdk_client.hpp`. Agents use the same SDK reference reads, source
editing, build/inspection and Play operations to author and test queries. Canonical
query exclusions use the same public SDK paths, with bounded copied
reference arrays in Project Code. No separate MCP physics world or direct engine-
source operation is introduced.

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
`forge_play_status.timing` also includes `unscaled_time`, `unscaled_delta`
(seconds) and unsigned `unscaled_sequence`, copied from the existing clock.
Elapsed observations continue during Pause and zero rate without advancing fixed
ticks or authored revision. Status is read-only; it does not force a new sample.
A fresh Play/process has a new origin. Repeated samples may share a sequence;
these are transient monotonic observations, not calendar timestamps or durable saves.

Navigation runtime entity rows include `navigation_debug.diagnostic` for the
current failed sample. It is bounded display text from the navigation owner,
not a structured command or persisted source value. Empty means no current failed
reason. Honor phase/sampled/has_path before interpreting position,
projected_destination and velocity; a live edit can return Pending before its
next fixed tick. Read-only snapshots never create another navigation/error owner.

Navigation Agent tuning uses `forge_play_tune` with component
`forge.navigation_agent` and property enabled, has_destination, speed,
stopping_distance or destination_x/y/z. Pause to submit several coordinate values
without intervening simulation. The next fixed tick recomputes the route through
NavigationRuntime; the NavMesh asset remains outside live tuning. The same
reflected validation, execution grant, observed launch/session/generation, queued
ack and keep-values capture/Stop/review/history paths apply. No extra MCP schema,
pose writer or independent navigation implementation is introduced.

Compiler text pages and Play/exported-game log text share UTF-8-safe response
conversion. Invalid or incomplete native output bytes appear as replacement
characters in the returned text, so they cannot break the JSON log response.
Valid Unicode is preserved. The underlying process buffers and compiler-page
byte offsets remain unchanged; this conversion does not alter authored data or
relax structured protocol validation.

`watch` selects an Inspector entity for native audio/animation observation.
`audio_control`/`animation_control` require the exact watched entity; a human
selection change cannot redirect them. Mix and CPU controls use their native
owners. `profile_refresh` requests one snapshot without taking over the Profiler
window's watch. Its value includes `ecs`, the last requested/collected native fixed-pipeline plan, merge markers and cumulative native system CPU totals. Start collection and advance a fixed tick after requesting observations; Pause reads never run gameplay. Stop retains the plan, Clear invalidates its copy without resetting native lifetime counters, and replacement begins empty. The same bounded data feeds the Profiler's ECS systems view; native IDs are world-local diagnostic handles. UI commands go through the actual presenter's declared command
list, ID queue and acknowledgements, never DOM/script injection.

`forge_play_profile_trace` reads the cached isolated Play CPU profile as Trace Event
JSON under `play.read`; it does not execute gameplay or require an execution grant.
Start collection and request `profile_refresh` through the existing controls first.
Use `limit`1..128 (default128) and `offset`0..512; `trace.otherData.measured_spans`
gives the full measured count and `exported_spans` the page count. Keep the returned
`profile_state` on subsequent pages; refresh, Clear, world replacement or other
snapshot changes can reject it with `stale_profile`. Pause/Stop collection for
stable paging. All pages retain the full source origin and lane layout.
Completed host scopes have measured starts; duration-only SDK samples, GPU records
and ECS lifetime totals are not placed on invented intervals. `ts`/`dur` use
microseconds, while capture date/time/UTC offset describes the read boundary.
Logical overlap lanes describe one owner thread, not measured worker concurrency.
The machine-wide Editor trace remains in its UI because it can span projects.
See [CPU profiling](cpu-profiling.md) for semantics and current boundaries.

To capture actual native system dispatches, refresh the current profile first and
check `native_systems_available`. The existing `play.profile.control` operation
accepts optional boolean `native_systems` alongside required `enabled`/`clear`.
Use `forge_play_profile_control` with the current revision/launch/session/world
and execution grant. Enable collection and native capture, await acknowledgement,
advance a fixed tick, refresh and read `forge_play_profile_trace`. A cold or
unsupported enable reports `not_supported`; no alternate authoring path is used.
Category `ecs.system` carries completed owner-thread fixed-pipeline dispatches,
including nested native runs and excluding timer skips. The same512-record ring
and64-depth limit apply; `native_systems_dropped` is copied into trace metadata and
the cursor digest. Clear resets it. Replacement retains the capture preference
with fresh world-owned history. This does not expose worker utilization, every
arbitrary C++ call or an Editor-wide project-independent trace.

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
remain authoritative. Publication is separate from Scene Undo. Settings add optional
integer `plane`:0(default) uses3D NavigationSurface geometry;1 uses top-down XY
NavigationRegion2D rectangles. The six numeric profile fields remain required.
Create/edit regions with ordinary component/property tools, including validation,
Undo/Redo and Scene Save. XY builds publish guarded schema3 through the same
worker/job/status/cancel owner. One logical asset per Scene remains authoritative;
switching plane preserves its ID and requires rebuilding. No independent agent-only
geometry, bake or persistence path exists. Scene outline
selection/framing and centered resize handles are editor conveniences over the same
qualified width/height fields. Agents continue to use ordinary property tools and
history/Save; no new tool/schema or screen-coordinate authoring path is required.

The asset-authoring grant also exposes `forge_navigation_overlay`, with a required
boolean `visible`. It uses the same NavigationTools view flag as the editor checkbox;
status includes `overlay_visible` even before a bake is loaded. Normal qualified
target/revision/sequence checks apply. It changes no Scene history or file and does
not bake, project a point or compute a route. The overlay includes the selected
agent's requested destination using the selected saved asset's plane, alongside
existing triangles and runtime paths. Read-only connections do not expose this control.

Destination values remain ordinary `forge.navigation_agent` fields. A
`forge_property_set_many` batch can edit `destination_x/y/z`, with standard revision
guards, Undo/Redo and saving. The Scene destination gesture submits that same native
property operation. It changes two world-plane coordinates, while callers can
deliberately set all three fields. No MCP screen-coordinate drag tool is advertised.

`forge_dependencies_open/read/edit/save/discard` borrows the selected asset's
RuntimeDependenciesEditor draft. Edit supplies a finite typed set with reasons
and optional module ownership. Save reviews source revisions, optional native UI
resources and the persisted catalog baseline in the existing worker. Poll its
actual result; there is no parallel dependency graph or automatic conflict merge.
Reviews, sessions and completed operation results are project-scoped, including
copied projects with identical asset IDs. Switching projects invalidates clean
reviews even with Inspector closed. Open a fresh review and read its state before
editing. Existing dirty-review and active-save switch guards retain unfinished work.
A failed Open/Discard leaves the prior review, session, draft and operation diagnostic
intact. A successful Open/Discard starts a fresh review without the previous save's
error or operation ID. Successful asynchronous Save keeps its own operation ID
when reloading the published result.

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

The optional Boolean `verify_startup:true` selects the game's existing finite
startup diagnostic (`--verify-startup --show-fps`). It renders with D3D12 WARP,
uses offline audio, verifies startup and frame statistics, and exits. The same
supervisor reports the exit code and timestamped output; `startup_verification`
identifies the requested mode. This checks the real shipped executable on a
headless Windows worker, without establishing physical GPU/audio acceptance.
Omitting the option keeps normal game launch. No caller-supplied CLI arguments
are accepted.

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
checks. `mcp_asset_stdio` uses actual workers and editor owners for raw glTF/WAV/FLAC/MP3
imports, source revision conflicts, model/mesh placement and history, reviewed
file operations, recovery, permissions and replay. Native Windows/GPU validation
remains a separate gate.

`mcp_source_stdio` exercises all12 real source editor owners, graph schema/port
discovery, document sessions/revisions, source Undo/Redo, failed identity edits,
copy-on-write tile painting, saved-state dirtiness, native publication, typed
import setting atomicity, source disk conflicts, prefab source publication and
recoverable two-file Apply Undo/Redo through both MCP protocol envelopes. It
publishes Material, Collision, Editable Mesh, Particle Effect, Terrain, Tile Set,
transform-animation and WAV/FLAC/MP3 candidates through their real owners. Rig-dependent
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


The final assembled-package gate adds `mcp_editor_package_test.py`, using the actual
D3D12/WARP editor fixture and shipped tools/SDK/runtime. A test-only explicit grant
handoff is private and removed, never retained in project/preferences/evidence.
The client authors a material and C++ cube, builds/inspects it, drives actual Play,
steps/tunes/keeps values with Undo/Redo, captures a rendered Game image as MCP PNG
content, then exports and verifies the authored C++ running in the shipped game.
This is a native acceptance scenario, separate from recorded-pixel fixtures and
physical/user testing; inspect its result/captures before claiming that gate passed.

### Build 213 acceptance

Build 213 (`261024-000213`) passes the [compiled validation jobs](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40083004201)
at source `8200738a7055decfafae14b0705c1406cb8ef20f`: all four core/SDK profiles,
shared-runtime relocation, formatting and 336/336 native editor checks. The
[final package gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/40093162001)
reuses those exact compiled binaries and passes both packaged SDK and MCP scenarios.
The intervening change corrects only the Python test's SDK lookup to the existing
combined-package layout; it does not relabel engine binaries or change the SDK.

The native MCP capture validates against the pinned official 2026-07-28 schema,
including 199 advertised tool schemas and 273 successful tool argument sets.
Separate native owner captures validate both supported protocols. The test's
exported game reports 19 finite changing values of its rotation quaternion's Y
component at saved speed -180, both startup/frame-statistics verification messages
and exit 0. Its paused Game
PNG was inspected; full and preview images are identical 867×456 PNGs. This
establishes automated Windows/D3D12 WARP and offline-audio execution. Physical
hardware/audio and a particular AI application's connection remain user testing.

## Author gamepad feedback

`forge_play_gamepads` reads the same host device observation as the Game panel:
availability, topology revision, active connection and up to 16 copied device
records. `forge_play_gamepads_assign` accepts that current `revision`, opaque
`device` token and `slot` 0..4, plus normal document target/expected revision and
mutation sequence. It needs the existing execution grant, validates project
ownership and delegates to GameInput/SdlGamepads. Stale topology/connection,
invalid slot, full neutral-input queue or failed native Stop reject without
changing assignments. Only old/new slots' input and feedback are neutralized;
the operation does not invoke the global input-release preparation used by input
injection. This is transient host routing, without authored Undo or saved tokens.
Read discovery for `pad.1.*` through `pad.4.*` controls; `pad.*` remains available
for the legacy active controller. Tools do not create virtual or physical devices.

Use the existing C++ source/declarations/build tools to author a module that submits
Game service `rumble` requests, then Play and exercise its control actions through
`forge_play_input_submit`. The installed public gamepad feedback sample documents
the action IDs, bounded request and receipt handling. These are ordinary C++ game
behaviors; no separate agent haptic implementation or arbitrary raw platform API
is added. Actual controller output is executed by the focused editor/standalone
platform owner. A headless MCP test host without that physical adapter cannot
claim vibration acceptance. Use Play logs and the module's copied feedback counters
to distinguish completion from rejected/unavailable device feedback.

Audio codecs use the existing `forge_asset_import_prepare`/review/commit/reimport
and `forge_asset_settings_*` tools. MP3/FLAC share the Audio clip settings schema,
processing, publication and runtime PCM owner with WAV. No codec-specific MCP tool
or bypass is required. Source-copy grants, reviewed digests, project revisions,
last-good publication and Scene Undo boundaries remain unchanged.

Audio Source discovery includes the saved bool `pause_with_game` (defaulttrue),
labelled Pause with game. Existing component/property operations own authored
changes and Undo/Prefab persistence. SDK Play uses `forge_play_tune` for transient
live changes; `forge_play_watch`, `forge_play_audio` and `forge_play_audio_control`
retain source/world/sequence checks. Copied source observations expose effective
paused state separately from the overall game pause. No new tool family or agent
implementation is added. The focused MCP audio pause gate exercises both framing
modes through SDK Play, including live edits, one-shots, Stop and authored isolation.


Source filter authoring uses the same Audio Source schema and property operations:
low_pass_enabled/high_pass_enabled and low_pass_hz/high_pass_hz. Existing Play tune
and audio observations expose live admitted filter values without advancing fixed
time. Finite 20–20000 Hz validation and bad-value preservation belong to the shared
Scene/runtime owners. No separate agent filter graph, tool family or mutation path
is added; saved settings reach source/one-shot playback and export.

Echo uses those same property operations with echo_enabled, echo_seconds and
echo_feedback. Intervals admit .01..1seconds and feedback0...75; invalid native
tuning preserves the last admitted value. Existing audio observations include
admitted settings, retained echo_tails and echo_buffer_bytes, with no extra trace
or effect owner. Source controls freeze/resume post-EOS tails without restarting
their clips; Stop clears them. These reads/controls retain the same permissions,
watched identity, revision, generation and unchanged authored-Scene rules.

## Ordered Sprite frame regions

Native discovery for forge.sprite_renderer includes frame_regions as a vector of
up to512 pixel rectangles with x/y/width/height and optional rotated_clockwise,
source_width/source_height and trim_x/trim_y metadata. Source dimensions independently
default to the unrotated retained dimensions; offsets must fit the original canvas.
Use forge_property_set_many to change
frame_regions and frame atomically; an empty list restores the existing grid.
Indices are zero-based and coordinates are relative to the optional image crop.
Normal target/revision/permission/history rules apply. Sprite image Material and
texture references retain the existing asset owners.

Both Sprite and Tile Map Renderer discovery include `mip_filter`, with Disabled (0),
Nearest (1) and Linear (2). Set it through the existing property operations, including
`forge_property_set_many` when changing frame layout and filtering together.
Target scope, permissions, revision checks, Undo/Redo and saving remain the normal
Scene/Prefab contract. Sprite `forge_play_tune` accepts `mip_filter` through the
same runtime property owner; check the tune acknowledgement before continuing.
Tile Map Renderer filtering uses authored Scene/Prefab properties. Rendering
admission and last-good retention follow [Sprites](sprites.md#independent-frame-mip-filtering).

Simple, Sliced and Tiled image_mode values0/1/2 all support trim and clockwise
storage. Sliced borders use original-canvas pixels and must leave a positive
center in every entry. Tiled density repeats the full original canvas, including
transparent margins. Combine frame_regions, image_mode and any border edits in
one property batch when needed to produce an admitted complete candidate. Actual
trim still requires a Blend Material with depth writes off; texture fit and
last-good draw admission stay with the existing renderer.

During Play, forge_play_tune supports the list through the same runtime-property
owner in ordinary and SDK Play. Set Frame to0 before shrinking a list below its
current index. Poll tune_ack for validation; ordinary-host success carries the
applied value while failure carries error. This annotated list permits128KiB/8192
elements for expanded frame metadata; unrelated collections keep their existing
limits. Tuning modes/metadata does not advance a fixed tick. Stop restores authored
values. Actual image fit/last-good rendering and clip-frame constraints still
apply; successful authoring does not prove an unavailable image is usable.

### Touch controls

`forge_play_controls` discovers `touch.0.down/x/y/pressure` through `touch.7.down/x/y/pressure` and each control's numeric minimum/maximum. Use existing project-settings edit/save operations to bind ActionIds, then generation-checked `forge_play_input_submit` and `forge_play_input` to test/observe RuntimeInput. Coordinates/pressure are0..1, down is0/1, reset clears values; invalid batches reject before queue mutation. Agent-injected semantic values do not simulate an operating-system finger/device or certify physical capture. No separate touch tools or input database.

### Touch gesture units and deterministic observation

`forge_play_controls` also advertises `touch.gesture.active` and relative `pan_x/pan_y/pinch/rotation`, with units in the native tool description. Bind separate Axis1 actions through the existing settings edit/save owner. Pan is normalized image-center motion, pinch is log-scale delta, and rotation is clockwise radians. A semantic contact injection does not synthesize an SDL finger; submit the intended gesture values directly.

The same discovery includes digital `touch.gesture.tap`, `double_tap`, `long_press`
and `swipe_left/right/up/down`. Bind Digital actions through project settings, then
submit a 1/0 pair per recognition through `forge_play_input_submit`. Pause/Step makes
the next fixed pressed/released edges observable through `forge_play_input` in
ordinary Play. SDK Step uses its existing cursor-release/neutralization path;
use Resume and running playback to consume SDK pulses. Repeated identical pulses
before one latch coalesce under the existing digital edge semantics. Both
physical and semantic pulses use the same input/action/latch validation; injecting
raw semantic coordinates does not run the native timestamp classifier. Physical
Tap/Double tap/Hold/Swipe thresholds and cancellation are described in
[input](input.md). No classifier-specific tool or separate input state is added.

For exact relative observation, pause Play with physical capture released, submit the semantic batch, Step once and read `forge_play_input`. Step again to verify zero relative deltas. Active down/up in one batch retains both digital edges without discarding its movement. Keep existing revision/launch/session/generation guards; relative values do not allow analog deadzones/response curves. No new tools or authoring database.

## Read and configure controller motion

`forge_play_gamepads_motion` reads actual host raw sensor capabilities/modes and
latest routed measurements, with SI units, nanosecond timestamps and the shared
device revision. It uses the read grant and does not advance time. An unavailable
host, unsupported sensor, disabled sensor and no measurement are distinct.
`forge_play_gamepads_sensor` takes current `revision`, opaque `device`, `sensor`
(`accelerometer`/`gyroscope`) and Boolean `enabled`, plus existing target/document
expected revision/mutation sequence. It needs the execution grant, validates
project ownership, rejects pending native controls/gestures and delegates to the
same GameInput/SdlGamepads configuration as UI and SDK Game requests. Replay uses
the existing sequence receipt. Stale/foreign tokens, invalid payloads, unsupported
enable and native errors reject. This transient edit has no authored Undo and
saves no device identity. A changed mode increments device revision; query again
before another edit. Tools neither create hardware nor synthesize sensor events.

Use normal source/build/scene/startup/export tools for the original Gallery93
`GamepadMotion.cpp` workflow. Installing the example opts into enabling Player1's
supported sensors through unscaled Game requests. Raw readings require current
native gameplay routing; injecting an input action does not create a sensor
measurement. No mobile, calibration, fusion or orientation capability is advertised.

## Sprite atlas generation

With asset-write permission, call `forge_asset_atlas_prepare` with ordered
`textures` (1–512 Texture UUIDs) and `destination` (a new directory below Assets).
Optional `filename` defaults to `atlas.png`; `max_size` defaults4096 (at most4096),
`padding` defaults1 (0–64), `extrude` defaults true, `trim_transparent` and
`allow_rotation` default false, and `byte_limit` defaults64MiB
(at most64MiB). Only independent published sRGB RGBA8 Color textures are supported;
originals are not modified. No external import-root grant is required.

Poll `forge_asset_import_review` until review or failure. `generated` includes
width, height, ordered frames, captured input IDs/sources and settings. Commit
with the exact batch/review through `forge_asset_import_commit`, or cancel/close
through the same import owner. A changed input catalog invalidates the copy.
After cooking, `results[].asset` is the actual normal published Texture UUID;
failed entries have null asset. Queue admission is not publication.

Create/publish an ordinary unlit Sprite Material with that Texture through the
existing source-document create/patch/save workflow. On the target Sprite use
`forge_property_set_many` for `frame_regions: generated.frames`, `frame:0` and
`region_enabled:false` together. Assign the Material through ordinary reflected
properties and save the scene. Native paste additionally verifies the image ID/
dimensions; clients should compare those before applying the returned mapping.
Returned layout version2 frames include rotated_clockwise, original source
dimensions and retained offsets; existing reflected property discovery exposes
the same optional/defaulted members. Transparent trimming keeps one available
filter ring; stored turns are90 degrees clockwise. Transformed banks support
Simple, Sliced and Tiled image modes; Sliced borders use each original canvas.
Actual trim requires Blend with depth writes off. Frame
payloads support512 expanded entries in the declared128KiB/8192-element envelope.
Runtime/Scene/Prefab/history still use their existing owners and validation.
Packing is one-shot and does not automatically repack changed sources. Optional
Sprite/Tile Map mip filtering prepares independent frame chains in the renderer;
the generated source PNG remains an ordinary base image.

## Light emission and live size tuning

Recipe `render.light` and component `forge.light` expose `shadow_source_radius`
(world metres,0–10000) and `shadow_angular_radius` (directional half-angle radians,
0–float(pi/4)). Both default to zero. Discover actual schema fields and units for
the connection; use `forge_property_set_many` to set `cast_shadows:true` and the
appropriate source dimension in one ordinary Scene edit. Point/Spot use radius;
Directional uses angular radius. These affect shadow filtering only.

Revision, replay, history, save and project permissions use the existing property
operations. Invalid types/ranges leave the Scene and revision unchanged. Existing
Light documents without these fields retain zero defaults. During Play, only these
two shadow-source properties and the three emitter dimensions below are accepted
by `play.tune` / `forge_play_tune`; other Light
fields remain Edit-only. Use the current Play generation and poll the tune
acknowledgement before assuming application. Runtime tuning does not edit authored
values. Stop and use the tuning review to keep selected values deliberately.
Gallery111 and the Scene lighting manual describe the visual workflow and limits.

### Rectangle and Sphere emitters

The same recipe and discovered Kind enum append Rectangle3/Sphere4 without changing
Directional0/Point1/Spot2. Set `kind` and the appropriate dimensions with
`forge_property_set_many`: Rectangle uses `emitter_width`/`emitter_height` (full
world metres), Sphere uses `emitter_radius`. Defaults are1/1/0.5; each dimension
accepts finite0.001–10000. Scale does not resize the emitter. Existing old-document
defaulting, revisions, replay, grants, validation, Undo/Redo and save rules apply.
These are native Light properties, not a separate agent implementation.

The three dimensions are also allowed in `forge_play_tune`, with the current Play
generation and execution grant. Invalid types/ranges leave runtime values and the
simulation clock unchanged. Source Scene values remain unchanged until selected
Stop-time tuning is kept. Kind, color, intensity, range and layers are Edit-only.
Use `basis` and entity rotation to aim/roll Rectangle; Sphere ignores orientation.
Intensity is distant candela (Rectangle's front normal, Sphere's any direction),
and range uses center distance. Area shadows are unavailable. Gallery112 and the
Scene lighting manual cover visible shape behavior and receiver prerequisites.

## Local reflection probes

Discovery includes recipe `render.reflection_probe` and optional component
`forge.reflection_probe`. Use `forge_entity_create` with the recipe, then
`forge_property_set` for its typed texture reference and scalar controls. The
Scene grant, expected revision, replay, Undo and document-save rules are the same
as the Inspector. Texture is an imported color/HDR cube or panorama; dimensions,
finite ranges and signed 32-bit priority validate through the native component.
Invalid candidates leave the revision unchanged. Live Play tuning admits its ten
scalar controls, including `diffuse_intensity`, through `play.tune` /
`forge_play_tune`; texture assignment remains Edit-only.
Use discovered tools/fields for the current connection and permissions.
`diffuse_intensity` defaults to zero and controls local base diffuse light independently
of specular `intensity`. Both use the existing typed source/history and live tuning
owners; no extra tool is required. See [the lighting contract](local-diffuse-irradiance.md).

`capture.reflection.start` / `forge_capture_reflection_start` requires the project
asset grant and Edit mode, with the ordinary target/expected revision/sequence
routing. Required fields are `probe` and `destination` (a NEW folder below Assets
with an existing parent). Optional fields are `size` (16–1024, default256),
`near_plane` (positive, default0.1), `far_plane` (greater than near, default100),
`layers` (uint32, default all bits) and `assign` (default true). No external output
folder is needed. This uses the same image-task owner as PNG/sequence capture and
cannot overlap those operations.

Poll `forge_capture_status`; its `reflection` value reports phase, face count,
original revision/generation, destination, cancellation and the eventual Texture
`asset`. When `import_batch` appears, use `forge_asset_import_review`, then
`forge_asset_import_commit` with that exact `batch` and `review` and `import:true`.
No files/identities publish during rendering/review. Copy/import and Scene
assignment have distinct outcomes. If the original Scene/document revision is
still current and `assign` is true, publication assigns the map and resets its yaw
to zero in one Scene Undo step. Otherwise the published map remains unassigned.
Save after assignment. Read per-file import diagnostics and close the settled
batch through `forge_asset_import_close`.

`forge_capture_cancel` requires this task's exact operation UUID and the grant for
its workflow: project assets for reflection maps, output access for PNG/sequence
images. One grant cannot cancel the other workflow. It drains
accepted GPU/encoding/import work and prevents automatic assignment; already
copied sources/published assets remain. Asset import cancel/close tools keep their
existing batch guards. Cancellation/review controls can service their pending
owner; a new capture still requires normal admission. Runtime recapture and
in-place map replacement are not available. See
[local reflection probes](local-reflection-probes.md) for capture/rendering limits.

## Shared projector lists

The clustered projector profile uses the existing `render.decal` recipe and
`forge.decal` properties through `forge_entity_create`, `forge_property_set_many`
and `forge_scene_apply`. There is no separate agent projection implementation or
cluster-setting operation. Schema, target/revision, permissions, atomic history,
save and export remain owned by the same scene/document subsystems.

A visual scene supports 128 enabled, assigned, nonzero-opacity projectors; receivers
admit the complete required map/sampler set within their 0–8 slots. This rendering
profile is not an entity-authoring limit. Excess active projectors or incompatible
receiver cooks report presentation diagnostics and keep ordinary rendering.
The document/MCP regression creates 127 projectors alongside an existing one in one
transaction, reads all 128, rejects an invalid atomic batch without revision changes,
checks whole-batch Undo/Redo and saves all 128 through the ordinary document owner.

### Light images

Create an ordinary Light with `forge_entity_create` and use the discovered
`forge.light` schema with `forge_property_set` / `forge_property_set_many`.
`texture` is nullable; Directional/Spot require a rendering-capable 2D Color/HDR
Texture and Point requires a Cube. Set `image_width`, `image_height` and
`image_tiled` for Directional projection. Clear the image when switching to an
area kind; use one atomic property update for related values. Existing revision,
sequence replay, Undo/Redo and document save behavior applies.

During Play, `forge_play_tune` supports the image dimensions and tiling through
the existing runtime tuning owner. Image assignment remains Edit-only. Stale play
identity, invalid values and unsupported texture replacement retain current values.
Use normal source edits and saved assets for changes that must survive export.
See [coordinates, resource admission and limits](light-images.md). No additional
agent-only light or rendering operation is needed.
