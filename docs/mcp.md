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
`mesh.scatter` becomes `forge_mesh_scatter` for bounded foliage placement.
It uses an effective Mesh Renderer template and explicit world center, disk radius,
spacing, integer count/seed, scale multipliers and yaw bounds. It creates an
ordinary independent group in one Undo step; a failure creates no partial patch.
The base settings are required and discoverable. Spacing compares new horizontal
origins. Optional Boolean `placement_mask` retains a nonempty subset of the
original candidate order, preserving seeded values and minimum spacing. Results
report candidate_count/placed_count; count remains the pool size. Optional
`world_normals` supplies upward directions for surface-frame orientation in that
same order (finite components±1e12, Y>0, scale-safe normalization). Default without
normals retains world-up yaw/template orientation; aligned_to_slope reports the
selected profile. Optional `world_heights` supplies one finite world Y per generated origin;
otherwise placement uses the fixed Y plane. Children, gameplay, animation and
Prefab links are not copied. Renderer values and template local
rotation/scale are copied through the shared Scene command. See
[foliage placement](foliage-placement.md) for exact bounds and current limits.

Optional Mesh Renderer [Wind](mesh-wind.md) uses `forge_property_set` with
component `forge.mesh_renderer`, field `wind` and its complete discovery-default
struct. Native units/ranges/descriptions accompany its fields. `mesh.scatter`
copies that renderer value into each independent child. It does not need a
separate wind tool, asset or runtime placement operation.

[Fog Volumes](local-fog-volumes.md) use `forge_entity_create` with recipe
`render.fog_volume`, or `forge_component_add` with `forge.fog_volume` on an existing
entity. Discovery supplies the eleven reflected fields and Box/Sphere choices.
Use existing `forge_property_set` / `forge_property_set_many`, history and document
tools for authored values. `forge_play_tune` accepts those same fields on an
existing runtime Fog component; scope, generation, validation and authored
isolation use the ordinary Play owner. Keep values uses the existing tuning
review/apply tools after Stop. No separate fog tool, permission or source database
is required. Saved Fog intent requires a matching runtime kit even when disabled.

`forge_foliage_collision_start` binds `foliage.collision.start` as a read-only
`asset.read` workflow. Pass ordinary scatter `arguments` or the arguments returned
by terrain preparation. Optional settings: radius 0.001..100(default 0.25), offset
as `{x,y,z}` in world meters(each+/-1000, default 0,0.5,0), uint32 mask(default all),
include_sensors(default false), surface(entityID) and ignored(up to14 distinct
entityIDs). The template and surface are excluded automatically. It tests existing
authored 3D colliders without running project code or simulation, using one private
native/resource owner. It does not query live Play poses or full plant Mesh bounds.

Start returns an operation UUID. Poll `forge_foliage_collision_read` with that
exact `operation`; states are preparing/querying/ready/failed/cancelled. Ready
`result` includes copied arguments with an original-order placement_mask,
scene_revision, candidate_count/retained_count, digests, settings and physics_tick 0.
At most512 candidates,16 native queries/poll and a 30-second deadline apply.
Terrain masks/heights/normals remain aligned; excluded candidates are not
resampled. Empty retention fails. Scene/project changes cancel the operation;
asset/settings changes invalidate preparation and explicit ready reads. Exact
same-input retries reuse the operation; a different busy request rejects.
`forge_foliage_collision_cancel` cancels that exact transient operation. All three
are available to read-only clients; none publishes files or changes Scene/history.
Apply ready arguments with `forge_mesh_scatter` at scene_revision and the ordinary
mutation sequence. Re-run start to check current asset/settings selections before
applying a previously read result. Saved objects remain independent of the task.

`forge_foliage_terrain_prepare` binds the read-only `foliage.terrain.prepare`
workflow. Supply a `terrain` Scene entity ID, the ordinary scatter `arguments`
without `world_heights`, `placement_mask` or `world_normals`, and optional `height_offset`
(-1000–1000), `minimum_slope`/`maximum_slope` (finite inclusive0..90degrees,
minimum<=maximum, default0/90). Slope uses the actual transformed geometric
triangle normal, not smoothed shading. Filtering returns an original-order mask,
`candidate_count` and `retained_count`; no candidates retained is an error.
It never resamples to fill count. Optional Boolean `align_to_slope` defaults false;
when true the same prepared triangle normal is returned in `world_normals`.
The Scene command tilts the placement frame before seeded yaw/template rotation;
scale/signs/heights remain unchanged. Preparation reports aligned_to_slope.
Invalid range/mask/normal/type/coverage checks preserve
Scene/history and share native errors. The ordinary mesh.scatter command remains
the mutation/Undo owner. It validates
one current published heightfield selection and matching on-disk source, then
returns `arguments`, `scene_revision`, `terrain_mesh`, `terrain_revision` and
`source_digest`. Apply the returned arguments with `forge_mesh_scatter` at the
returned Scene revision, using the normal mutation sequence. Preparation does
not publish files or change Scene/history, is available to read-only clients and
checks the same project/target scope. Upright terrain yaw/scaling is supported;
tilt/inversion, missing coverage and source/publication mismatch reject. The
saved result is an ordinary static group, without a live terrain link.

`forge_scene_apply` groups 1..128 catalog commands into one atomic Undo entry;
`forge_scene_replace`, `forge_history_undo` and `forge_history_redo` preserve their
existing transaction semantics. Core editor read-only discovery includes those
four Scene reads plus `forge_project_read`. Project workflows such as terrain
preparation expose their additional reads under the granted capabilities; isolated
scene-only test hosts retain the four core reads.

`forge_scene_rendering_set` includes the Scene's optional `auto_exposure` section
alongside manual exposure, lighting, bloom and color. For example, send
`arguments: {"auto_exposure":{"enabled":true,"min_ev":-4,"max_ev":6,"low_percentile":0.1,"high_percentile":0.9,"target_gray":0.18,"speed_up":1,"speed_down":3}}`
with the current discovered target/revision/sequence. Percentiles are normalized
0–1; the human editor displays percentages. Partial sections preserve omitted
values, inverted gain/percentile pairs reject atomically, and Undo/Redo use Scene
history. Save through the existing document owner to retain settings for export.
Read-only grants do not expose this mutation. These are authored Game settings,
not a new live-Play control. See [automatic exposure](scene-auto-exposure.md).

World Sprite and Tile glow uses that same Scene operation. Send
`arguments: {"bloom":{"enabled":true,"threshold":0.25,"intensity":1.25,"radius":3}}`
to `forge_scene_rendering_set` with the current discovered target, revision and
sequence. Partial bloom fields retain the other values; invalid candidates do not
change the Scene or revision. Replay, Undo/Redo and document Save use the existing
owners. The threshold acts on linear world-image brightness before exposure, so
ordinary image highlights generally need a value below 1. This is authored shared
bloom, with no separate Sprite effect tool or live-Play mutation. See
[HDR bloom](hdr-bloom.md) and Gallery example **128-sprite-tile-bloom**.

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

The same tool accepts
`{"fast_approximate_anti_aliasing":{"enabled":true,"subpixel_strength":0.5,"search_steps":12}}`.
Smoothing is finite 0–1 and steps are an integer 1–16. Partial patches preserve other
settings and unknown metadata; rejection retains values/revision. Existing Scene
permissions, replay, Undo/Redo and Save apply. The filter processes composed
current display color; it adds no history and keeps subsequent UI untouched.
Export retains saved settings. See the [spatial filter contract](fast-approximate-antialiasing.md)
for output limits and current validation scope.

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
| `forge_asset_search`, `forge_asset_read` | Borrow ContentImports' published AssetCatalog snapshot plus immutable engine assets; search pages contain at most 128 records |
| `forge_asset_skeleton_joints` | Read selected imported joint UUIDs, parent UUIDs, names and rest TRS in pages of1–128, with optional exact selected-revision checking; bounded to256 joints and64MiB model bundles |
| `forge_asset_animation_retarget_catalog` | Describe the implemented target-bound baking profile and shared `animation_retarget` Source workflow; no publication or Scene mutation |
| `forge_asset_animation_graph_catalog` | Describe the bounded Clip/Blend/Additive DAG, named slots, scoped node UUIDs, source layout, exact skeleton binding and shared `animation_graph` Source workflow |
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
existing `forge_property_set_many` operation authors `motion` (0 Static,1
Kinematic or2 Dynamic) and `sensor` together, with normal revision guards and one
Undo/Redo step. Sensor applies to the selected layer's rectangles. Save before
Play/export. Static or Kinematic nonsensor Boxes and full-cell/custom-polygon Maps can set the
existing `one_way`/`one_way_margin` fields through the same property operations.
Kinematic targets or Transform Animator clips can move them. The existing
Character Controller 2D `platform_carry` property enables walkable support transport.
Sensor and Dynamic one-way collision remain outside the runtime profile. Custom
polygons use original front-facing edges through the same runtime owner. Dynamic Map bodies require `forge_transform_binding` with
`spatial:{mode:"world"}` before Play. Existing body density, mass, lock and gravity
properties apply to the complete compound. No separate agent-only body operation
is needed.

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

