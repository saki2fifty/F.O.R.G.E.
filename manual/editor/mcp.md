# Connect an AI agent through MCP

FORGE lets a trusted AI agent develop a game through your open editor's existing
authoring tools. Scene changes share normal Undo; source documents, files, builds,
Play and export keep their own normal ownership and results.

## Connect

1. Open the project and scene you want the agent to work on.
2. Open **Tools → Automation → Local connection…**.
3. Choose a limited read/scene/file connection, or **Start project authoring** for game development. Select the additional grants described below before starting.
4. Click **Copy MCP configuration**.
5. Add the copied FORGE server entry to your AI application's MCP configuration and start its connection.

The additional grants are:

- An external source folder for imports.
- A Projects folder for creating and switching projects.
- **Allow C++ build and execution** for builds, native inspection, Play and game testing.
- **Allow an output folder** for exports, view images and stored texture pixels.

The exact settings screen depends on the application. It must support local
stdio MCP servers and one of FORGE's supported protocol versions:
2026-07-28 or 2025-11-25.

The copied configuration points to FORGE's existing `bin/forge_tools.exe` and
includes a temporary access token. Keep it within the trusted AI client. FORGE
does not require Python or another scripting language to run this connection.
If the client uses a different configuration format, retain the executable,
`--mcp-stdio` argument and `FORGE_AUTOMATION_CONNECTION` environment setting.

Ask your agent to **discover FORGE's scene tools and describe the open scene**.
It should see a read-only or editable tool set matching your choice. To try an
edit, ask it to **create a cube named Agent Cube at the origin**. The cube should
appear in Hierarchy; Undo removes it. Save in FORGE to keep authored changes.

## Save and change scenes

With **Start with scene and file edits**, ask the agent to save the scene, save a
copy under **Scenes**, create a new scene, or open a scene in this project.
The tools report whether the scene has unsaved changes and whether recovery exists.
New and Open require a clean scene or an explicit request to discard its changes.
Other unsaved source drafts must be settled in their own editor first.

A copied saved scene gets its own identity. Saving an untitled scene for the first
time keeps its identity. The agent must discover the new scene target after a
filename or scene change. Saving checks for external file changes and will not
silently overwrite someone else's edits. File saves are not reversed by Scene Undo.

## Work with assets

For navigation decisions, the agent can discover both state graphs and behavior
trees through the behavior catalog. Ask it to create a Navigation Behavior with
profile `tree`, connect ordered nodes, publish it and attach it to a Navigation
Agent. Source edits share the open graph document's revision checks and Undo.
A disconnected draft can be edited, but must become one connected tree before
publication. See [Navigation behaviors](navigation-behavior.md).

Choose **Start project authoring** for asset search, import/reimport, placement,
and source-file move/duplicate/delete. To import files from outside the project,
first enable the external source-folder option and choose that folder. The agent
can select sources only within the project or this explicitly granted folder.

The agent prepares an import and reads its review before confirming it. Copies
go into a new folder under **Assets**; existing files are not overwritten. The
native importer reports each file's actual result. Imported Models, Meshes,
Tile Maps and Prefabs can then be placed in the scene. Placement is undoable.
File copying and asset publication have their own results and are not Scene Undo.

Move, duplicate and delete also use a prepared reference-impact review. Delete
needs explicit acknowledgment and keeps the existing recovery backup. Cancelling
stops pending work; it does not remove completed publications or file commits.
The agent should inspect progress rather than treating a queued request as done.

## Edit reusable assets

Project authoring also exposes the existing material, material graph, mesh,
collision, terrain, tile, Sprite atlas, paint texture, particle and animation source editors. The agent can
create or open a source, inspect its format, edit validated fields, and use that
source document's Undo and Redo. You see the same draft in the editor.

For [skeletal retargeting](animation-retargeting.md), the agent can inspect published
joint identities, parents and rest transforms, create or open a `.retarget.json`
recipe with the **animation_retarget** editor, map joints explicitly and publish
the target-bound clip. It assigns that clip using the same Animator, States or
Blend Space or Animation Graph properties as a user. Skeleton inspection is read-only; saving and
Scene assignment use the normal project-authoring grants and separate histories.

