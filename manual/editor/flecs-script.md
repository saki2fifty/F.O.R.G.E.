# Flecs Script

Flecs Script creates and updates ECS content from `.flecs` source text. It is separate from C++ gameplay code. The Script document evaluates a **separate preview world**; it does not modify your open scene or the running game.

## Create or open a script

1. In **Content**, choose **Create / Register**, then expand **Flecs Script**.
2. Enter a project-relative **Script path**, such as `Assets/example.flecs`.
3. Choose **Create / Register Script**. An absent file gets a small example. An existing file is registered without being overwritten.
4. The source opens in the central workspace. Later, double-click its asset in Content to reopen it.

The file receives an asset identity like other project content. Keep includes inside the project and use relative `.flecs` paths. Flecs resolves includes relative to their source file; FORGE checks that the resolved path stays in the project.

## Edit, save and evaluate

**Save** writes the source file. **Apply / Reload** evaluates the current draft in a bounded worker. Saving and evaluating are separate operations, so you can try a draft before saving it.

The output shows the source, preview world, included files and managed entities. Templates, expressions, math, loops, conditionals, relationships and component definitions use the pinned Flecs language directly. This is a text editor, not a visual scripting graph.

Each evaluation starts a worker, reconstructs the previous successful source when available, and uses Flecs' managed update for the new candidate. The worker ends after producing its inspection result. Native entity IDs in this output are temporary and must not be saved as FORGE EntityIds.

If evaluation fails, the previous successful output remains visible. **Cancel** stops the candidate. **Fresh preview** deliberately evaluates without reconstructing the previous source; use it when you want to start again or an included file has changed incompatibly.

## Find text and understand errors

Enter text in **Find in script…**, then choose **Find next**. The match is selected in the editor, wrapping to the beginning after the last match.

Errors appear in the document and **Problems**. **Go to error** in the document or **Open source** in Problems navigates to the reported source. The active source uses your current draft; an included source opens in a read-only viewer, preserving unsaved work. Register/open that file from Content to edit it. Parse errors include line and column when Flecs supplies them. Include failures name the requested file. Some managed evaluation failures provide a textual diagnostic without separate coordinates.

Source files are limited to 1 MiB each; total include reads are limited to 8 MiB and 128 distinct files. Workers have time and memory limits. A failed or cancelled candidate cannot edit the authored scene.

## History and closing

Text Undo/Redo works while editing the source field. Scene Undo/Redo does not undo source-file saves or Script evaluation. Closing a changed source asks you to save, discard the draft, or cancel; project switching uses the same guard.

If another program changes the file, Save refuses to overwrite that newer content. Preserve your draft, close it, and reopen the file before deciding how to combine the changes.

See also [Content browser](content-browser.md), [Undo and redo](undo-redo.md), and [ECS inspection](ecs-tools.md).