Editable Mesh template/create tools accept `editor: "editable_mesh"` and
`mesh_profile: "planar_quad"` with optional positive `mesh_size: [width,height]`
(default `[1,1]`), or `mesh_profile: "planar_polygon"` with a required strictly
convex `outline` of 3–16 XY pairs. `cube` remains the default; `sculpt_sphere` is
also available. Flat profiles face +Z and map UVs across their bounds. These
options reject on other editors and Open. Template allocates no files; Create
uses the existing asset identity/document owner. Patch, Undo/Redo, Save and
publication keep their usual scoped revision/permission rules. Assign a published
Mesh and Unlit Material through ordinary Scene tools and use an orthographic
Camera. See [Editable Meshes](editable-meshes.md).

`forge_asset_document_types` lists the actual DocumentWorkspace source editors
and their suffixes. `template` exposes each existing owner's initial source shape;
`create` allocates the ordinary fresh asset identity, and `open` borrows that
editor's current document. These tools never create a parallel draft or history.
Material, material graph, collision, editable mesh, particle, terrain, Tile Set,
Tile Map, Sprite atlas, animation states/blend space/mask, transform animation and
Navigation Behavior, UI Localization, Application States and Render Texture are bound. `forge_asset_behavior_catalog` describes the native
behavior source versions1/2, state actions/conditions, tree node kinds and examples,
finite limits, row shapes, monotonic IDs and execution policy. Use editor
`navigation_behavior`; template/create accepts optional `behavior_profile` with
`state_graph` (default) or `tree`. Open reads the existing saved profile and rejects
that creation-only parameter. Tree children are ordered node IDs; source patches
use the existing document revision/validation/history, including unfinished drafts
and stricter connected-tree publication. Use the
ordinary template/create/read/patch/history/save/close tools. Its component's
graph/target/anchor/enabled fields use the existing reflected Scene authoring tools.

`forge_asset_render_texture_catalog` describes saved Camera output dimensions,
linear-HDR/opaque limits, Camera field, composition/feedback rules and a full typed
2D Color Material `binding_template`. Use editor `render_texture` with the same
source tools. Publish it, replace the template's null UUID with its output UUID,
then author a Material version3 role and publish that Material. Camera output and
Mesh material assignments use ordinary reflected component/property tools and
Scene Undo. Separate producer/screen layers; conservative selected reads reject
feedback even when a screen is outside the frustum. After resizing an output,
republish dependent Materials before export. See [the complete contract](render-textures.md).