For [Animation Graphs](animation-graphs.md), the agent uses the **animation_graph**
source editor and its normal template, patch, history and publication tools. Node
connections use graph-local UUIDs; only skeleton, clip and mask fields select assets.
Named input slots keep their identities when rows or names change. The agent assigns
the published graph with ordinary Animator property operations. During Play,
**play.tune** changes declared input values and override checkboxes in the current
runtime; the existing post-Stop review can keep selected changes in the Scene.

For a Sprite atlas, create or open its `.atlas.json` source with the
**sprite_atlas** editor. Creation needs the published input Texture UUIDs in
**textures**. Its source contains stable local frame keys and packing settings;
the agent can replace or reorder images through a validated patch, use source
Undo/Redo, and save/publish the same draft shown in the recipe editor. Removed
keys are not allocated again. Set **Texture frames** on each Sprite using the
atlas Material, then choose an existing key. See
[Sprites and maintained atlases](sprites.md).

For tile collision, the agent can choose full cells, decoration, a convex or
concave polygon, or **Contours / holes** for separate solid regions and inner
voids. The same validation, document history and publishing steps apply as in
[Tile Maps](tile-maps.md). Apply or discard interactive collision edits before
the agent changes that source. Publish the Set and then its dependent Maps.

For animated tiles, the agent can set the ordered image-grid frames and their
durations in the Tile Set source. Publish the Set and then its affected Maps.
Pause and Step in Game follow simulation time; the source tab's preview controls
only preview the applied sequence. Finish or discard an interactive animation
draft before the agent changes that source.

Saving checks for external changes before publishing. The agent must inspect the
publication result; a saved file can remain after compilation fails. Closing an
unsaved or unpublished source requires an explicit discard. Active graph, brush
and timeline gestures must finish before an agent edits their source.

Import settings expose the editor's existing typed rules and model identity
reviews. Prefabs use the existing source editor and library. Applying instance
overrides requires review and acknowledgment because it saves both the prefab
and the current scene; the normal Scene Undo and Redo replay that pair.

## See navigation in the editor

With asset authoring enabled, an agent can use `forge_navigation_overlay` with
`visible: true` or `false` to show or hide the navigation overlay. This includes
the selected agent's requested destination. `forge_navigation_status` reports
`overlay_visible`. The view setting does not save the Scene or add an Undo entry.

## Projects and settings

Enable **Allow a project workspace** and choose your **Projects** folder before
starting project authoring. The agent can then create and open projects inside
that folder, or return to the project initially connected. Existing folders are
not overwritten. Save or explicitly discard scene changes before switching;
other unsaved source/settings drafts must be settled first.

The agent can inspect and edit the same Project Settings draft shown in FORGE.
Saving checks external file changes. Native code/build configuration stays with
its corresponding code tools.

Recovery is explicit. The agent reviews the exact snapshot before restoring or
discarding it. Restoring does not silently save the scene. These tools can resolve
the recovery dialog itself; they remain blocked by other dialogs and pending work.

## Project code and Play

The agent can use the same **C++ Sources** editor to create/open/edit/save project
code under **Native**. It can read public examples and headers from the matching
installed SDK. Source changes appear in the normal editor tabs; replacement shares
one source Undo step. File conflicts preserve the draft. Saving does not grant
permission to run code: an ungranted save never queues Build on Save.

Project-wide source search is also available with read-only access. The agent
starts `forge_code_search_start`, polls `forge_code_search_status`, and can cancel
the returned operation with `forge_code_search_cancel`. Results include open draft
snapshots, source digests and line/column locations. With code editing permitted,
`forge_code_open` can use a result's path, state, line and column to navigate;
changed source returns `stale_source`. Search never grants editing or execution.
See [Find code across the project](native-gameplay.md) for matching rules and limits.

With execution allowed, the agent can test compiler tools, build current saved
code, inspect its opted-in component types and explicitly migrate old authored
values. Native code and build recipes run with your account's operating-system
access. FORGE's separate process and MCP folder limits are not an OS sandbox for
that code. No arbitrary shell/program or engine-repository tool is provided.

