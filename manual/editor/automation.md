# Headless authoring tools

The Windows package includes `forge_tools.exe` for scripts that inspect and edit a scene in memory, without opening the editor or using a GPU. It shares authoring commands with the editor.

## Start a session

Open a terminal in the extracted package folder and run:

```text
forge_tools.exe --stdio
```

Send this single line first:

```json
{"api":1,"method":"discover"}
```

The response lists supported commands, property descriptions, a document target, and its revision. A script echoes that target when reading or editing the document. Changes require the current revision so an outdated request cannot overwrite newer session changes.

## Try the packaged example

With Python 3 installed, run this from the extracted package folder:

```text
python Examples/Automation/create_blockout.py forge_tools.exe
```

It prints a four-shape scene created through one command batch. To keep that result, redirect it to a new scene file in a writable location you choose, then open the file in FORGE. Avoid overwriting a scene that is currently open. The sample does not require additional Python packages.

## What scripts can do

Create primitives, rename/reparent/duplicate/delete entities, edit supported properties, copy/reset/ground/snap transforms, inspect inherited values, query entities, read diagnostics, and undo/redo. A batch of commands commits as one undo step; if any command fails validation, the whole batch is rejected.

The session edits an isolated copy in memory. It does not connect to the open editor, save project files, compile gameplay, or run native modules. An MCP server is not included yet.

End the input stream to close the process. Unsaved in-memory work ends with the session. A script must consume a scene snapshot if it needs the result afterward.

Technical request examples and exact contracts are maintained separately in the developer documentation. Normal editor users can use [Command palette](commands.md) and [Scene diagnostics](diagnostics.md) without writing scripts.

## Work with the open editor

For read-only project source and dependency inspection, see [Asset inspection tools](asset-tools.md).

To inspect or edit a scene already open in FORGE, use [Live automation](live-automation.md). The headless process described above continues to own only its isolated in-memory scene.

## Structured property values

`property.set` accepts the value type described by the selected component field.
Besides numbers, text and switches, a reflected field may accept a structured value
or a list. Submit the complete value for that field; the editor checks its type,
limits and scene rules before committing. An invalid edit leaves the scene and
Undo history unchanged.

On a prefab instance, a supported list field is one override. Setting the same list
again still records your choice. Revert makes that whole field follow the prefab
again, and Undo restores the override. This does not merge individual list entries
with later prefab edits. Schema discovery describes fields currently available;
project-defined component authoring remains under development.