For interactive output panels, discover `forge.ui_surface_input` in the shared
component schema. Add it to the Mesh entity, set `render_texture` to the same
image and configure `enabled`. Geometry0 selects the undeformed Engine / Quad;
Geometry1 selects retained mesh triangles, including supported skin/morph poses
and static-mesh wind. `geometry` is the shared reflected choice field, with
`Quad`0/`Mesh`1 and a missing-field default0. Discovery describes UV/material
limits, unique reverse projection and capture invalidation; the current runtime
kit supports both profiles. Use `forge_component_add`, `forge_property_set`
and the ordinary Scene save/history operations. Its Material must meet the
[UI surface input profile](runtime-ui.md#interactive-scene-surfaces). UI Document
source/model authoring and semantic Play commands retain their existing tools.
MCP does not inject a raw ray or pointer gesture; graphical native focus and
capture are owned by the running presenter.


`forge_asset_localization_catalog` describes bounded literal-message catalogs,
locale tags, fallback, source limits and RML bindings. Use editor
`ui_localization` for the same source template/create/read/patch/history/save/close
workflow as the UI document. The default locale owns message keys; translations
may omit them. Key rename patches must update every language and calling RML.
Source gestures block agent mutation; agent patches retire stale native text
buffers before a subsequent user gesture. Scene/Prefab catalog and locale values
use the ordinary component/property tools. In Play, `forge_play_tune` can adjust
the controller's locale; `forge_play_ui_command` can send `forge.locale` and a
valid locale string through the existing active-document gate. Source and catalog
reference changes remain Edit-only.

`forge_play_ui` returns copied runtime settings in `value.localization` and
`presentation_localization` from the actual live presenter, or null if unavailable.
The latter includes the displayed requested/selected/default locale and fallback
message count. It may still describe the previous complete presentation after a
failed replacement. It is read-only and creates no second locale authority.

`forge_asset_application_states_catalog` describes direct/hierarchical/computed/
subordinate source shapes, keys, limits, conditions and runtime bindings. Use source
editor `application_states` and the same template/create/read/patch/history/save/
close owners. Scene attachment uses the reflected `forge.application_states`
component and its typed `definition` field; semantic keys and labels are not asset
references. Key renames do not rewrite external RML/C++ strings.

`forge_play_application_states` returns copied current choices, active/ancestor
membership, pending requests and per-domain revisions. `forge_play_application_state_request`
requires runtime execution permission, current launch/session/generation and the
normal document revision/sequence envelope, plus `domain`, `state` and
`domain_revision`. The latter is the domain's observed revision, independent of
the envelope's `expected_revision`. Submission queues PlaySession's existing live
command slot. Read status for `application_state_pending` and
`application_state_error`; stale/computed/inactive/unknown state rejections leave
Play alive. State transitions do not author a Scene edit or advance a paused tick.

`forge_play_ui_command` can send `forge.state` with a bounded
`domain:state:revision` string through the current visible document's existing
command gate. `forge_play_ui.value.application_states` is the runtime observation;
`presentation_application_states` is copied from the actual displayed native set.
The latter may describe a complete predecessor after failed presentation admission.
These observations and controls share the core state owner rather than creating
an agent-owned state machine. See [Application states](application-states.md).

Optional **State Condition** uses the normal reflected
`forge.application_state_gate` component. Discover its fields, add it with
`forge_component_add`, and edit `enabled`, `domain` and `state` through normal
property operations and Scene history. During Play, `forge_play_snapshot` retains
the object and reports `state_active:false` when it or a structural ancestor is
ineligible. `forge_play_tune` accepts an existing condition's fields, validates
enabled references through the runtime owner and preserves authored source.
Poll `forge_play_status.tune_ack`; success contains the applied value, while
failure includes its error. Existing launch/session/generation, revision,
sequencing, permissions and Undo rules apply. Newly eligible physics realizes
on normal synchronization; state changes do not advance the clock.

`forge_play_application_state_scope` binds a runtime-created root to an active
state/ancestor. Supply its current EntityRef as `entity:{scene,entity}`, `domain`,
`state`, `domain_revision` and the same launch/session/generation mutation scope.
Read the root from `forge_play_snapshot` and the domain revision from
`forge_play_application_states`. The operation uses the existing Play command
slot and both transports. Pending controls, stale scope/revision, authored roots,
inactive states and rebinding reject. Read `application_state_pending` and
`application_state_error` for completion. Successful binding has no authored Undo;
state exit retires the structural group. Runtime creation remains the existing
C++ entity/Prefab API; this tool does not create entities or edit engine source.

Material graph templates and creation accept `editor:"material.graph"` with
`domain:"mesh"` (default) or `domain:"sprite"`. Both call the same native factories
used by Content. A Sprite template contains image, Color, Multiply and Sprite
Output; its output references the primary image by stable binding key. The graph
catalog exposes the output schema and help, and node inspection exposes its
Color4 input and `image`/`tint` configuration. Patch, history, sessions, revisions,
Save and publication remain owned by the source editor. Unknown domains, `domain`
on another editor and creation options on Open reject. A source template/edit is
not evidence of native Shader compilation; custom Sprite programs require the
Windows/D3D12 Shader worker.

Sprite atlas documents use `editor:"sprite_atlas"` and `.atlas.json`. Templates
and creation require `textures`, a1–512 UUID array of input Texture references.
Other editors reject this option; `tile_set` remains exclusive to Tile Maps.
Atlas rows have stable local uint32 IDs and an allocation highwater retained
through source Undo. Shared patches replace/reorder input rows and change packing
settings; owner validation rejects duplicate/missing IDs, backward counters and
reuse of retired keys. Save invokes the same editor/importer/publisher used by
interactive authoring. Removing a key requires repairing Sprite/Clip references.
The cooked Texture uses required output profile 2 with its attached frame table;
Sprites opt in through `texture_frames`. No additional atlas MCP draft exists.

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
The `contours` alternative is
`{"kind":"contours","contours":[{"points":[[0,0],[1,0],[1,1],[0,1]],"holes":[[[0.25,0.25],[0.75,0.25],[0.75,0.75],[0.25,0.75]]]}]}`.
It admits1..4 filled contours with0..4 holes each; all loops use3..8 counterclockwise
unit-cell XY/up points. Holes must stay strictly inside their own contour. Touches,
crossings, overlapping solids and nested holes reject; separate islands inside a
void are valid. The schema advertises these bounds and alternatives. Unknown
profile fields and surviving contour-position fields remain during typed edits.
Extensions belong to array positions, including when geometry changes position;
truncation discards removed positions and changing kind removes contour data.
Omitted hole lists clear the known old holes.

Explicit profile editing promotes to Source2, Source3 for Polygon, or Source6 for
Contours, when safe; old opaque `collision.contours` fields across every definition
block6 promotion. Earlier source versions keep those fields opaque;
an already promoted source never downgrades through ordinary edits. It does not remint keys,
write Scene values or publish automatically. Undo/Redo and Save use the ordinary
asset-document methods. After publishing a Set, republish its dependent Maps
against that selected revision. Collision uses existing Static/Kinematic/Dynamic bodies,
sensors, cell flags and shape budgets. Static/Kinematic nonsensor custom one-way
polygons use original front-facing edges; decomposition diagonals have no response.
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

`forge_asset_document_tile_autotile` applies an `autotile` profile to a local
`tile` key in a Tile Set at its exact document session/revision. Enabled profiles
need 1..64 ordered rules; disabled profiles allow 0..64. Each rule has byte
`mask`/`same` bitsets and a full-width local `tile` key; `same` must be a subset
of `mask`. Bits clockwise in XY/up are N, NE, E, SE, S, SW, W, NW. Same means
the same logical tile ID in the same layer, adjusted for cell orientation.
Different includes empty cells. First match selects an existing definition;
fallback uses the root's own definition. Targets may be self or definitions
without enabled neighbor rules. Missing/image IDs and chains reject.
Safe Source5 promotion preserves identity, allocator, unknown profile/rule-position
siblings, animation and collision. Rendering and collision consume the selected
definition; raw cells, SDK queries and checkpoints retain logical IDs. Use the
ordinary Undo/Redo, Save and dependent-Map publication flow. Interactive neighbor,
animation and collision drafts share the document-busy guard. Read-only connections
advertise no neighbor mutation binding.

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
duplicate, `forge_prefab_variant_create`, source open/read/patch/publish/close,
refresh, and reviewed instance Apply. Source reads default to raw intent and
compact reviewed-base AssetId/revision/SHA summaries. `view: effective` selects the
inherited definition; `pointer` selects a JSON pointer within either view, and
`offset`/`limit` page a selected array (maximum128 rows). Selected values are
bounded to8MiB; use a narrower pointer/page when requested. The draft digest stays
the same across read views and changes after Rebase reviews different bases.
The effective view is the composed source definition; omitted supported built-in
fields use their discovered schema defaults, without adding authored intent.
`forge_prefab_source_revert` removes a selected
member's name intent, component intent, or one field (component required).
`forge_prefab_source_rebase` refreshes published sources and reviews updated bases
while retaining draft intent; a successful refresh may clear Scene history.
Source edits require the exact draft digest and preserve identity/publication
revision. Publication checks both the source baseline and reviewed base chain
against loaded values and disk. A variant Apply writes only its own intent;
its base remains unchanged. Apply review captures the existing owner's candidate; commit requires
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
its exact content digest (bounded64 MiB read). Restore/discard require that digest
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
cap is34 MiB because structured data is also included as text. Large scenes
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
a new session. Open-tab observations also return a one-based visual `cursor`
line/column, allowing clients to verify source navigation. Logical redirects into another owner, generated paths and external
save conflicts are rejected.

`forge_code_search_start` starts the source editor's single background literal
search with `query` (1..255 UTF-8 bytes, no line breaks), `match_case` (default true)
and `whole_word` (default false). It snapshots open tabs without moving focus or
saving. All three search tools use `code.read`, including scene-only/read-only
connections; they neither execute code nor mutate source/history/revisions.
Starting a fresh ephemeral job advertises `idempotentHint:false`; status and
named cancellation retain their idempotent hints. Discovery owns this metadata.
`forge_code_search_status` accepts `offset` and `limit` (1..128) and returns the
operation, busy state, options, total retained matches and a result page. Finished
results report `complete`, `cancelled`, `limited`, `skipped`, bounded errors and
scanned file/byte counts. Total means retained matches, never an unlimited count.
Each match exposes Native path, exact source `state`, one-based visual line/column,
UTF-8 byte offset/length, bounded excerpt and whether it came from an open tab.

`forge_code_search_cancel` requires that exact operation UUID; another job/project
returns `wrong_job`. Poll until idle before starting another search. Project
switches cancel/retire old work; results cannot publish into the new source owner.
The worker owns immutable text copies, project paths and cancellation only.
Discovery/reads/matching stay off the editor frame. Budgets are 2048 files,32 MiB,
512 matches,1 MiB per valid UTF-8/NUL-free file, and16 retained file diagnostics;
any cancellation/limit/skip is explicitly incomplete. Existing containment and
generated-path/symlink exclusions apply. Case folding is ASCII-only; whole-word
boundaries treat ASCII letters/digits/underscore and all non-ASCII bytes as word
content. There are no regexes or replacement operations.

With the existing `code.write` grant, `forge_code_open` additionally accepts
`line`, `column` and optional `state`. Pass the match's state to reject stale
navigation with `stale_source` before tab creation, automatic disk reload, focus
or cursor changes. Clean open tabs also compare their disk baseline; dirty tabs
retain their source draft/history. Search result navigation has ordinary scene
routing/revision/sequence requirements, without a Scene Undo or source edit.

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

Mesh Renderer `wind` is one tunable complete struct through `forge_play_tune`,
using the current launch/session/generation and `play.execute` grant. It changes
rendered geometry without moving authored transforms or physics. The status clock
includes derived `timing.presentation_seconds`; `forge_play_snapshot` copies
`simulation_seconds` when the actual world contains enabled wind or a runtime-capturing probe. Pause/Step,
runtime validation, pending acknowledgement and Keep Values retain their shared
owners. `mcp_mesh_wind_play_stdio` and `mcp_mesh_wind_sdk_stdio` exercise both
protocol framings, authoring/history/scatter, scoped native tuning/Step/review and
stale-generation rejection; the SDK fixture initializes ordinary Game defaults
and uses isolated test user data. These are local clients, without external-client
or graphical Windows acceptance.

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

`navigation_behavior_debug` is a separate copied observation: phase, graph,
state/name, elapsed, last_transition/count, tick, diagnostics and requested/retained
resource revisions. It does not expose a second mutable progress database.
`forge_play_tune` accepts `forge.navigation_behavior.enabled` through the same
navigation owner and grant/generation checks. Graph and EntityRef replacement
require authored editing after Stop. Source graph editing uses the shared source
session/revision/history/publication owner and remains locked while Play is active.

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
owners. Audio watch also admits a SoundCue entity. Event-only cues accept
`one_shot`, which consumes the next event choice. When `repeat_interval_seconds`
is configured, `play` resets choices and starts future periodic events;
`pause`/`resume` preserve their remaining interval and `stop` prevents future
events. Already-requested voices remain with their Audio Sources. Discovery
exposes the repeat interval/defaults/startup/game-pause values. Observation reports
producer state, remaining time, preparation, skipped events and retained errors.
`repeat_mode` has native choices0 Interval and1 AfterCompletion. Completion mode
uses a0..3600second gap and observes only its current event's Source-owned voices,
including tails; unrelated manual/other-cue voices do not block it. Observation
adds waiting/completed/pending/current-event voice counts. Source Stop can finish
an event while the producer continues; cue Stop prevents future events.
Discovery also exposes `repeat_event_limit`: unsigned0..65535, default0(unlimited).
Existing property commands author finite runs in either timing mode. Observation
reports issued scheduled events, finishing/finished state and this run's pending
requests/voices. Natural completion waits for all current-run Source voices/tails,
including overlapping interval events. Manual requests and shared bus tails remain
separate. Manual `one_shot` does not consume the finite count; `play`
resets it, and `stop` is cancellation rather than natural completion. Recovery
retains authored intent and starts fresh. No additional mutation grant or separate
authoring operation is introduced.
The same Scene/history, scoped Play/watch identity and permission checks apply;
these controls do not change authored values. Native output-time repetition works
outside fixed game ticks; stalled host service skips missed periods rather than
replaying a backlog. See [Audio](audio.md#repeating-sound-cue-events). Existing Scene creation/property commands author the complete
bounded graph, with nested integer choices and typed source references in discovery.
`forge_audio_playlist_create` composes 1–8 ordered registered clip references into
an ordinary completion-timed cue and child Sources in one Scene/history transaction.
Its `arguments` contain `clips:[{clip:"<AudioClip UUID>",name:"Opening"}]` and optional
`name`, `gap_seconds`, `loop`, `play_on_start` and `pause_with_game`. Discovery
derives this tool and schema directly from `audio.playlist_create`; read-only
connections do not advertise or permit it. Echo the current target, revision and
mutation sequence. The result's `results[0]` contains `selected` and `sources`.
Edit the published graph/Source properties with existing tools, then save the
Scene. The command grants no new filesystem or runtime control authority.
Source leaves expose default-zero `delay_seconds` with native bounds0..10. Cue
observations include `last_delay_seconds`; source observations include
`delayed_one_shots`. Existing source Pause/Resume/Stop controls apply while voices
wait. Timing uses output frames after preparation, independently of pitch, and
preserves applicable pause intervals through the same audio owner.
`profile_refresh` requests one snapshot without taking over the Profiler
window's watch. Its value includes `ecs`, the last requested/collected native fixed-pipeline plan, merge markers and cumulative native system CPU totals. Start collection and advance a fixed tick after requesting observations; Pause reads never run gameplay. Stop retains the plan, Clear invalidates its copy without resetting native lifetime counters, and replacement begins empty. The same bounded data feeds the Profiler's ECS systems view; native IDs are world-local diagnostic handles. UI commands go through the actual presenter's declared command
list, ID queue and acknowledgements, never DOM/script injection.

The copied `ecs.dependencies` observation follows bounded native DependsOn
lookups from the copied plan systems:512nodes,512links,8targets per source.
Nodes include plan membership, native phase tags and partial-target flags;
links retain native slot indices and owned-pair presence. Lookup may resolve
inherited targets. The legacy row `phase` names the first target; it does not
resolve an execution phase. The native plan remains the order observation.
These are diagnostic handles within the current profile's world/session scope,
not persistent game entities or a complete dependency/race analysis. Older
snapshots may omit `dependencies`. Refresh/collection/Clear use the same controls
and permissions as the rest of the profile; reads do not progress gameplay.

`ecs.worker_access` references declared fixed-source writes in configured
multi-threaded This-source systems with more than one actual stage. `writes`
contains at most128 `system`/`term` references into the existing copied plan;
`total_writes`, eligible system/term counts, unresolved terms and separate
term/write truncation flags describe coverage. `access.matches_this` preserves
the initialized query flag. Owned self fields, shared reads, serial/single-stage
execution and no-This queries are outside this particular check, not certified
race free. Traversal, complex/omitted declarations and custom callback/run side
effects require separate review. These are declarations, not measured writes,
simultaneous callbacks or race verdicts. Legacy snapshots may omit these fields.
Collection, scoped reads, Clear/Stop and replacement use the existing owners.

`forge_play_profile_trace` reads the cached isolated Play CPU profile as Trace Event
JSON under `play.read`; it does not execute gameplay or require an execution grant.
Start collection and request `profile_refresh` through the existing controls first.
Use `limit`1..128 (default 128) and `offset`0..512; `trace.otherData.measured_spans`
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

## UI output assignment

Discovery exposes the nullable typed `forge.ui_document.render_texture` field.
Use the existing component/property operations and `forge_property_set` to choose
a published Render Texture UUID or null for main. Normal Scene revision, atomic
validation, Undo/Redo, Prefab inheritance/overrides and save rules apply; no separate
agent-only route exists. Author the producer Camera and consumer Material through
the existing output workflow. Output UI is display-only: assigning it does not
route Mesh clicks, keyboard or focus. Runtime model values and explicit semantic
commands retain their existing scope. See [Runtime UI](runtime-ui.md).

## UI filter authoring

Use `forge_source_text_read/write` to edit RML/RCSS `filter` declarations or
`data-style-filter` bindings, then register/refresh through
`forge_source_ui_register`. Blur0..16 resolved pixels, opacity/grayscale0..1 and
brightness/contrast0..4 share native source/candidate admission. Drop-shadow uses
straight byte color, signed offsets -128..128 and sigma0..16 resolved pixels;
for example `drop-shadow(#20408080 8px 6px 2px)`. Chains cap at8
and active nesting at4. Invalid source/reload returns the ordinary diagnostic and
retains the preceding usable presentation. Source writes keep their Edit-only
file grant, exact disk digest, target/revision/sequence and no-Undo contract.

The existing `forge.ui_values` schema/property commands set saved named scalar
values; `forge_play_tune` changes them in the current launch through the ordinary
runtime tuning/review owner. RCSS filter animation stays on presentation time.
`forge_play_ui_reload` retains its current Play identity and execution permission.
Export uses the existing project export service and requires a matching filter-
capable runtime kit (profile7 for shadow panels/encoded inline styles, profile6 for luminance and dynamic mask/decorator bindings, profile5 for box shadows, profile4 for alpha masks, profile3 for backdrops, profile2
for shadows/ordinary dynamic filter bindings, profile1 for basic static filters;
newer known profiles preserve the preceding profiles). No separate agent filter
graph, DOM mutation or tool family
is introduced. See [Runtime UI](runtime-ui.md) for rendering/resource limits.

`backdrop-filter` and `data-style-backdrop-filter` use these same source, register,
model/tune and reload operations. No new tool schema or DOM mutation entry point
is required. Native candidate admission supplies the same kernel/numeric bounds,
clipping and shared layer budgets. Backdrops require kit profile3. See
[Runtime UI](runtime-ui.md) for ordering, transparency and output behavior.

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

`forge_navigation_status/build/cancel` and `forge_navigation_select_profile` use NavigationTools and its bounded static/tiled
bake worker. The native settings/profile and stale-geometry publication check
remain authoritative. Publication is separate from Scene Undo. Status includes a
Scene-scoped `profiles` mapping from names to ordinary Nav asset IDs. It also reports
`link_count` for the selected published asset. Author `forge.navigation_link`
through the existing component/property operations. Discovery exposes eleven
fields, named numeric Plane choices and static (nonanimatable) source policy.
Set local endpoints, radius, direction and exact profile with normal revision /
sequence/Undo semantics; build includes only enabled links in the matching plane
and profile. There is no separate agent-only link implementation. Runtime Play
snapshots include `navigation_debug.link` as null or `{source, entry, exit}`,
including held crossings. See [navigation](navigation.md#authored-links). Optional
settings string `profile` chooses the build identity; omission means `default`
and preserves legacy IDs. Names are case-sensitive 1–64 ASCII tokens matching
`[A-Za-z][A-Za-z0-9_-]*`. Rebuilding replaces only that profile's revision.
`forge_navigation_select_profile` takes `profile` and loads the saved view/settings
or creates an unpublished draft. It does not publish or change Scene history.
Select/build use the same assets permission, revision handling and Play lock.
For optional cross-profile steering, add `forge.navigation_avoidance` through the
ordinary component/property tools. `shared_group` defaults to0 (asset isolation),
accepts integers0–32 and uses normal revision-checked Undo/prefab persistence.
Equal nonzero groups also share peers across same-plane profiles, using each
peer's baked radius. This authored field is not live-tunable or animatable.
Its appended native value requires a matching SDK/module rebuild; C callbacks
remain unchanged.
Settings add optional
integer `plane`:0(default) uses3D NavigationSurface geometry;1 uses top-down XY
NavigationRegion2D rectangles or simple convex/concave polygon regions. The six numeric profile fields remain required. Optional boolean
`dynamic_obstacles` selects the version 4 tiled profile; omitted/false retains
static baking. Optional boolean `collect_static_colliders` defaults false and
`collision_layers` defaults4294967295, admitting integers0–4294967295. Collection
selects enabled Static nonsensor bodies in the chosen plane by their Layer. An
explicit marker in that plane overrides collection, including disabled exclusions.
XY still requires explicit walkable regions and collects physical obstacles;3D
uses actual physical shapes. Create/edit bodies and colliders through ordinary
component/property operations, with the same validation, revision, Undo and prefab
rules as Inspector. Publish referenced Collision/Map inputs before baking. Collection
uses envelope7, composing with dynamic layers and optional authored links; it adds
no separate MCP workflow or runtime rebake. Geometry provenance is2(spatial),
3(authored XY regions) or5(XY collision sources). Create/edit
`forge.navigation_obstacle` through the same component/property tools: enabled,
radius(0.05–5) and height(0.2–10, 3D only). The component uses ordinary validation,
revision-checked history, prefab/Scene persistence and saving. It has no separate
agent-only shape store or live-tuning operation. Runtime routes observe resolved
poses at the writable navigation boundary. See [Navigation](navigation.md) for
upright/circular transform requirements and layer/obstacle limits.
Create/edit regions with ordinary component/property tools, including validation,
Undo/Redo and Scene Save. Discovery exposes `points` as a vector with maximum64,
default`[]` and structured finite local`x/y` coordinates. Empty points selects the
width/height rectangle. To seed a polygon, set the complete field to four valid
corners; then replace the complete3–64point CCW contour as needed. Invalid count,
crossing/touching/collinear or out-of-bounds candidates leave Scene/revision intact.
Point extension data, prefab overrides and Undo/Redo use existing reflected owners.
Discovery exposes `source` as0 **Rectangle / Points** (default) or1 **Collider 2D**,
with ordinary property validation/history. Source1 selects exactly one configured
Box/Circle/Capsule or published Tile Map Collider on the marked static entity. For
a Map, assign its Renderer Map and stable Collider layer through the existing
properties. XY collision builds publish geometry5/catalog5 (dynamic envelope4)
with separate selected `tile_inputs` and dependency edges. Other XY builds retain
guarded schema3. Both use the same
worker/job/status/cancel owner. One logical asset per Scene remains authoritative;
switching plane preserves its ID and requires rebuilding. No independent agent-only
geometry, bake or persistence path exists. Scene outline
selection/framing and centered resize handles are editor conveniences over the same
qualified width/height fields in rectangle mode; polygon outlines/framing use the
complete contour, with no rectangle resize handles. Agents continue to use ordinary property tools and
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

`forge_capture_start` requests the next rendered Scene/Game image: **.png**, lossless **.webp** or opaque **.jpg/.jpeg** inside the granted output root. Existing files
require `replace:true`. `capture_status` reports the native image task and completed `file_mime` (`image/png`, `image/webp` or `image/jpeg`). Its ready
`capture_image` returns a bounded PNG **image content block** plus metadata; base64
is not repeated in text or structured JSON. The preview fits1024 pixels, while the
full PNG/WebP file retains rendered dimensions and exact captured RGBA, including transparent RGB. Only the task's exact current ready operation
is readable. `capture_preview` prepares a completed owned PNG/WebP/JPEG or PNG sequence frame;
there is no arbitrary filesystem image-read tool.

`forge_capture_texture` exports a published Texture asset's stored pixels through
that same image task. Supply `asset` and an output-root-relative `path`, with
optional `semantic` (`automatic`, `color`, `data`, `normal`, `hdr`), `mip`0..31,
`layer`/`depth`0..2047, `face`0..5 and `replace`. Actual dimensions and variants
are validated by the selected-artifact owner; syntactically bounded unavailable
selections fail without replacing a good image. Automatic prefers HDR color then
color; it never substitutes data or normal. The profile admits uncompressed
R8/RG8/RGBA8/sRGB byte storage, a complete128MiB selected texture and a4096-axis /
64MiB RGBA image. PNG preserves the stored sRGB label; WebP requires sRGB storage.
Missing G/B are0 and missingA255. Present bytes, including alpha/hidden RGB,
remain exact, with no preview color/alpha/normal conversion or import metadata.

UNORM BC1/2/3/4/5/7 and their current sRGB forms decode through the packaged
asset worker using one selected immutable block plane. Complete selected Texture
admission is128 MiB, decoded RGBA8 is64 MiB, each axis4096. PNG preserves the
format color label; WebP requires sRGB and JPEG requires actual opaque sRGB.
Status reports component_bits8 and texture.profile decoded_rgba8. These are
reference decoded texels, not original pre-compression samples or compressed
payload. BC1 retains punch-through even with an opaque hint; BC4/5 supply absent
G/B zero and A255 without normal-Z reconstruction. BC6 uses the HDR profile below.
Owned current/later previews and all output/revision/replay rules remain the same.

BC4/BC5 SNORM uses the same Texture operation with a PNG destination. Choose its
published semantic explicitly where required (`semantic:"data"` for the signed
Gallery sources). The producer maps stored signed channels with
round-half-up((s+1)*65535/2), emits unlabelled PNG16 with absentG/B0,A65535 and
reports component_bits16, texture.profile decoded_snorm_rgba16. Its owned preview
quantizes the mapped samples to8-bit. Full selected source and detached RGBA16
caps are128MiB, encoded PNG136MiB and each axis4096. Non-PNG and ambiguous ordered
endpoints -127/-128 reject before file replacement; preview controls and normal-Z
reconstruction do not affect the result.

Normalized R16/RG16/RGBA16 selections also use `.png`, retaining exact16-bit
samples in RGBA file layout without an invented sRGB label. MissingG/B are0 and
missingA65535; RG16 green is not alpha. The selected source limit is256MiB, detached
RGBA16 128MiB, extent4096axis and encoded PNG136MiB. Other format destinations
reject without overwriting a good file. Status reports `component_bits:16` and
completed `texture.profile:"stored_rgba16"`; completed byte files report8 and
stored_rgba8, while pending selection metadata uses stored_samples. Bounded PNG
image content is8-bit, using nearest channel quantization and existing1024-axis
sizing. The saved PNG preserves16-bit precision; its preview does not. Current and
later owned-file previews share quantization. Original sources remain unused and
the same output/revision/replay/overwrite/Undo boundaries apply.

For JPEG still output, use `.jpg/.jpeg` and optional `jpeg_quality`1..100
(default90). Both `forge_capture_start` and `forge_capture_texture` accept this
option only for JPEG destinations; other combinations reject before starting work.
The actual byte image must be sRGB and have alpha255 throughout. Transparent,
unlabelled data/normal and float HDR selections reject without replacing a good
file. Output limits remain4096axis/64MiB RGBA/68MiB encoded. JPEG is lossy even at
quality100. Status reports `jpeg_quality` and `file_mime:"image/jpeg"`; bounded
PNG previews represent decoded written JPEG colors. Output grants, exact replay,
revision/sequence and explicit overwrite rules are unchanged.

The result reports `view:"texture"` and the owned requested asset/semantic/slice
under `texture`. Poll capture status for the written file path and MIME; image
inspection uses the existing image/preview operations. No file-download method is
advertised. The published
catalog/cache is required; source images are not read or rebuilt. Standalone and
model-member selections use the existing CPU resource admission. This operation
requires the output grant and normal target/revision/sequence guards, uses no Scene
Undo, and is absent for read-only connections. Pending project changes cancel;
a job already committing finishes. Existing view/sequence requests cannot overlap.

For `.hdr`, the same tool selects the linear RGBE output profile. It admits HdrColor
RGBA16Float/RGBA32Float, finite/nonnegative RGB and actual selected alpha1;
`semantic:"hdr"` selects that variant explicitly. The complete selected source limit
is256MiB, detached RGB floats192MiB, extent4096axis and encoded68MiB. Shared-
exponent RGBE quantization is not lossless, alpha is absent, and no tone mapping or
exposure is applied. Native working primaries/D65 are explicit in the file.
Status reports `file_mime:"image/vnd.radiance"` and requested
`texture.profile:"linear_rgbe"`. HdrColor BC6Unsigned/BC6Signed instead decodes
through the same isolated asset worker's fixed `bc6-rgba16f-v1` profile, then
reuses the existing finite/nonnegative/alpha1 HDR projection and writer. Completed
status identifies `texture.profile:"decoded_bc6_linear_rgbe"`; `component_bits`
is absent for RGBE. Raw half-float output is bounded128MiB/136MiB transport;
complete selected source256MiB, linear RGB192MiB,4096axes and HDR68MiB remain.
Negative colors and negative infinity reject without replacing a good file;
PNG/WebP/JPEG do not accept BC6 in this profile. Native source blocks remain
unchanged and editable source files may be absent. Selectors, scoped permissions,
revision checks, replay, cancellation and file publication retain their owners.
This is reference decoding followed by RGBE quantization, not BC6 encoding or
recovery of original pre-compression pixels. Requested current and later `capture.image`
inspection returns bounded sRGB PNG8 of the actual quantized written RGBE, labeled
`preview_profile:"hdr_reinhard_srgb"` in both status and image metadata. Fixed unit
exposure applies per-channel c/(1+c), then standard sRGB encoding and nearest8-bit
samples before existing1024-axis preview sizing. This is a display inspection;
full HDR remains linear RGBE and is not lossless float data. It does not use native
Texture-viewer or Scene exposure/processing. The owned reader accepts only FORGE's
fixed-primary writer profile with its flat/literal packets,68 MiB encoded,
4096-axis/64 MiB RGBA8 bounds. Unsupported/malformed previews keep the completed
artifact and a separate diagnostic. No arbitrary HDR source-reader tool is added.
`capture.image` requires this exact ready operation and never substitutes an older
PNG. Source images remain unused; source/subresource/output errors,
old-file retention and the usual guards share the same native task. Scene/Game
capture requests with an HDR suffix reject; their display pixels are tone-mapped.

Skeletal sequence authoring uses the ordinary Scene component schema and
`forge_property_set_many` on `forge.animator`: `sequence` is a typed controller
reference; `sequence_start_seconds`, `sequence_next_clip` and
`sequence_next_start_seconds` arrange one direct clip and an optional compatible
next clip. The asset type is `animation_clip`. Complete candidates use existing
revision, permission, Scene history and save rules. States, Blend Spaces, Graphs
and standalone blend selections cannot share that Animator's bound profile.
Published retargeted clips require the same target skeleton/model binding.

Both `forge.animator` and `forge.transform_animator` expose `sequence_extra_clips`
as a vector (default empty, maximum six). Its element schema describes typed
`clip` references and finite `start_seconds`. With the original primary/next pair,
this permits eight placements. Set the list through `forge_property_set_many` with
other required binding fields in one candidate; it uses the same revision, replay,
permission, Undo/Redo and document Save owners. Clearing the sequence or next clip
requires clearing this list in the same transaction. Source admission validates
references/count/ordered starts; resource preparation additionally validates every
clip's channels, target/skeleton/model compatibility and ordered ends. Default mode
admits two adjacent contributions; `sequence_multiway:true` opts into up to eight. A valid source list is not proof of runtime admission.

For default-mode admitted nonempty lists, `sequence_clips.clips` describes the complete ordered
list, with zero-based `active_primary` and `active_next`. Existing public clip/blend
identities and weight describe that active pair; track time/duration describe the
whole span. Mixed holders retain typed `players` observations. The parent controller
still owns Play operations; there is no independent placement clock or mutation
endpoint. See [bounded collections](transform-animation.md#bounded-clip-collections).

For multiway clip overlaps, set Boolean `sequence_multiway` through the same
player schema, optionally with starts/list edits in one `forge_property_set_many`.
Its default is false; wrong types reject without revision/history changes. Save
and Prefab operations preserve it through the existing Scene owner. Resource
admission must still validate the complete selected clip set.

An enabled nonempty collection's watch adds `multiway:true` and `summary_only`;
each ordered `sequence_clips.clips` entry includes local `time_seconds` and a
normalized `weight`. More than two positive contributors make the legacy
clip/next/weight fields a dominant/runner-up summary, with relative pair influence.
Use the complete list to inspect the actual blend. The read-only Inspector names
that limit explicitly. Generic source and Play tools cover this workflow without
a separate agent-owned animation implementation. Exact flag recovery15 and the
matching runtime-kit requirement apply; see [layered overlap semantics](transform-animation.md#layered-clip-overlaps).

Nested controllers use `forge_property_set_many` on `forge.animation_sequence`:
`sequence` selects the parent, `sequence_start_seconds` is the start in parent
seconds and `sequence_rate` is local seconds per parent second (default 1,
finite 0.25–4). Rates multiply through the active chain within 1/64–64. The root owns
play/pause/seek; stored child playback_speed/loop/play_on_start stay separate.
`sequence_loop` is a separate default-false boolean: true repeats the child's local
duration through the remaining parent span and resets descendants with it. Set it
with start/rate through the same `forge_property_set_many` transaction; clearing
the parent retains all placement intent for later reuse. Root controllers ignore
their stored placement-loop flag. Admission composes at most 256 root-time spans
per controller, including inherited repeats; excessive geometry rejects preparation
without replacing the admitted candidate. Sound points retain the 128-event
all-or-none tick budget, and skeletal root motion integrates active spans without
adding reset jumps. Controller watch `loop` reports placement looping; child controls
remain read only. These are existing fields/operations, with no new MCP endpoint.
Generic discovery, Scene permission/revision, replay, history and Save apply.
Invalid local rates reject without mutation; invalid/cyclic/composed bindings
reject preparation through the same runtime owner. Watch speed includes the
cumulative rate. `sequence_extra_placements` adds up to seven entries after the
primary, each with `start_seconds`, `rate` and `loop`. Starts increase strictly;
finite placements cannot overlap, and a loop ends at the next placement or parent
end. Edit primary and extra values together through `forge_property_set_many` to
validate one complete configuration and one history change. Empty arrays preserve
the earlier single-placement profile; detaching keeps the stored list.
A repeated controller watch includes read-only `sequence_placements` with its
ordered list, selected positional `active_placement` and active-window flag. No
new tool, clock or authoring owner is added. See
[nested timing](transform-animation.md#nested-controller-placements).

Watch the controller before using scoped `forge_play_animation_control` to seek,
pause or restart it. Bound players reject independent control. A model watch via
`forge_play_animation` reports its admitted `sequence` and `sequence_clips`; on a
mixed property/skeletal holder, `component` identifies the entity-wide primary
observation and `players` includes separate `forge.animator` and
`forge.transform_animator` values. Each reports its own placed time, span and derived
weight. Existing C++/SDK queries keep transform-first priority. These are copied
observations of the same native owners, not an additional authoring operation.

Timeline frame export uses `capture_sequence_open/read/start`, the actual sequence
controller and observed source revision/preview generation. Start chooses a new
folder and native end-exclusive frame plan; fixed dimensions render real pixels.
The existing task owns sampling, resource readiness, IO, cancellation, cursor
restoration and `frames.json`. Completed/cancelled frames stay available. A single
readback/file write already committing must finish rather than promise rollback.

The optional `format:"webp"` on `capture_sequence_start` adds `animation.webp`
beside retained PNG frames; the default is `"png"`. Schema enum, validation and
native format choice use the same task/plan/output owners. WebP requires2..4096frames
and the packaged asset worker. `sequence.format` identifies the request; state
`encoding` begins after Timeline restoration. Success adds `sequence.animation`
with file path, MIME, frames, bytes, duration_ms, loop_count0 and
profile`lossless-rgba8-full-frames-v1`. The same fields use a relative file in
`frames.json`. PNG frame image inspection remains available; no animated-image
block or arbitrary-file read authority is added.

`format:"avi"` uses that same task for a silent Motion JPEG movie. Optional integer
`jpeg_quality` is1–100/default90 and rejects for other sequence formats. AVI supports
1–4096 opaque sRGB frames and FPS0.001–240 with a bounded reduced rational; snapshots
cap256MiB aggregate and AVI output256MiB. State and receipt use the shared encoded
metadata owner: file/MIME/bytes/frames/profile plus codec `mjpeg`, `audio:false`, rate,
scale, duration_seconds and jpeg_quality. No loop directive or movie image block is
invented. Existing source revision/generation, output grants, cancellation, retained
PNGs and preview restoration apply. See [the exact profile](sequence-movie-export.md).

Fresh game recording uses `capture_game_read/start`. Read the current `scene`,
`source_revision` and `document_generation`, then supply all three to Start with
a NEW output-root-relative `path`, `frames_per_second`, `frame_count`, `width`
and `height`; `jpeg_quality` defaults to90. The method requires both execution
and output grants and advertises `openWorldHint:true` because Project Code runs.
It starts its own fresh exact-SDK Play process and private UserData, captures the
Game image, then mixes and advances through existing runtime owners. FPS1–240,
1–4096frames, PCM128MiB and combined snapshots/movie256MiB apply. Input, Pause,
scene changes and restoration of an existing live C++ world are unavailable.

Poll `capture_status.game_recording` until retirement. Success retains PNGs,
`audio.s16le`, `recording.json` and `game.avi` with MJPEG/48kHz stereo PCM16.
Cancellation needs the active `operation` and output grant. For a completed PNG,
use the stable `artifact_operation` plus `frame` with `capture_preview`, then
read the new preview `operation` with `capture_image`. Repeated frame previews
retain the same artifact token. Source edits abort without rolling back those
edits, and recording creates no Scene Undo entry. Read-only sessions can discover
the source and limits but cannot start recording. See [game recording](game-recording.md).

The fixed profile is lossless sRGB RGBA8 full-canvas/no-blend/no-disposal,
infinite-loop, cumulative-rounded integer milliseconds. Scene sample times remain
end-exclusive.2..4096frames/FPS>0..240, duration1..16777215ms each and total
<=2147483647ms; snapshots256MiB/per-file68MiB, compressed frames64MiB/output68MiB,
worker1GiB/120s. Native and MCP share invalid-profile, cancellation and failed
receipt behavior. Existing output grant, new-folder, revision, replay and no-Undo
rules remain. See[editor image ownership](editor-ui.md) and the
[user workflow](../manual/editor/animation-sequences.md).

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
| View images, stored Texture pixels and sampled frames | Owned output, real image blocks, selected artifacts and native sampling/readback/IO | ViewImageTask / Timeline / presentation |
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

`mcp_sprite_atlas_stdio` scopes the actual modern/legacy transport to image import,
atlas template/create/patch/history/publication, maximum uint32 keys and retained
allocation highwater. It uses real editor and fixed worker owners with asset
read/write grants; read-only discovery excludes mutations.

`mcp_source_stdio` exercises all 16 real source editor owners, graph schema/port
discovery, document sessions/revisions, source Undo/Redo, failed identity edits,
copy-on-write tile painting, saved-state dirtiness, native publication, typed
import setting atomicity, source disk conflicts, prefab source publication and
recoverable two-file Apply Undo/Redo through both MCP protocol envelopes. It
publishes Material, Collision, Editable Mesh, Particle Effect, Terrain, Tile Set,
Sprite atlas, transform-animation, Navigation Behavior and image/WAV/FLAC/MP3 candidates through their real owners. Behavior checks include valid patrol rows,
invalid destinations/conditions/identities, monotonic allocation through Undo and
native publication. Rig-dependent
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

`mcp_image_stdio` exercises the real image-task IO, PNG/lossless WebP encoding, exact PNG previews,
MCP image blocks and sequence receipts with detached recorded pixels and a
recording frame source. It checks format/MIME consistency, replacement, owner/path/operation guards, cancellation and
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
`device` token and `slot` 0..16, plus normal document target/expected revision and
mutation sequence. It needs the existing execution grant, validates project
ownership and delegates to GameInput/SdlGamepads. Stale topology/connection,
invalid slot, full neutral-input queue or failed native Stop reject without
changing assignments. Only old/new slots' input and feedback are neutralized;
the operation does not invoke the global input-release preparation used by input
injection. This is transient host routing, without authored Undo or saved tokens.
Read discovery for `pad.1.*` through `pad.16.*` controls; `pad.*` remains available
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

Audio settings also expose `playback_storage` through
`forge_asset_settings_schema` with exact string choices `resident` and `stream`.
Use `forge_asset_settings_edit` with `editor:"audio_import"` and current
session/state, then `forge_asset_settings_publish`. Stream requires one second
after trimming, cooks48000Hz PCM, and retains the ordinary import size bounds.
Invalid choices preserve the draft. Publication updates ordinary asset discovery
and metadata; it does not change Scene Undo. The same Play/source control tools
handle pending streams, restart, pause/resume, one-shots and live values. Audio
status separates resident payload, stream pages, temporary disk backing and
reserved/preparing/retiring counts. Eight native stream slots include pending,
stopped and retiring voices. Export requires the matching streaming-capable kit.
See [Audio](audio.md#bounded-cooked-pcm-streaming) for ownership and limits.

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

Source reverb also uses the registered Audio Source fields and the same
`forge_property_set_many` / `forge_play_tune` paths: `reverb_enabled`,
`reverb_decay` (0.1–10 seconds), `reverb_damping` (0–0.95) and `reverb_wet` (0–1).
The existing complete-component admission rejects invalid values and preserves
the previous authored/runtime state. Actor identity, permissions, revision/replay,
Undo/save and unchanged authored-Scene rules remain shared; no reverb-specific
mutation implementation is added. Live observations include admitted settings,
`effects_tail(s)`, `reverb_tail(s)` and derived `reverb_buffer_bytes`, including
one-shots. Source/category gain applies after effects so mute covers retained
tails while their time continues. Effect-parameter edits clear source effect
history; Source Pause/Resume retains it and Stop/Restart uses the normal lifecycle.

### Custom audio buses and live faders

Use `forge_project_settings_read/edit/save` with the observed settings digest to
author `game.audio.routing`. The shared audio value owner validates version1,
at most16 stable-key definitions, finite faders, target resolution, up to3
post-fader sends per bus, cycles, duplicate targets and potential path gain16.
Project topology is not a user-settings override. Discovery describes the actual
fields and limits; no agent-specific graph or persistence owner exists.

Each bus accepts an optional `effects` object. Its fields are
`high_pass_enabled` / `high_pass_hz` and `low_pass_enabled` / `low_pass_hz`
(20..20000 Hz), `echo_enabled` / `echo_seconds` (.01..1) / `echo_feedback`
(0...75), and `reverb_enabled` / `reverb_decay` (.1..10) / `reverb_damping`
(0...95) / `reverb_wet` (0..1). Switches require booleans; numeric parameters
are finite and bounded even while disabled. Unsupported fields reject before
draft publication, preserving the observed settings digest. Empty/default effect
settings remain bypassed; an explicit block still requires the current runtime kit
because older strict readers reject the field. Save settings in Edit mode, then start Play. Effects and
topology cannot be changed by a live fader request; the usual settings permission,
stale-baseline and document boundaries remain in force.

`forge_play_audio` includes `bus_processing`, `bus_effect_chains`,
`bus_echo_buffer_bytes` and `bus_reverb_buffer_bytes` in the copied audio value.
Global effect memory includes the source and shared bus chains. Bus effects mix
within the source's game-pause domain. Source controls change future input;
shared history and cue completion keep their respective existing owners.

`forge_property_set` / `forge_property_set_many` author AudioSource `custom_bus`
through ordinary revision, history and Prefab rules. Empty retains stock `bus`;
nonempty binds a project key. `forge_play_tune` checks live custom keys before
mutation. `forge_play_watch` and `forge_play_audio` report the admitted stock/
custom source selection, project routing and current `bus_volumes`.

`forge_play_audio_mix` takes `values` with `master_volume`, three
`channel_volumes`, and optional `bus_volumes` (object of key→finite0..1).
Omission preserves custom faders; an explicit map must exactly match the admitted
project key set. Existing Play permission, launch/session/generation, revision,
sequence and single-command readiness checks remain required. Rejected maps
preserve the healthy mix and report `mix_error`; valid retries remain available.
These controls do not save project defaults or player preferences and do not
change paused game time. See [audio routing](audio.md#custom-audio-routing-and-sends).

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
`allow_rotation` default false, and `byte_limit` defaults64 MiB
(at most 64 MiB). Only independent published sRGB RGBA8 Color textures are supported;
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
Invalid candidates leave the revision unchanged. Live Play tuning admits the
scalar controls, including `diffuse_intensity` and capture settings, through `play.tune` /
`forge_play_tune`; texture assignment remains Edit-only.
Use discovered tools/fields for the current connection and permissions.
`diffuse_intensity` defaults to zero and controls local base diffuse light independently
of specular `intensity`. Both use the existing typed source/history and live tuning
owners; no extra tool is required. See [the lighting contract](local-diffuse-irradiance.md).

Optional `runtime_capture:true` enables view-local GPU capture without importing
or writing source files. The same `forge_property_set` / `forge_play_tune` fields
are `capture_size` (16/32/64/128/256), `capture_interval` (0..3600seconds),
`capture_near`, `capture_far` (0<near<far<=1000000), and `capture_layers` (uint32).
At most two runtime probes/eight total are supported. Zero interval refreshes each
render; positive intervals use simulation time. Exclude camera-output readers from
the capture layers. Existing saved textures provide fallback; runtime captures
ignore saved-map yaw and recreate on each run. Discovery exposes current defaults,
bounds and descriptions. There is no separate capture-management tool or persisted
runtime image. See [the runtime contract](local-reflection-probes.md#runtime-gpu-capture).

`capture.reflection.start` / `forge_capture_reflection_start` requires the project
asset grant and Edit mode, with the ordinary target/expected revision/sequence
routing. `probe` is required. New-map capture requires `destination` (a NEW folder
below Assets with an existing parent). Alternatively `update_assigned:true` targets
the probe's existing independent imported capture DDS source; omit `destination`
in that mode. Optional fields are `size` (16–1024, default256),
`near_plane` (positive, default0.1), `far_plane` (greater than near, default 100),
`layers` (uint32, default all bits) and `assign` (default true). No external output
folder is needed. This uses the same image-task owner as PNG/sequence capture and
cannot overlap those operations.

Poll `forge_capture_status`; its `reflection` value reports phase, face count,
original revision/generation, destination, cancellation and the eventual Texture
`asset`. When `import_batch` appears, use `forge_asset_import_review`, then
`forge_asset_import_commit` with that exact `batch` and `review` and `import:true`.
No files/identities publish during rendering/review. Source commit, import and Scene
assignment have distinct outcomes. If the original Scene/document revision is
still current and `assign` is true, publication assigns the map and resets its yaw
to zero in one Scene Undo step. Otherwise a new map remains unassigned; a rebake
reports `updated` and retains existing Texture references without changing probe values.
Save after assignment. Read per-file import diagnostics and close the settled
batch through `forge_asset_import_close`.

Update review includes `source_revision_asset`, target/source/settings/catalog
revisions in its exact review digest, and generated metadata identifying the
updated Texture. Commit atomically replaces only that source and reimports with
its saved settings and AssetId. Every existing consumer retains its reference.
Stale source/settings/catalog and shared-source families reject. `import:false`
reports source-only completion for this batch. Closing the review resumes normal
automatic updates for registered sources, which can schedule a reimport. Failed
later reimport retains last-good cooked content. A separate `source_commit_diagnostic` reports a completed source write
with a durability warning without changing its successful source-only state. A committed source is not reverted by Scene Undo or cancellation after
commit. Reimport before another rebake if the source has unpublished changes.

`forge_capture_cancel` requires this task's exact operation UUID and the grant for
its workflow: project assets for reflection maps, output access for PNG/sequence
images. One grant cannot cancel the other workflow. It drains
accepted GPU/encoding/import work and prevents automatic assignment; already
copied sources/published assets remain. Asset import cancel/close tools keep their
existing batch guards. Cancellation/review controls can service their pending
owner; a new capture still requires normal admission. Runtime recapture is not
available. See
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

`mcp_application_states_stdio` scopes source validation/history/publication to the
new state owner. `mcp_application_states_play_stdio` exercises the real editor's
state discovery, paused controls, computed/stale/launch/gesture rejection and
runtime-only isolation through both MCP formats. Native SDK and software renderer
fixtures provide separate evidence; these MCP checks do not assert Windows execution.

## Authored input messages

Discovery includes optional Gameplay component `forge.input_message` with shared
`enabled`, `action` UUID-text, `payload` component-key and unsigned `trigger` fields
(0Pressed/default,1Released,2Changed). Discovery supplies the trigger range and
legacy missing-field default. Configure it with
`forge_component_add` and `forge_property_set_many`; remove its owned value with
`forge_component_revert`. Normal permissions, target/revision, replay, Undo/Redo,
Prefab and document-save semantics apply. Existing project-settings tools own
the input map. Pressed/Released use digital fixed-context actions; Changed also
accepts fixed Axis1/Axis2. Use an attached builtin or inspected opted-in project component; there is no separate agent payload store.

`forge_play_input_submit` exercises the existing input owner. `forge_play_input`
returns its cached `value.messages` observation, including tick, emitted count,
overflow, per-source trigger and diagnostics. The normal Play launch/session/world
correlation still applies. Sent means native dispatch, not proof of a receiver.
Source-code creation/build/inspection supplies C++ receivers through the existing
workflow. See [the dispatch and lifecycle contract](input-messages.md).

`forge_code_create(kind="system", name="InputMessages")` omits the optional
component template and creates an empty managed startup registration. A supplied
component name retains the existing component-header check and rotation template.
The same helper drives Content's None (empty registration) choice. Replace the
new source through normal guarded code-edit/save/build operations with the Gallery
receiver; omission does not create a native event bus or gameplay behavior. Managed
source provenance reports an empty component string for this registration.

## Smooth Camera Shot views

Discovery exposes native `forge.camera_shot.blend_in_seconds` with an optional
zero default and seconds metadata. Existing `forge_property_set` and atomic property
batches author it; the ordinary history tools Undo/Redo the Scene value. Camera
and sequence bindings remain typed EntityRefs. The animation provider validates
nonoverlapping intervals and matching adjacent camera composition before playback.
No additional MCP director or separate Scene implementation exists.

`forge_play_tune` accepts only an admitted enabled shot's `blend_in_seconds` in
Play, using the current launch/session/generation scope. Poll `forge_play_status`
for the tuning acknowledgement; failed duration/profile validation leaves the
runtime value unchanged. Camera, sequence, interval and enabled state remain Edit
operations. Paused `forge_play_animation_control` seeking updates the copied view
without a simulation tick. Runtime tuning capture/read/apply can review the duration
and apply it with the ordinary Scene history owner. Stopping Play preserves source.

Both current and legacy stdio framing use the same native descriptor and operation.
See [camera-shot behavior](transform-animation.md#sequence-camera-shots) and the
[manual workflow](../manual/editor/animation-sequences.md#camera-shots).

Box shadows use the existing source-write/register, named UiValues, guarded
live-tuning, reload and export operations. Static/model-bound box-shadow declarations
require filter5; blur radius resolves to native sigma at half its value and must
stay0..32. Native box-shadow keyframes/transitions reject during candidate
preparation and preserve the last usable UI. Existing source permissions,
revision/digest/replay, Edit-only write and Play identity rules apply. No agent
shadow graph or parallel authoring API is introduced.

`forge_scene_rendering_set` includes `morphological_anti_aliasing`:
`enabled` Boolean, `edge_threshold` finite 0.01..0.5, `search_steps` integer 1..32,
`corner_rounding` finite 0..1. Defaults false/0.1/16/0. Source values are checked
before float narrowing. Enable only one spatial method; switch explicitly by
patching the other method's `enabled:false` in the same operation. Conflicting
methods, bad bounds/types or stale revisions preserve Scene/revision. Existing
permissions, receipt replay, partial-patch extension retention, Undo/Redo, Save,
Play locks and export owners apply; no additional MCP tool is introduced.
See [the rendering contract](morphological-antialiasing.md).

`forge_scene_rendering_set` exposes optional `occlusion_culling` with strict
boolean `enabled` and finite numeric `depth_bias` from 0 to 0.01. Discovery, the MCP
input schema and native `scene.rendering.set` share the same owner. The operation
preserves existing revision checks, permissions, receipt replay, partial patches,
Undo/Redo, Play locks and Save. `mcp_occlusion_culling_stdio` covers both framing
formats and direct read-only denial. See [the rendering contract](occlusion-culling.md).


### Editable texture pixels

`forge_asset_paint_texture_catalog` describes the bounded source, brush math and
copy limits. `forge_asset_document_types` advertises `paint_texture` when its
actual editor is registered. Create with a `.paint-texture.json` project path and
optional `canvas={width,height,semantic,background}`; dimensions are1..1024,
semantic is `color` or `data`, and background is four byte channels. Defaults are
256×256transparent Color. Template creation only returns metadata and a preview
UUID; ordinary source creation allocates the actual identity.

`forge_asset_document_read` returns metadata for this typed source. Query up to
4096exact stored pixels with `forge_asset_document_pixels` and `points=[[x,y],…]`.
Queries carry the document session; mutating operations also carry its revision.
Metadata patch preserves the pixel pages and cannot change the canvas or identity.

`forge_asset_document_stroke` accepts1..256pixel-coordinate pairs and
`brush={color:[r,g,b,a],radius,hardness,opacity,erase}`. Color is required and its
channels lie in0..1; omitted controls use radius16,hardness0.75,opacity1,erasefalse.
Color RGB is linear, Data channels raw. Pixel centers are atx+0.5,y+0.5 from the
top left. Continuous path overlap uses maximum coverage per stroke.
Optional `breaks=[index,…]` starts disconnected paths at strictly increasing
zero-based point indices from1to point-count-minus1. All paths share maximum
coverage and one Undo entry; no capsule crosses a break.

`forge_asset_mesh_triangles` reads up to128published object-space triangles from
an engine, root or imported-model Mesh. Pass `mesh`, optional `revision`, `lod`,
`part`, `offset` and `limit`. Results include the selected revision, part/LOD counts,
triangle count and paged `primitive`, vertex indices, positions and float2 `uv0`
arrays. Missing/unsupported UV0 returns an empty UV array. A supplied stale revision
rejects; no Scene/world state changes. Geometry decoding is bounded to32MiB. These
are undeformed publication coordinates, without Scene visibility or transforms.

`forge_asset_document_mesh_stroke` prepares a texture stroke from explicit Mesh
triangle samples. It requires the open paint document's session/revision, `mesh`,
`material`, exact `mesh_revision`/`material_revision`, `samples` and the same
`brush`. Read revisions through `forge_asset_read` (engine `revision` or
`metadata.forge.import.key`); page triangles before choosing samples. Optional
`lod` defaults to0 and `slot` to`baseColorTexture`. Each sample has `primitive`,
`barycentric:[a,b,c]` summing to1, optional `part` (default0) and `break_before`.
It maps the selected published Material slot to the open canvas, sharing the
native Scene tool's Material/UV mapper, disconnected paths, asynchronous brush
and source/history/publication owners. Native pointer input uses actual visible
geometry; explicit samples do not imply Scene occlusion. The profile requires
opaque, two-sided, depth-tested/writing built-in Materials, matching Color/Data2D
paint-image bindings, nondegenerate unit UV0 and identity/full-axis flips. Invalid
revisions, mappings or samples report a preparation diagnostic without replacing
the draft or adding history. The existing read and asset-write grants apply.

`forge_asset_document_copy_pixels` accepts a published Texture UUID in `texture`.
Its base image must match the canvas's dimensions and Color/Data usage. Normalized
byte and unsigned LDR block samples are supported; signed/HDR, higher precision,
layered/volume/cube and premultiplied/unknown Color-alpha selections reject.
Linear Color bytes encode to sRGB for the destination. Originals stay unchanged.

Pixel strokes, mesh strokes and copy prepare asynchronously. The initial receipt reports
`transient_draft=true`; poll `forge_asset_document_status` until settled, inspect
`diagnostic`, and refresh the document revision. An identical outer request retry
returns its receipt and cannot apply the operation twice. Cancellation uses
`forge_asset_document_cancel_pixels`; it discards pending work without history.
`forge_asset_document_fill` replaces pixels with exact byte `rgba=[r,g,b,a]` in
one Undo step. Existing document Undo/Redo/Save/close apply. Save publishes an
ordinary Texture through the supervised importer; poll publication status before
assigning/exporting. The tools require the existing asset-write permission and
preserve project scope, document identity, source baselines and retained history.

Luminance UI masks use the existing source text read/write/register operations,
UiValues authoring/live tuning, Play-scoped reload and runtime-dependency review.
The explicit `luminance(<anonymous image or gradient>)` wrapper needs filter6;
nested wrappers, named inner decorators and inner lists reject before candidate
promotion. Both stdio framings preserve source revision/digest/replay, permissions,
Edit-only writes, Play identities and last-good presentation. No new agent-only
mask behavior or mutation endpoint is introduced. See [Runtime UI](runtime-ui.md).


## Animated shadow-panel authoring

Use `forge_source_text_read/write` for a `forge-shadow-panel` RML element and its
six `forge-shadow-*` styles, then `forge_source_ui_register` and the existing
component/property operations to assign UI Document and UI Values. Native numeric,
color and keyword keyframes/transitions run on presentation time. Use
`data-style-forge-shadow-blur="blur + 'px'"` for a copied named value; Play tuning
and review use the existing UI Values operation. Keep a binding and animation
off the same control. The element owns its derived `box-shadow`; direct native
shadow-list animations remain unsupported.

Existing source read/edit permissions, digest/revision guards, replay, Undo,
Edit-only writes, Play identity scopes and non-drawing inspection remain in force.
`forge_play_ui_reload` rebuilds a candidate and retains last-good UI on invalid
resolved blur/length values or resource limits. Scalar blur resolves0..32px; other
lengths are finite within +/-8192px; each view permits at most24 panels, subject to
shared snapshot/layer budgets. Percentages are unsupported. Export uses the
existing service and requires private filter7. There is no separate agent shadow
API. See [the manual](../manual/editor/runtime-ui.md#animate-a-panel-shadow).