The agent can start/stop/pause/step Play, inspect runtime entities, read timestamped
logs, supply validated input, adjust supported live fields, control watched
sounds/animations and use declared game-UI commands. It must use the current Play
launch/session/world and check actual acknowledgements. A queued tuning request
can still be rejected by the runtime. These changes do not silently change source
or authored values. For a [Sound Cue](audio.md), author its complete graph through
the usual Scene property commands, watch that cue entity and request `one_shot`
with `forge_play_audio_control`. Each request consumes an event choice; retrying
it requests another event. The cue's Audio Sources retain pause/stop ownership.
Source nodes accept **delay_seconds** from zero to ten. Omit it for zero delay.
The cue watch reports the selected offsets; source watches report waiting
**delayed_one_shots**. Use the source's normal playback controls to pause, resume
or stop those voices. Delay uses output time after preparation, independently of
source pitch.
For Navigation Agent, supported live fields include Enabled,
Has Destination, Speed, Stopping Distance and world destination coordinates; its
NavMesh asset is selected while authoring. Pause before changing several goal
coordinates and inspect Navigation progress after Step or Resume.
Runtime reads also include the current failed agent's concrete details. A corrected
live target clears the previous reason while Pending; inspect its next tick for
the route result. Position/goal metrics require a valid sampled path.

To keep supported live values, the agent captures the existing review, stops Play,
selects fields and applies them to the Scene. That creates one Undo step; saving
is separate. Building changed C++ still requires Stop → Save → Build → Play.

## Other game sources

Raw game UI and HLSL sources can be edited with exact disk-conflict checks. UI
registration and shader compilation use their existing tools. These raw files do
not have Scene or document Undo. Typed material/mesh/animation/terrain/tile/particle
sources continue to use their normal source documents and history.

For UI drop shadows, read and edit the RML/RCSS through the source text tools,
then register or refresh the UI source. Use `drop-shadow(#20408080 8px 6px 2px)`
inside `filter`. Each offset is limited to `-128`..`128` resolved pixels and blur
sigma to `0`..`16`. Saved **UI Values** can drive a `data-style-filter` binding;
use current Play tuning and **Reload UI** through their existing tools. Failed
source reload keeps the preceding usable UI. Export with the matching runtime kit.
See [Runtime UI](runtime-ui.md) for the illustrated workflow.

Use the same UI source tools for `backdrop-filter` and
`data-style-backdrop-filter`. A declaration such as `backdrop-filter: blur(3px)`
filters content already drawn behind a partly transparent panel. Its foreground
stays sharp. Saved **UI Values**, current Play tuning, native keyframes and
**Reload UI** use their ordinary tools. Save and export with the matching runtime
kit. See [Runtime UI](runtime-ui.md) for clipping and shared layer limits.

For linear gradients, edit RML/RCSS through the same source tools and register or
refresh the source. Use `decorator: linear-gradient(90deg,#e96644,#4e96df)` or
`repeating-linear-gradient` with two through sixteen stops. Saved **UI Values**
can drive `data-style-decorator`; current Play tuning, native keyframes and
**Reload UI** use their existing tools. Invalid source reload preserves the
preceding usable presentation. Export with the matching supplied runtime kit.
See [Runtime UI](runtime-ui.md) for syntax and limits.

Radial and conic gradients use the same tools: write `radial-gradient`,
`repeating-radial-gradient`, `conic-gradient` or `repeating-conic-gradient` in
`decorator`, then register/refresh the source. Model-driven radii or conic angles
use **UI Values** and `data-style-decorator`; native keyframes, current Play
tuning, Reload UI and matching-kit export retain their ordinary behavior. See
[Runtime UI](runtime-ui.md) for the illustrated radial/conic workflow and bounds.

For alpha masks, edit `mask-image` in the same RML/RCSS source tools and
register/refresh the source. Use a supported gradient or local image decorator.
For image decorators, add the registered image through the existing Runtime
Dependencies review/save tools on the UI document; an observed file alone is not
a required package resource. Saved values can drive `data-style-mask-image`; current Play tuning, keyframes,
Reload UI and matching-kit export retain their existing tools. Invalid reload
preserves the preceding presentation. See [Runtime UI](runtime-ui.md) for alpha
semantics and shared layer, image and filter-chain limits. For brightness-based
coverage, use `mask-image:luminance(linear-gradient(90deg,#000,#fff))` or wrap a
supported image. The same source, runtime-dependency, live-value and reload tools
apply. Nested wrappers reject and preserve the usable UI; use a current runtime
kit when exporting luminance masks.

