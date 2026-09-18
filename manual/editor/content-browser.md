# Content browser

Content is the project's asset browser. It lists registered scenes, prefabs, audio, animation, navigation and Runtime UI assets from the existing project catalog. Saved scenes are also discovered by their scene identity.

## Find and inspect an asset

1. Open **Window → Content**.
2. Use **Search project assets...**, **Type** and **Folder** to narrow the list.
3. Click an asset to inspect its type, source and availability in Inspector. This replaces entity selection; it does not change an entity's properties.
4. Double-click a scene to open it, or use **Open scene** in its context menu. Resolve any unsaved scene or draft prompts first.

**Refresh** rescans immediately; the visible browser refreshes approximately every five seconds. Empty results explain how to clear filters or create/register content. **Reveal source folder** opens the containing folder in your operating system. Advanced **Asset details** shows identity and dependency metadata.

## Assign an asset

Select the destination entity and find its component field in Inspector. Drag a compatible Content row onto the field. Starting the drag preserves the entity Inspector. A regular click selects the asset instead.

Alternatively open the field's picker, search, and choose a compatible asset. **None / Clear** removes the reference; **Reveal in Content** selects the referenced asset without modifying the scene. Fields validate expected asset type. Assignment is a scene edit and supports Undo/Redo; asset creation and external file edits do not.

## Create or register supported content

Open **Create / Register**:

- **New scene** creates an untitled scene through the save guard.
- **Audio / Register WAV** registers a WAV already copied into this project. Enter its project-relative path and choose **Register WAV**.
- **Prefabs** contains Create from selection, Instantiate, Edit source and Duplicate asset.
- The animation, navigation and Runtime UI sections expose their existing conversion, build and registration tools.

See [Audio](audio.md), [Animation](animation.md), [Navigation](navigation.md), [Runtime UI](runtime-ui.md) and [Prefabs](prefabs.md). Stop Play to create/register assets; browsing remains available.

## Current limits

This is a catalog browser, not a general importer or file manager. Arbitrary file importing, thumbnails, file rename/delete, asset placement into the Scene, and cooking are not implemented. A failed registration leaves existing good assets intact. Problems retains errors; the operation also displays its error locally.

Scene discovery skips `.forge`, `.git` and symbolic links. Scans are bounded to 16 directory levels, 10,000 entries and 64 MiB of JSON candidates, with an 8 MiB per-file limit. Use **File → Open scene...** if a scan exceeds these limits. Package metadata and project manifests are not scenes.

The default bottom panel keeps search and filters compact so asset rows remain visible. Prefab assets come from the project’s existing prefab library and retain their AssetIds. Selecting a newly created prefab resolves it even when Content is hidden. **Reveal in Content** brings that tab forward. Closing a prefab source returns its selection to the asset, rather than leaving a closed member draft selected.
