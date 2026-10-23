# Live automation

Use a trusted local script to inspect or edit the scene you have open in FORGE. Script edits appear in the Hierarchy and Scene panels and share the editor's undo history. The connection starts **off** every time you launch the editor.

## Start a connection

Open **Tools → Automation → Local connection...**. The **Local automation** window offers three choices:

- **Start read only** allows scene inspection, entity queries, diagnostics, component schema discovery and current project/document status.
- **Start with scene edits** also allows the supported scene commands, scene replacement, undo and redo.
- **Start with scene and file edits** also permits Save, Save As, New Scene and Open Scene inside this project.

Choose read only when a tool only needs information. To change permissions, stop the connection and start another one.

Click **Copy connection JSON**. This copies an address, port and secret token. Treat the copied text as a password: give it only to a trusted local tool. FORGE does not save the token to your project or preferences. Stopping the connection makes the old token unusable.

## Try the included example

Python 3 must be installed on your Windows computer. The script is included in the extracted ZIP at **Examples/Automation/live_scene.py**.

1. Start a read-only connection and copy its connection JSON.
2. Open a terminal in the extracted FORGE folder.
3. Run the following command, then paste the connection JSON at the hidden prompt and press Enter.

```powershell
py Examples/Automation/live_scene.py
```

The terminal prints scene counts and diagnostic findings. Your scene remains unchanged.

To add a small example, stop the connection, choose **Start with scene edits**, copy the new connection JSON, then run:

```powershell
py Examples/Automation/live_scene.py --add-example
```

Paste the new connection JSON when prompted. Four named shapes appear along the X axis: cube, sphere, cylinder and plane. Use **Fit scene** in the Scene panel to see them. Undo once removes all four; Redo restores them. Save in the editor to keep the changes.

## When edits pause

FORGE rejects automation edits while play, a native build, a file operation, a popup or an active UI gesture needs the editor. Finish that activity, inspect current state and submit a new request. Reads still describe the committed authoring scene, rather than an unfinished drag preview or the play world.

Scene changes and new filenames stop read-only and scene-edit connections. An explicit scene-and-file grant stays active across scenes in the same leased project and gives each changed document a fresh target. Switching projects or losing writer ownership stops every connection. An ordinary Save keeps the connection active.

**Automation (on)** in Tools shows that a listener is running. **Stop connection** closes it immediately. Committed scene edits remain, and saved files stay on disk.

## Failures and recovery

If a script reports a stale revision, another edit happened after its last read. Read the current scene before deciding whether to try again.

If a script disconnects or times out, a complete command may already have committed. Inspect the scene or undo history before running the example again. The included client does not automatically retry edits. Partial requests never commit. Integration developers can use the transport's bounded replay receipts to retrieve the result of the same numbered request without applying it again.

A second FORGE editor cannot open the same project while this editor owns it. Close or switch the first editor before opening that project elsewhere. See [Projects](projects.md).

## Current scope

This connection accepts FORGE authoring requests from the same machine. The
[MCP adapter](mcp.md) lets a compatible AI client use those same scene tools.
An explicit file grant exposes the editor-owned scene save/switch operations.
The connection exposes no native execution, arbitrary shell commands, project
switching or asset tools. The editor retains ownership of saving and recovery. The separate [Headless authoring tools](automation.md) remain
available for isolated in-memory work.

For exact request fields and limits, integration developers can read the repository's technical **docs/live-authoring.md** guide.