Navigation uses the existing static bake worker. Runtime dependency declarations
use their existing reviewed draft and catalog Save. Flecs Script tools evaluate
the existing isolated diagnostic data/world preview; they do not replace C++
gameplay or write the authored Scene. Failed previews keep the last good result.

## Capture reflection maps

With project asset authoring access, the agent can capture an authored Reflection
Probe through `forge_capture_reflection_start`. It chooses a new folder below
**Assets**, polls `forge_capture_status`, then reviews and commits the returned
import batch through the existing asset import tools. This project source workflow
does not need an external output-folder grant.

The capture has the same controls and limits as [Capture a reflection map](lighting.md).
Automatic map assignment requires the original Scene revision and uses one Scene
Undo step, including resetting map rotation to zero. A changed Scene leaves the
new map available for explicit assignment. Save after assignment. Cancellation
keeps completed source changes/publications.

To rebake the assigned capture DDS map, the agent sets `update_assigned:true` and
omits `destination`. It reviews the returned `source_revision_asset` and exact
review token before committing. This retains the Texture ID, its consumers and
saved import settings. Source replacement is separate from reimport and Scene
Undo; failed reimport preserves the old cooked selection. Edited source/settings
must be reimported before rebaking. GPU runtime capture uses the reflected component controls described below.

## Refresh probe lighting during Play

With Scene authoring access, create a Reflection Probe using `forge_entity_create`
and the discovered `render.reflection_probe` recipe. Use `forge_property_set` to
enable `runtime_capture`, then set `capture_size`, `capture_interval`,
`capture_near`, `capture_far` and `capture_layers`. These are the same Inspector
fields; discover their defaults and supported bounds before changing them. Save
the Scene before export. An assigned Texture remains the saved fallback.

During Play, `forge_play_tune` can change these scalar values for the running
entity. They are temporary unless kept through the tuning review. Use at most
two runtime probes and eight probes in total. Capture layers must exclude surfaces
that read camera outputs. Positive intervals use simulation time and hold while
paused; zero refreshes every rendered frame. No source-import review or output
folder grant is needed to refresh GPU lighting. A matching runtime kit recreates
maps in the exported game. See [Refresh lighting during Play](lighting.md).

## View images, export and test

Choose an existing output folder before enabling the connection. The agent can
create child output folders, capture Scene/Game PNG, lossless WebP or opaque JPEG images and
inspect bounded PNG previews through MCP. Full files keep rendered dimensions.
`forge_capture_texture` exports a published Texture's selected mip/layer/face/depth
slice through the same image task. The agent supplies the asset, variant and output
filename; the stored-byte profile, PNG/sRGB WebP distinction and limits are described
in [Texture image export](textures.md). HDR output uses supported opaque linear float
textures and shared-exponent RGBE quantization; it provides a written file path and
MIME through capture status. Its owned PNG inspection uses a fixed display transform. Import drafts and preview display controls
are excluded. Existing files require an explicit replacement request.

For published **BC1/BC2/BC3/BC4/BC5/BC7 UNORM** selections, use the same texture
capture tool and selectors. The packaged asset worker decodes the selected blocks.
Status identifies **decoded_rgba8** and 8-bit components. PNG and sRGB WebP contain
decoded reference texels; they cannot recover pixels lost during compression.
JPEG additionally requires actual opaque sRGB texels. BC1 retains punch-through;
BC4/5 do not reconstruct normal blue. BC6 uses HDR output below. See
[compressed texture export](textures.md) for limits and the Gallery example.

For **BC6 unsigned/signed**, choose **semantic: hdr** and a **.hdr** destination.
Use **Mip** 0 of the Gallery's **HdrUnsigned.dds** or **HdrSigned.dds** examples.
Status identifies **texture.profile: decoded_bc6_linear_rgbe**. Decoding and RGBE
quantization cannot recover pre-compression pixels. The signed example's mip 1
has negative red; its export reports an error and preserves an existing file.
Selected values must be finite and nonnegative. Preview display controls remain
excluded; the saved HDR supports the same bounded owned PNG inspection.

For **BC4/BC5 SNORM**, select the cooked variant explicitly when needed, such as
**semantic: data** for the signed Gallery examples, and choose **.png**. Stored
signed channels map -1..1 to 0..65535 with nearest, half-up rounding. Missing
green/blue are zero and alpha is 65535; there is no sRGB label or normal-blue
reconstruction. Status reports **component_bits: 16** and
**texture.profile: decoded_snorm_rgba16**. The owned PNG inspection is quantized
to 8-bit. Other output formats and the ambiguous -127/-128 endpoint order reject;
an existing file remains intact. Preview display controls do not alter this mapping.

