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
- **Allow an output folder** for exports and view images.

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
collision, terrain, tile, Sprite atlas, particle and animation source editors. The agent can
create or open a source, inspect its format, edit validated fields, and use that
source document's Undo and Redo. You see the same draft in the editor.

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
or authored values. For Navigation Agent, supported live fields include Enabled,
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
keeps completed copies/publications. Runtime recapture and in-place rebaking are
unavailable.

## View images, export and test

Choose an existing output folder before enabling the connection. The agent can
create child output folders, capture Scene/Game PNGs and inspect bounded image
previews through MCP. Full PNG files keep the rendered dimensions. Timeline
sequence export uses the existing preview/controller and writes sampled frames
plus their receipt into a new folder. Cancellation retains completed files and
restores the preview cursor; an image already committing must finish.

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
