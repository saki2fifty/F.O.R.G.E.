# Content browser

Content lists JSON scene candidates inside the current project. It helps you switch scenes without opening the system file dialog.

## Open a scene

1. Select the **Content** tab. In existing layouts it initially joins the Console docking area.
2. Type part of a relative path into **Filter scene paths...**, if needed.
3. Double-click the file, or select it and choose **Open selected**.
4. Resolve the unsaved-change prompt if the current scene has edits.

Files are validated when opened. A JSON file that is not a valid scene produces a diagnostic without replacing your authored scene. The browser lists candidates by filename rather than parsing every file while scanning.

## Create or refresh

**New scene** creates an empty untitled scene. Use Save As to choose its project filename. **Refresh** rescans immediately after an external file operation; the visible browser also refreshes approximately every five seconds.

The browser skips `.forge`, `.git`, project manifests, and symbolic links. It limits scans to 16 nested directory levels, 10,000 entries, and fewer than 4,096 JSON files. If a limit is reached, use File → Open scene directly.

Content currently supports scene discovery and opening. Asset importing, thumbnails, file rename/delete, and drag-and-drop placement are not available yet. See [Scenes](scenes.md) and [Saving and recovery](saving-recovery.md).