Normalized **R16/RG16/RGBA16** textures use **.png** and preserve 16-bit samples in
the full file. Capture status reports **component_bits: 16** and the completed
texture profile **stored_rgba16**. The bounded PNG preview uses nearest 8-bit
samples and may reduce resolution; it cannot demonstrate full 16-bit precision.
Its PNG16 limits and channel/alpha rules are described in [Texture image export](textures.md).

For a written **.hdr**, the agent can request the current or later owned PNG
inspection. Status and image metadata report **preview_profile: hdr_reinhard_srgb**.
The PNG uses unit-exposure per-channel c/(1+c), standard sRGB encoding and nearest
8-bit values, with the usual 1024-pixel bound. Full HDR remains quantized linear
RGBE. The inspection ignores Scene and Texture-viewer exposure/processing and
cannot recover HDR magnitudes or weaker channels discarded during encoding.
Only FORGE's fixed-working-primary output profile is admitted. A malformed preview
preserves the completed HDR file and reports its own error. See
[HDR image export and inspection](textures.md).

For **.jpg/.jpeg**, the agent can set **jpeg_quality** from **1–100**; the default is
**90**. In still-image export, this option is accepted only for JPEG destinations. Texture JPEG output
requires opaque sRGB byte storage. JPEG is lossy even at 100; its PNG inspection
preview shows decoded saved pixels. The same output grant, operation/revision
checks and replacement rules apply. See [JPEG image export](textures.md).

Timeline sequence export uses the existing preview/controller and writes sampled
PNG frames and their receipt into a new folder. Set **format: webp** on
**forge_capture_sequence_start** to add `animation.webp`; the default **png** keeps
frames alone. WebP requires 2–4096 frames, the packaged asset worker and the
[sequence export limits](animation-sequences.md). Status reports **encoding**
while it runs, then the animation's path, bytes, duration and loop count when
complete. Individual PNG frames remain available through the owned image preview.
Cancellation retains completed files and restores the preview cursor; an image
or animation already committing must finish.

Set **format: avi** on **forge_capture_sequence_start** for a silent Motion JPEG
movie alongside its PNG frames. Set **jpeg_quality** from 1–100 if needed; it defaults
to 90 and is accepted only with the AVI format on this tool. AVI needs opaque sRGB
frames, 1–4096 frames, 0.001–240 FPS and the packaged asset worker. Status and
`frames.json` report the AVI path, byte count, rational rate/scale, duration, quality
and that audio is absent. Encoding failure keeps the PNGs and restores the preview.
Scene values and sample times remain unchanged; see the [movie export controls and
limits](animation-sequences.md). This captures the sequence preview; it does not run
gameplay or record sound.

For a fresh game movie with sound, use **forge_capture_game_read** and then
**forge_capture_game_start**. Grant both execution and an output folder. Pass the
observed **scene**, **source_revision** and **document_generation**, plus a new
relative **path**, **frames_per_second**, **frame_count**, **width** and **height**.
**jpeg_quality** is optional. The tool starts its own fresh Play run and private
UserData; it cannot capture an existing live game or interactive input.

Poll **forge_capture_status** and read **game_recording**. Use its active
**operation** with **forge_capture_cancel**. The completed folder includes
**game.avi**, PNG frames, PCM audio and **recording.json**. To inspect a completed
PNG, pass the stable **artifact_operation** and **frame** to
**forge_capture_preview**, then pass the returned new **operation** to
**forge_capture_image**. Keep the artifact token for further frame previews.
See [recording prerequisites and limits](game-recording.md).

Export uses saved project content and the matching selected runtime/module kits.
Save all authoring drafts first and set Game defaults and Startup Scene in Project
Settings. The agent can inspect actual export progress, cancel pending preparation,
and explicitly rebuild a recognized prior export. Unrelated folders are preserved.
The exported game includes its compiled runtime/game code, without the editor,
compiler, SDK or MCP connection.

With execution allowed, the agent can run this task's verified completed Windows
game and inspect its process/log or stop it. Managed deployment kits and exported
graphical games currently target Windows. FORGE does not substitute a portable kit.

