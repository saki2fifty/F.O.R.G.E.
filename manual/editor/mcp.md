# Connect an AI agent through MCP

FORGE can let a trusted AI agent inspect and edit your open scene through MCP.
The agent's changes appear in the editor and share your normal Undo history.
The tools cover scene files, asset/source editing, settings and recovery.
An explicitly selected Projects folder also permits project creation/opening.
C++ source/build/reflection, Play controls and export are still being connected
to MCP.

## Connect

1. Open the project and scene you want the agent to work on.
2. Open **Tools → Automation → Local connection…**.
3. Choose **Start read only**, **Start with scene edits**, or **Start with scene and file edits**. The last option also permits saving and opening scenes in this project.
4. Click **Copy MCP configuration**.
5. Add the copied FORGE server entry to your AI application's MCP configuration and start its connection.

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
collision, terrain, tile, particle and animation source editors. The agent can
create or open a source, inspect its format, edit validated fields, and use that
source document's Undo and Redo. You see the same draft in the editor.

Saving checks for external changes before publishing. The agent must inspect the
publication result; a saved file can remain after compilation fails. Closing an
unsaved or unpublished source requires an explicit discard. Active graph, brush
and timeline gestures must finish before an agent edits their source.

Import settings expose the editor's existing typed rules and model identity
reviews. Prefabs use the existing source editor and library. Applying instance
overrides requires review and acknowledgment because it saves both the prefab
and the current scene; the normal Scene Undo and Redo replay that pair.

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

## Keep control

- **Stop connection** immediately revokes access. Committed scene edits remain, and saved files stay on disk.
- Read-only and scene-edit connections expire on scene switches. A scene-and-file connection stays active across scenes in the same project; switching projects or restarting the editor requires a new connection. A separately granted Projects folder allows transitions within that folder and back to the initial project; other project switches revoke access. Regular Save keeps every connection.
- Edits pause during Play, C++ builds, dialogs and active editor gestures.
- The agent can edit supported scene properties and create entities. These tools do not grant access to FORGE's engine source or unrelated computer files.

If the agent reports a lost response, it should inspect state or retrieve the
same command receipt before trying again. Repeating an uncertain action as a
new command can create duplicate objects. The tools provide revisions and
sequence numbers to help the agent handle this correctly.

The precise capability checklist and integration contract are maintained in
**docs/mcp.md** in the source repository.
