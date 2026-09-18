# Content browser

Content lists recognized JSON scenes inside the current project. It helps you switch scenes without opening the system file dialog.

## Open a scene

1. Select the **Content** tab. In existing layouts it initially joins the Console docking area.
2. Type part of a relative path into **Filter scene paths...**, if needed.
3. Double-click the file, or select it and choose **Open selected**.
4. Resolve the unsaved-change prompt if the current scene has edits.

Files are validated when opened. A JSON file that is not a valid scene produces a diagnostic without replacing your authored scene. The browser recognizes scene structure during scanning; opening performs full scene validation.

## Create or refresh

**New scene** creates an empty untitled scene. Use Save As to choose its project filename. **Refresh** rescans immediately after an external file operation; the visible browser also refreshes approximately every five seconds.

The browser skips `.forge`, `.git`, project manifests, and symbolic links. It limits scans to 16 nested directory levels, 10,000 entries, and fewer than 4,096 JSON files. If a limit is reached, use File → Open scene directly.

Content currently supports scene discovery and opening. Asset importing, thumbnails, file rename/delete, and drag-and-drop placement are not available yet. See [Scenes](scenes.md) and [Saving and recovery](saving-recovery.md).

The browser checks document structure, so package metadata such as build.json and manifest.json no longer appears as scene content. Scenes using custom `.json` filenames are still recognized. Opening performs full validation. Scans are bounded to 10,000 entries and 64 MiB of JSON candidates, with an 8 MiB per-file limit; use File → Open scene if a scan exceeds those limits.

## Prefab assets

Expand **Prefab assets** to create, instantiate, duplicate or edit reusable object groups. See [Prefabs](prefabs.md) for the full workflow. The scene list remains separate.

## Runtime UI assets

Use **Content → Runtime UI** to create a HUD example or register a project-relative RML document. Assign it to an entity in **Inspector → Runtime UI**. See [Runtime UI](runtime-ui.md) for the complete workflow. These asset operations are outside scene Undo.