For a headless Windows check, the agent can request `verify_startup:true` when
running the export. This uses the real game's finite startup diagnostic with
software D3D12 rendering and offline audio, then reports its exit code and log.
Normal launch uses your graphics and audio devices. A diagnostic pass does not
replace testing the game on your computer.

A useful request is: **Use FORGE's discovered schemas to create a cube game, add
project code with Inspector-adjustable speed, save/build/inspect it, test it in
Play, capture a view image, then export into the granted output folder. Report the
actual build/runtime/export results and anything not tested.**

For heightfield foliage, **forge_foliage_terrain_prepare** accepts
**minimum_slope** and **maximum_slope** in degrees, defaulting to **0** and **90**.
The read-only preparation reports **candidate_count**, **retained_count** and
prepared scatter arguments. Apply those exact arguments through
**forge_mesh_scatter** at the returned Scene revision. Count is the candidate pool;
filtering can keep fewer objects. Set **align_to_slope: true** to tilt the placement
frame to the same geometric surface; its default is **false**. The prepared
arguments include normals and status identifies **aligned_to_slope**. Seeded yaw
and template rotation stay relative to the tilted frame. See
[terrain slope filtering and alignment](foliage.md).

For collision clearance, call **forge_foliage_collision_start** with the ordinary
scatter arguments, or with the arguments from terrain preparation. Optional
**radius**, **offset** as **{x,y,z}**, **mask**, **include_sensors**, **surface** and
**ignored** control the sphere and exact object exclusions. Poll
**forge_foliage_collision_read** using the returned **operation** until it is
**ready**, **failed** or **cancelled**. **forge_foliage_collision_cancel** stops that
operation. These tools change no Scene objects and are available to read-only
connections. Ready results preserve heights, normals and the original candidate
mask; use their **arguments** and **scene_revision** with **forge_mesh_scatter**
to create one ordinary Undo entry. Source changes cancel preparation; changed
assets/settings invalidate it. Check again before using an older result. These
tools inspect authored 3D colliders without executing project code or advancing
Play. See [collision clearance controls and limits](foliage.md).

For box shadows, edit `box-shadow` or `data-style-box-shadow` through the same
source tools, register the source, set **UI Values**, and use live tuning/reload.
Keep blur radius within `0..32` pixels. Direct box-shadow list keyframes/transitions are
unavailable; an invalid reload keeps the usable UI. Save the authored Scene and
source before exporting with the supplied kit. See [Runtime UI](runtime-ui.md)
for viewport and cached-texture limits.

For morphological anti-aliasing, use **forge_scene_rendering_set** with
`morphological_anti_aliasing`. Stop Play before changing authored settings. To
switch methods, set its **enabled** to true and
`fast_approximate_anti_aliasing.enabled` to false in the same operation. Threshold
accepts 0.01..0.5, search steps 1..32 and corner rounding 0..1. The scene command uses
normal revisions, Undo and Save. See [Lighting](lighting.md) for defaults and limits.

## Keep control

- **Stop connection** immediately revokes access. Committed scene edits remain, and saved files stay on disk.
- Read-only and scene-edit connections expire on scene switches. A scene-and-file connection stays active across scenes in the same project; switching projects or restarting the editor requires a new connection. A separately granted Projects folder allows transitions within that folder and back to the initial project; other project switches revoke access. Regular Save keeps every connection.
- Authoring edits pause during Play, C++ builds, dialogs and active gestures. Exact owner controls such as Stop, job cancellation, live tuning and image capture use their own guarded paths.
- Direct MCP file tools stay within their granted project/import/output/public-SDK roots. They do not edit FORGE's engine checkout. The separate native execution grant has the OS access described above.

If the agent reports a lost response, inspect the current state or retrieve the
original command receipt. To retrieve it, resend the exact original tool arguments,
including the target, expected revision and sequence number. The receipt returns
the original result without repeating the edit. If the receipt is unavailable,
check which changes already exist before issuing a new command. Repeating an
uncertain action with a fresh sequence can create duplicate objects.

The precise capability checklist and integration contract are maintained in
**docs/mcp.md** in the source repository.

For occlusion culling, call **forge_scene_rendering_set** with
`occlusion_culling:{"enabled":true,"depth_bias":0.0001}`. Stop Play before changing
authored settings. Use the current document target and expected revision, then
save the Scene before export. **forge_history_undo** and **forge_history_redo**
use the same history as the editor. See [Occlusion culling](occlusion-culling.md)
for supported geometry and limits.

## Paint texture pixels

Use `forge_asset_paint_texture_catalog` to inspect brush math and current limits.
Create a `paint_texture` source with `forge_asset_document_create`, a
`.paint-texture.json` project path and optional `canvas` dimensions, Color/Data
semantic and byte background. Read metadata with `forge_asset_document_read` and
query stored RGBA bytes with `forge_asset_document_pixels`.

`forge_asset_document_stroke` prepares a pixel path and brush;
`forge_asset_document_copy_pixels` copies a matching published Texture into the
canvas. Both return while pixel preparation is pending. Poll document status until
`transient_draft` is false, inspect its diagnostic, then refresh the document
revision. `forge_asset_document_cancel_pixels` cancels pending work.
`forge_asset_document_fill` fills with exact byte RGBA. Completed edits use the
ordinary document Undo/Redo and Save / Publish workflow. These operations require
asset-write access and retain the normal project, revision and retry rules.
See [Paint a texture](texture-painting.md) for the equivalent controls and limits.


For animated panel shadows, use the same source tools to create a
`forge-shadow-panel` and its six `forge-shadow-*` controls. Use native keyframes
or transitions, or bind a named UI Value to a control. Register the source, assign
UI Document, save, and use the existing Play reload/tuning workflow. Keep blur
within `0` to `32` resolved pixels and export with the supplied compatible kit. Direct
`box-shadow` list animation is unsupported; see [Animate a panel shadow](runtime-ui.md)
for syntax and shared resource limits.


## Author an interactive scene panel

1. Create and publish a Render Texture and an ordinary unlit Material sampling it. Keep producer Camera layers separate from display Mesh layers.
2. Create an **Engine / Quad** Mesh entity and assign the Material. Discover `forge.ui_surface_input` in the component schema, then use `forge_component_add` and `forge_property_set` to set its `render_texture` to that output and keep `enabled` true.
3. Register an RML document, assign it through `forge.ui_document` on a UI entity, and set that document's `render_texture` to the same output. Author its source, UI Values and commands with their existing tools.
4. Save the Scene and resources, publish pending asset changes, and export with the matching supplied runtime kit. Use ordinary history and revision checks for Scene changes.

In Play, use the existing semantic document-command tools for declared actions.
MCP does not inject raw pointer or ray gestures. The running presenter owns
native focus, pointer capture, text and keyboard navigation. See
[the supported surface profile](runtime-ui.md)
for Material settings, UV flips, geometric blockers and Camera view limits.

## Create flat Mesh shapes

Call `forge_asset_document_template` with `editor: "editable_mesh"` and
`mesh_profile: "planar_quad"`, optionally setting `mesh_size: [width,height]`, or
use `mesh_profile: "planar_polygon"` and a strictly convex `outline` of 3–16 XY
pairs. Create the source with `forge_asset_document_create` and a new project
`.mesh.json` path. Use the existing source read/patch/Undo/Redo and Save tools.
Creation options do not apply to Open or other editors.

Publish the Mesh, assign it to a Mesh Renderer, and assign an Unlit Material.
Use a game Camera facing the XY plane with orthographic projection; enable
Two-sided on the Material when viewing its back. Save the Scene before export.
The shapes use the ordinary Mesh source and renderer. Concave outlines, holes
and curves are unavailable. See [Create a flat 2D Mesh](editable-meshes.md).

## Place foliage on a Mesh surface

Call `forge_foliage_surface_prepare` with the surface object's Scene UUID,
scatter arguments and optional height offset, slope range and alignment. It reads
published static LOD0 triangles and returns prepared arguments with a Scene
revision. Apply them using `forge_mesh_scatter` at that revision, then save the
Scene. Preparation is available to read-only clients; placement requires write
permission. Recheck preparation after publishing geometry changes.

Optional collider clearance uses `forge_foliage_collision_start` with these
prepared arguments and the same surface entity. Apply its ready arguments through
the existing scatter tool. See [Place onto a static Mesh](foliage.md) for limits,
coverage, geometry selection and ordinary Undo/export behavior.
